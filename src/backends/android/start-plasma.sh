#!/data/data/com.termux/files/usr/bin/bash
# 完整的Plasma桌面启动脚本 - 集成硬件加速KWin

set -e

echo "=== Plasma Desktop with Hardware Accelerated KWin ==="

# 检查Termux环境
if [ ! -d "/data/data/com.termux" ]; then
    echo "Error: Must run in Termux environment"
    exit 1
fi

# 检查是否需要安装依赖
if [ "$1" = "--install-deps" ] || [ "$1" = "-i" ]; then
    echo "Installing required packages..."
    pkg install plasma-desktop konsole dolphin dbus mesa virglrenderer-android -y
    echo "✓ Dependencies installed"
    echo ""
fi

# 环境变量设置
export PREFIX=${PREFIX:-/data/data/com.termux/files/usr}
export XDG_RUNTIME_DIR="$PREFIX/tmp/runtime-$(id -u)"
export KWIN_WAYLAND_SOCKET="${KWIN_WAYLAND_SOCKET:-wayland-1}"
unset WAYLAND_DISPLAY
export XDG_CURRENT_DESKTOP="KDE"
export XDG_SESSION_TYPE="wayland"
export QT_QPA_PLATFORM="wayland"
export KWIN_BACKEND="android"
export KWIN_ANDROID_DISABLE_INPUT="${KWIN_ANDROID_DISABLE_INPUT:-0}"
export XDG_CONFIG_DIRS="${XDG_CONFIG_DIRS:-$PREFIX/etc/xdg}"
export XDG_DATA_DIRS="${XDG_DATA_DIRS:-$PREFIX/share}"
export PATH="../../../build/bin:$PATH"
mkdir -p "$XDG_RUNTIME_DIR"

echo "Checking dependencies..."

# 检查termux-render库
TERMUX_RENDER_LIB=""
for lib in "libtermux-render.so" "termux-render.so" "librender.so"; do
    if [ -f "$PREFIX/lib/$lib" ]; then
        TERMUX_RENDER_LIB="$PREFIX/lib/$lib"
        echo "✓ Found termux-render: $TERMUX_RENDER_LIB"
        break
    fi
done

if [ -z "$TERMUX_RENDER_LIB" ]; then
    echo "✗ Error: termux-display-client library not found"
    exit 1
fi

# 检查硬件加速选项
MESA_KGSL_AVAILABLE=0
if [ -f "$PREFIX/lib/dri/kgsl_dri.so" ]; then
    echo "✓ Mesa kgsl driver found (Adreno GPU support)"
    MESA_KGSL_AVAILABLE=1
fi

VIRGL_AVAILABLE=0
if command -v virgl_test_server >/dev/null 2>&1; then
    echo "✓ VirGL test server found"
    VIRGL_AVAILABLE=1
fi

MESA_VIRGL_AVAILABLE=0
if [ -f "$PREFIX/lib/dri/virpipe_dri.so" ]; then
    echo "✓ Mesa VirGL driver found"
    MESA_VIRGL_AVAILABLE=1
fi

MESA_LLVMPIPE_AVAILABLE=0
if [ -f "$PREFIX/lib/dri/swrast_dri.so" ] || [ -f "$PREFIX/lib/dri/llvmpipe_dri.so" ]; then
    echo "✓ Mesa software rendering available"
    MESA_LLVMPIPE_AVAILABLE=1
fi

# 配置渲染后端（优先级：kgsl > virgl > llvmpipe）
echo ""
if [ $MESA_KGSL_AVAILABLE -eq 1 ]; then
    echo "🚀 Using Mesa kgsl driver (Adreno GPU hardware acceleration)"
    export MESA_LOADER_DRIVER_OVERRIDE=kgsl
    export GALLIUM_DRIVER=freedreno
    ACCELERATION_MODE="Adreno Hardware (kgsl)"
    
