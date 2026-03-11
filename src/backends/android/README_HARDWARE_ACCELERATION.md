# KWin Android Backend - Hardware Acceleration

## 架构概述

KWin Android backend支持两种渲染路径：

1. **零拷贝直接渲染** (首选): Mesa直接渲染到与termux-app共享的AHardwareBuffer
2. **传统渲染** (回退): Mesa渲染到EGL framebuffer，然后通过glReadPixels复制

### 硬件加速后端 (优先级从高到低)：
1. **Mesa kgsl** - 直接访问Adreno GPU (推荐)
2. **VirGL** - 虚拟化GPU加速  
3. **Mesa llvmpipe** - CPU软件渲染

### AHardwareBuffer零拷贝渲染：
- 要求: Android API >= 26, Mesa支持EGL_ANDROID_image_native_buffer扩展
- 优势: 消除glReadPixels开销，提升性能
- 实现: 通过termux-render的LorieBuffer获取AHardwareBuffer，创建EGLImage直接渲染
4. **基础软件渲染** - 回退选项

## 核心机制

### 渲染路径自动选择

系统启动时自动检测并选择最佳渲染路径：

#### 1. 零拷贝直接渲染 (首选)
```cpp
// 检测AHardwareBuffer支持
if (desc->type == LORIEBUFFER_AHARDWAREBUFFER && desc->buffer) {
    // 创建EGLImage直接绑定到AHardwareBuffer
    EGLImageKHR eglImage = eglCreateImageKHR(display, EGL_NO_CONTEXT, 
                                             EGL_NATIVE_BUFFER_ANDROID, 
                                             clientBuffer, imageAttribs);
    // 直接渲染，无需复制
}
```

**优势**：
- 消除glReadPixels开销
- 零拷贝性能提升
- Mesa直接渲染到共享AHardwareBuffer

#### 2. 传统渲染 (回退)
```cpp
// android_egl_backend.cpp - doEndFrame()
glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, shared_buffer);
```

**使用场景**：
- Android API < 26 (不支持AHardwareBuffer)
- Mesa缺少EGL_ANDROID_image_native_buffer扩展
- LorieBuffer不是AHardwareBuffer类型

### 为什么需要共享缓冲区

1. **进程隔离**：KWin运行在Termux子进程中，无法直接访问Android的Surface
2. **EGL上下文隔离**：KWin的EGL上下文与termux-app的显示上下文分离
3. **跨进程通信**：需要通过共享内存(AHardwareBuffer或传统buffer)传递渲染结果

## 硬件加速方案

### 1. Mesa kgsl (最佳)
```bash
export MESA_LOADER_DRIVER_OVERRIDE=kgsl
export GALLIUM_DRIVER=freedreno
```

**优势**：
- 直接访问`/dev/kgsl-3d0`设备
- 真正的Adreno GPU硬件加速
- 无需额外服务进程
- 性能最高

**限制**：
- 仅支持Adreno GPU
- 需要kgsl内核驱动支持

### 2. VirGL (通用)
```bash
export MESA_LOADER_DRIVER_OVERRIDE=virpipe
export GALLIUM_DRIVER=virgl
virgl_test_server --use-egl-surfaceless --use-gles &
```

**优势**：
- 支持多种GPU
- 成熟的虚拟化方案
- 良好的兼容性

**限制**：
- 需要额外的virgl_test_server进程
- 有一定的虚拟化开销

### 3. Mesa llvmpipe (软件)
```bash
export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
export GALLIUM_DRIVER=llvmpipe
export LIBGL_ALWAYS_SOFTWARE=1
```

**优势**：
- 纯CPU实现，兼容性最好
- 支持完整的OpenGL功能

**限制**：
- 性能较低
- CPU占用高

## 使用方法

### 安装依赖
```bash
# 基础依赖
pkg install mesa

# Adreno GPU支持 (如果可用)
# Mesa kgsl驱动通常包含在mesa包中

# VirGL支持 (可选)
pkg install virglrenderer-android
```

### 启动KWin
```bash
# 自动检测最佳渲染方式
./start-kwin-virgl.sh

# 或手动指定
MESA_LOADER_DRIVER_OVERRIDE=kgsl ./start-kwin-virgl.sh
```

## 技术细节

### EGL初始化流程
1. KWin检测可用的Mesa驱动
2. 根据优先级选择渲染后端
3. 初始化EGL上下文和framebuffer
4. 设置与termux-render的共享buffer连接

### 渲染流程

#### 零拷贝模式：
1. KWin直接渲染到AHardwareBuffer绑定的EGL framebuffer
2. `doEndFrame()`跳过像素复制，直接通知完成
3. termux-app直接访问AHardwareBuffer显示

#### 传统模式：
1. KWin在标准EGL framebuffer中渲染帧
2. `doEndFrame()`调用`glReadPixels`复制像素数据
3. 像素数据写入共享buffer (LorieBuffer)
4. 通知termux-app有新帧可显示
5. termux-app从共享buffer读取并显示

### 性能优化
- **零拷贝渲染**: 消除glReadPixels开销 (推荐)
- **传统优化**: 使用`GL_READ_FRAMEBUFFER`减少状态切换
- **Buffer管理**: 合理的AHardwareBuffer/LorieBuffer锁定机制

## 故障排除

### 检查AHardwareBuffer支持
```bash
# 运行测试脚本
./test-ahardwarebuffer.sh

# 检查Android API级别
getprop ro.build.version.sdk  # 需要 >= 26

# 查看KWin日志中的渲染模式
# 零拷贝: "Mesa direct rendering to AHardwareBuffer setup successful"
# 回退: "Falling back to traditional rendering with glReadPixels"
```

### 检查GPU支持
```bash
# 检查kgsl设备
ls -la /dev/kgsl-3d0

# 检查Mesa驱动
ls $PREFIX/lib/dri/

# 检查EGL信息
eglinfo
```

### 常见问题
1. **黑屏**：检查共享buffer是否正确初始化
2. **性能差**：确认是否使用了硬件加速
3. **崩溃**：检查EGL上下文创建是否成功

## 与其他方案对比

| 方案 | 性能 | 兼容性 | 复杂度 | 推荐度 |
|------|------|--------|--------|--------|
| Mesa kgsl | 最高 | Adreno only | 低 | ⭐⭐⭐⭐⭐ |
| VirGL | 高 | 通用 | 中 | ⭐⭐⭐⭐ |
| Mesa llvmpipe | 中 | 最好 | 低 | ⭐⭐⭐ |
| 自制VirtualGL | 中 | 通用 | 高 | ❌ (已移除) |

Mesa的现成方案比自制VirtualGL更成熟、更高效，因此我们采用Mesa作为主要的硬件加速解决方案。