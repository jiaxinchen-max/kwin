# KWin Android Backend - Implementation Details

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────────┐
│                          Android System                           │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │  termux-app (Java/Kotlin)                                    │ │
│  │  ┌────────────────────────────────────────────────────────┐  │ │
│  │  │  waylandRenderServer.c (Native C)                      │  │ │
│  │  │                                                          │  │ │
│  │  │  1. Creates Unix Socket (/tmp/wayland-0)               │  │ │
│  │  │  2. Allocates AHardwareBuffer (GPU accessible)         │  │ │
│  │  │  3. Creates EGL Context (for display)                  │  │ │
│  │  │  4. Sends AHardwareBuffer FD to clients                │  │ │
│  │  │  5. Waits for drawRequested signal                     │  │ │
│  │  │  6. Renders buffer content to Android Surface          │  │ │
│  │  └────────────────────────────────────────────────────────┘  │ │
│  └───────────────────────┬──────────────────────────────────────┘ │
│                          │                                          │
│                          │ Unix Socket                              │
│                          │ + AHardwareBuffer FD                     │
│                          │ (via ancil_send_fd)                      │
│                          │                                          │
│  ┌───────────────────────▼──────────────────────────────────────┐ │
│  │  termux-display-client.so (Shared Library)                   │ │
│  │                                                                │ │
│  │  - Connects to Unix Socket                                    │ │
│  │  - Receives AHardwareBuffer FD (via ancil_recv_fd)           │ │
│  │  - Wraps AHardwareBuffer in LorieBuffer struct               │ │
│  │  - Provides C API: get_lorieBuffer(), get_serverState()      │ │
│  │  - Manages shared memory for lorie_shared_server_state       │ │
│  └───────────────────────┬──────────────────────────────────────┘ │
│                          │                                          │
│                          │ C API                                    │
│                          │                                          │
│  ┌───────────────────────▼──────────────────────────────────────┐ │
│  │  libKWinAndroidBackend.so (This Implementation)              │ │
│  │                                                                │ │
│  │  ┌──────────────────────────────────────────────────────┐    │ │
│  │  │  AndroidBackend (OutputBackend)                      │    │ │
│  │  │  - Calls connectToRender()                           │    │ │
│  │  │  - Gets LorieBuffer* and lorie_shared_server_state*  │    │ │
│  │  │  - Creates AndroidOutput                             │    │ │
│  │  │  - Creates AndroidEglBackend                         │    │ │
│  │  │  - Handles input events from socket                  │    │ │
│  │  └──────────────────────────────────────────────────────┘    │ │
│  │                                                                │ │
│  │  ┌──────────────────────────────────────────────────────┐    │ │
│  │  │  AndroidEglBackend (EglBackend)                      │    │ │
│  │  │                                                       │    │ │
│  │  │  1. Creates EGL Display (EGL_DEFAULT_DISPLAY)        │    │ │
│  │  │  2. Binds OpenGL ES API (eglBindAPI)                 │    │ │
│  │  │  3. Creates EGL Context (OpenGL ES 3.0)              │    │ │
│  │  │  4. Creates pbuffer surface for makeCurrent          │    │ │
│  │  │  5. Creates AndroidEglLayer for rendering            │    │ │
│  │  └──────────────────────────────────────────────────────┘    │ │
│  │                                                                │ │
│  │  ┌──────────────────────────────────────────────────────┐    │ │
│  │  │  AndroidEglLayer (OutputLayer)                       │    │ │
│  │  │                                                       │    │ │
│  │  │  setupRenderTarget():                                │    │ │
│  │  │    1. Get AHardwareBuffer from LorieBuffer           │    │ │
│  │  │    2. eglGetNativeClientBufferANDROID(ahb)           │    │ │
│  │  │    3. eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)   │    │ │
│  │  │    4. glEGLImageTargetTexture2DOES(EGLImage)         │    │ │
│  │  │    5. glFramebufferTexture2D(texture)                │    │ │
│  │  │    6. Check FBO completeness                         │    │ │
│  │  │                                                       │    │ │
│  │  │  doBeginFrame():                                     │    │ │
│  │  │    - Bind FBO                                        │    │ │
│  │  │    - Return RenderTarget(fbo)                        │    │ │
│  │  │                                                       │    │ │
│  │  │  doEndFrame():                                       │    │ │
│  │  │    - glFlush() / glFinish()                          │    │ │
│  │  │    - Signal termux-app (drawRequested = 1)          │    │ │
│  │  │    - pthread_cond_signal()                           │    │ │
│  │  └──────────────────────────────────────────────────────┘    │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │  KWin Compositor Core                                        │ │
│  │  - Receives RenderTarget from AndroidEglLayer                │ │
│  │  - Renders windows, effects, etc. into FBO                   │ │
│  │  - FBO is backed by AHardwareBuffer texture                  │ │
│  │  - GPU writes directly to shared memory                      │ │
│  └──────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────┘
```

## Key Implementation Files

### 1. `android_backend.h/cpp`
**Purpose**: Main backend implementation

**Key Classes**:
- `AndroidBackend`: Inherits from `OutputBackend`
- `AndroidInputBackend`: Handles input devices
- `AndroidInputDevice`: Represents keyboard/pointer/touch

**Responsibilities**:
- Connect to termux-app display server via `connectToRender()`
- Receive `AHardwareBuffer` handle from server
- Create output and input devices
- Process input events from Unix socket
- Convert Android input events to KWin input events

**Key Methods**:
```cpp
bool AndroidBackend::initialize() {
    connectToDisplayServer();  // Connect via termux-wayland
    createOutput();             // Create virtual display
    setupInputDevices();        // Create input devices
    return true;
}