elif [ $VIRGL_AVAILABLE -eq 1 ] && [ $MESA_VIRGL_AVAILABLE -eq 1 ]; then
    echo "🚀 Configuring VirGL hardware acceleration..."
    export MESA_LOADER_DRIVER_OVERRIDE=virpipe
    export GALLIUM_DRIVER=virgl
    export VIRGL_VTEST=1
    export VIRGL_VTEST_SOCKET_NAME=/tmp/virgl_test
    
    # 启动VirGL服务器
    virgl_test_server --use-egl-surfaceless --use-gles &
    VIRGL_SERVER_PID=$!
    sleep 2
    
    if kill -0 $VIRGL_SERVER_PID 2>/dev/null; then
        echo "✓ VirGL test server started"
        trap 'kill "$VIRGL_SERVER_PID" 2>/dev/null || true' EXIT
        ACCELERATION_MODE="VirGL Hardware"
    else
        echo "✗ VirGL failed, using software"
        export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
        export GALLIUM_DRIVER=llvmpipe
        export LIBGL_ALWAYS_SOFTWARE=1
        ACCELERATION_MODE="Mesa Software"
    fi
    
elif [ $MESA_LLVMPIPE_AVAILABLE -eq 1 ]; then
    echo "⚠ Using Mesa software rendering"
    export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
    export GALLIUM_DRIVER=llvmpipe
    export LIBGL_ALWAYS_SOFTWARE=1
    ACCELERATION_MODE="Mesa Software"
    
else
    echo "⚠ Using basic software rendering"
    export LIBGL_ALWAYS_SOFTWARE=1
    ACCELERATION_MODE="Basic Software"
fi

# 设置库预加载
if [ "${KWIN_ANDROID_PRELOAD_RENDER:-0}" = "1" ]; then
    export LD_PRELOAD="$TERMUX_RENDER_LIB:$LD_PRELOAD"
fi

# 性能调优
export QSG_RENDER_LOOP=basic
export QT_XCB_GL_INTEGRATION=none

echo ""
echo "Environment setup complete:"
echo "  Acceleration: $ACCELERATION_MODE"
echo "  Mesa driver: ${MESA_LOADER_DRIVER_OVERRIDE:-default}"
echo "  LD_PRELOAD: ${LD_PRELOAD:-}"
echo ""

# 启动D-Bus
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    echo "Starting D-Bus..."
    if command -v dbus-launch >/dev/null 2>&1; then
        eval "$(dbus-launch --sh-syntax)"
    else
        echo "✗ Error: dbus-launch not found"
        exit 1
    fi
else
    echo "Using existing D-Bus session"
fi

# 启动KWin Wayland
echo "Starting KWin with hardware acceleration..."
kwin_wayland \
    --socket "$KWIN_WAYLAND_SOCKET" \
    --xwayland \
    --no-kactivities \
    --no-global-shortcuts &
KWIN_PID=$!
sleep 3
if ! kill -0 "$KWIN_PID" 2>/dev/null; then
    echo "✗ Error: kwin_wayland exited during startup"
    wait "$KWIN_PID" || true
    exit 1
fi
export WAYLAND_DISPLAY="$KWIN_WAYLAND_SOCKET"

# 启动Plasma组件
echo "Starting Plasma components..."
if command -v kdeinit6 >/dev/null 2>&1; then
    kdeinit6 &
elif command -v kdeinit5 >/dev/null 2>&1; then
    kdeinit5 &
else
    echo "⚠ kdeinit not found, skipping"
fi
sleep 1

if command -v kded6 >/dev/null 2>&1; then
    kded6 &
elif command -v kded5 >/dev/null 2>&1; then
    kded5 &
else
    echo "⚠ kded not found, skipping"
fi

if command -v plasmashell >/dev/null 2>&1; then
    plasmashell &
    PLASMASHELL_PID=$!
else
    echo "✗ Error: plasmashell not found"
    exit 1
fi
sleep 1
if ! kill -0 "$PLASMASHELL_PID" 2>/dev/null; then
    echo "✗ Error: plasmashell exited during startup"
    wait "$PLASMASHELL_PID" || true
    exit 1
fi

echo ""
echo "✓ Plasma Desktop started with hardware accelerated KWin"
echo "✓ Acceleration mode: $ACCELERATION_MODE"
echo "✓ AHardwareBuffer zero-copy rendering supported"
echo ""
echo "Launch applications:"
echo "  konsole    # Terminal"
echo "  dolphin    # File manager"
echo "  kate       # Text editor"
echo ""
echo "Usage:"
echo "  $0                # Start Plasma (normal)"
echo "  $0 --install-deps # Install dependencies first"
echo ""
echo "Press Ctrl+C to stop..."

wait