void AndroidBackend::handleInputEvents() {
    lorieEvent e;
    while (read(m_connFd, &e, sizeof(e)) == sizeof(e)) {
        processInputEvent(e);  // Dispatch to appropriate device
    }
}
```

### 2. `android_output.h/cpp`
**Purpose**: Virtual display output

**Key Class**: `AndroidOutput` (inherits from `BackendOutput`)

**Responsibilities**:
- Represent the Android screen as a KWin output
- Provide output mode (resolution, refresh rate)
- Signal frame presentation to termux-app

**Key Methods**:
```cpp
void AndroidOutput::present() {
    // Signal termux-app that frame is ready
    lorie_mutex_lock(&state->lock, &state->lockingPid);
    state->drawRequested = 1;
    pthread_cond_signal(&state->cond);
    lorie_mutex_unlock(&state->lock, &state->lockingPid);
}
```

### 3. `android_egl_backend.h/cpp`
**Purpose**: OpenGL ES rendering backend

**Key Classes**:
- `AndroidEglBackend`: Inherits from `EglBackend`
- `AndroidEglLayer`: Inherits from `OutputLayer`

**Responsibilities**:
- Initialize EGL with OpenGL ES API
- Create EGL context for rendering
- Import `AHardwareBuffer` as render target
- Provide rendering surface to KWin compositor

**Critical EGL/OpenGL Flow**:
```cpp
bool AndroidEglBackend::initializeEgl() {
    // 1. Get EGL display
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, &major, &minor);
    
    // 2. Bind OpenGL ES API (critical for Android)
    eglBindAPI(EGL_OPENGL_ES_API);
    
    // 3. Choose config for GLES 3.0
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);
    
    // 4. Create GLES 3.0 context
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    
    // 5. Create pbuffer for makeCurrent (render to FBO, not window)
    pbuffer = eglCreatePbufferSurface(display, config, pbufferAttribs);
    eglMakeCurrent(display, pbuffer, pbuffer, context);
    
    return true;
}
```

**Zero-Copy Rendering Setup**:
```cpp
bool AndroidEglLayer::setupRenderTarget() {
    // 1. Get AHardwareBuffer from termux-wayland
    AHardwareBuffer *ahb = desc->buffer;
    
    // 2. Convert to EGLClientBuffer (Android-specific)
    EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(ahb);
    
    // 3. Create EGLImage from AHardwareBuffer
    m_eglImage = eglCreateImageKHR(
        display,
        EGL_NO_CONTEXT,  // Shared between contexts
        EGL_NATIVE_BUFFER_ANDROID,  // Android-specific image type
        clientBuffer,
        imageAttribs
    );
    
    // 4. Create OpenGL texture
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    
    // 5. Bind EGLImage to texture (establishes GPU buffer sharing)
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)m_eglImage);
    
    // 6. Create framebuffer and attach texture
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        m_texture,
        0
    );
    
    // 7. Verify framebuffer is complete
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        return false;
    }
    
    // 8. Wrap in GLFramebuffer for KWin
    m_fbo = std::make_unique<GLFramebuffer>(m_framebuffer, size);
    
    return true;
}
```

**Rendering Flow**:
```cpp
std::optional<OutputLayerBeginFrameInfo> AndroidEglLayer::doBeginFrame() {
    // Make context current
    m_backend->openglContext()->makeCurrent();
    
    // Setup render target if needed
    if (!m_initialized) {
        setupRenderTarget();
    }
    
    // Bind our FBO (backed by AHardwareBuffer)
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    
    // Return FBO to KWin for rendering
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(m_fbo.get()),
        .repaint = Region::infinite(),
    };
}

bool AndroidEglLayer::doEndFrame(...) {
    // Ensure GPU finishes all rendering to AHardwareBuffer
    glFlush();
    glFinish();  // Wait for GPU completion
    
    // Signal termux-app that frame is ready
    Termux::signalFrameReady(m_backend->backend()->serverState());
    
    return true;
}
```

### 4. `termux_display_api.h`
**Purpose**: Type-safe C++ wrapper for termux-display-client C API

**Provides**:
- Type definitions (`LorieBuffer`, `lorie_shared_server_state`, `lorieEvent`)
- Function declarations (`connectToRender()`, `get_lorieBuffer()`, etc.)
- C++ convenience wrappers (RAII locks, helper functions)

**Key Types**:
```cpp
// Buffer descriptor
typedef struct {
    int32_t width, height, stride;
    int8_t format, type;
    AHardwareBuffer* buffer;  // The shared GPU buffer
    void* data;
} LorieBuffer_Desc;

// Synchronization state
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    volatile uint8_t drawRequested;  // Set by KWin
    volatile uint8_t surfaceAvailable;  // Set by termux-app
    // ...
} lorie_shared_server_state;
```

## Build System

### CMakeLists.txt
```cmake
# Link termux-wayland library
target_link_libraries(KWinAndroidBackend
    kwin
    Qt::Core
    Qt::Gui
    EGL::EGL
    epoxy::epoxy
    wayland  # termux-display-client
)

# Include termux-display-client headers
target_include_directories(KWinAndroidBackend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../termux-display-client/include
)
```

## Input Handling

### Event Flow
1. User touches screen in termux-app
2. termux-app sends `lorieEvent` via Unix socket
3. `AndroidBackend::handleInputEvents()` reads event
4. Event is converted to KWin input event
5. Dispatched to appropriate `AndroidInputDevice`
6. KWin input system processes event

### Event Types
- **Touch Events**: Touch down/up/motion
- **Mouse Events**: Pointer motion, button clicks, scroll
- **Key Events**: Keyboard key press/release

### Keycode Conversion
Android keycodes are mapped to Linux input event codes via `android_to_linux_keycode[]` table.

## Synchronization

### Cross-Process Communication
Uses `lorie_shared_server_state` for synchronization:

```cpp
// KWin signals frame ready
lorie_mutex_lock(&state->lock, &state->lockingPid);
state->drawRequested = 1;
pthread_cond_signal(&state->cond);
lorie_mutex_unlock(&state->lock, &state->lockingPid);

// termux-app waits for signal
pthread_mutex_lock(&state->lock);
while (!state->drawRequested && state->surfaceAvailable) {
    pthread_cond_wait(&state->cond, &state->lock);
}
state->drawRequested = 0;
pthread_mutex_unlock(&state->lock);
// ... render buffer to Surface
```

## Memory Management

### AHardwareBuffer Lifecycle
1. **Allocation**: termux-app allocates via `AHardwareBuffer_allocate()`
2. **Transfer**: FD sent via Unix socket to KWin
3. **Import**: KWin imports via `eglGetNativeClientBufferANDROID()` + `eglCreateImageKHR()`
4. **Usage**: Both processes can access GPU memory
5. **Cleanup**: Reference counted, destroyed when all processes release

### Zero-Copy Guarantee
- No `memcpy()` or CPU copies
- GPU writes directly to shared `AHardwareBuffer`
- termux-app's GPU reads directly from same buffer
- Both EGL contexts share the same physical GPU memory

## Performance Considerations

### GPU Pipeline
```
KWin Render → GPU Writes to AHB → termux-app Samples AHB → Display
     (GLES Context 1)              (GLES Context 2)
```

### Synchronization Overhead
- Mutex lock/unlock: ~1-2µs
- pthread_cond_signal: ~5-10µs
- Total IPC latency: <100µs

### Bottlenecks
1. **GPU Fill Rate**: Large resolutions stress mobile GPUs
2. **Compositor Effects**: Blur, transparency, animations use GPU
3. **Window Count**: More windows = more draw calls

### Optimizations
- Use `glFinish()` to ensure GPU completion before signaling
- Minimize draw calls via batching
- Disable expensive compositor effects on low-end devices
- Consider lower resolution (1280x720 vs 1920x1080)

## Testing

### Unit Tests
```bash
# On device
./test-backend.sh
```

### Manual Testing
```bash
# Start KWin with logging
export QT_LOGGING_RULES="kwin_*.debug=true"
kwin_wayland --xwayland 2>&1 | tee kwin.log
```

### Performance Profiling
```bash
# GPU profiling
systrace.py -a com.termux gfx view

# CPU profiling
simpleperf record -a -g -p $(pidof kwin_wayland)
```

## Debugging

### EGL Errors
```cpp
EGLint error = eglGetError();
qCritical() << "EGL error:" << error;
```

### OpenGL Errors
```cpp
GLenum error = glGetError();
qCritical() << "GL error:" << error;
```

### Common Issues
1. **Black Screen**: Check `glCheckFramebufferStatus()`
2. **No Input**: Verify socket path and permissions
3. **Crash on Startup**: Check EGL extension availability
4. **Poor Performance**: Profile GPU usage, reduce effects

## Future Work

- [ ] Multi-buffer swapchain for reduced latency
- [ ] Explicit sync via EGL_ANDROID_native_fence_sync
- [ ] HDR support via EGL_EXT_gl_colorspace_bt2020_pq
- [ ] Variable refresh rate (VRR)
- [ ] Multi-display support
- [ ] Hardware cursor overlay
