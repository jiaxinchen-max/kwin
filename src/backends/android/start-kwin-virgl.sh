#!/data/data/com.termux/files/usr/bin/bash
# KWin startup script with VirGL hardware acceleration

set -e

echo "=== KWin with Hardware Acceleration Support ==="

# Check if running in Termux
if [ ! -d "/data/data/com.termux" ]; then
    echo "Error: This script must run in Termux environment"
    exit 1
fi

# Configuration
export KWIN_BACKEND=android
export QT_QPA_PLATFORM=wayland
export XDG_SESSION_TYPE=wayland
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/data/data/com.termux/files/usr/tmp}"

echo "Checking dependencies..."

# Check for termux-render library
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
    echo "Please install termux-display-client first"
    exit 1
fi

# Check for hardware acceleration options

# 1. Check for Mesa kgsl (Adreno GPU direct access)
MESA_KGSL_AVAILABLE=0
if [ -f "$PREFIX/lib/dri/kgsl_dri.so" ]; then
    echo "✓ Mesa kgsl driver found (Adreno GPU support)"
    MESA_KGSL_AVAILABLE=1
else
    echo "⚠ Mesa kgsl driver not found"
fi

# 2. Check for VirGL renderer
VIRGL_AVAILABLE=0
if command -v virgl_test_server >/dev/null 2>&1; then
    echo "✓ VirGL test server found"
    VIRGL_AVAILABLE=1
elif [ -f "$PREFIX/bin/virgl_test_server" ]; then
    echo "✓ VirGL test server found at $PREFIX/bin/virgl_test_server"
    VIRGL_AVAILABLE=1
else
    echo "⚠ VirGL test server not found"
    echo "Install with: pkg install virglrenderer-android"
fi

# 3. Check for Mesa with VirGL support
MESA_VIRGL_AVAILABLE=0
if [ -f "$PREFIX/lib/dri/virpipe_dri.so" ]; then
    echo "✓ Mesa VirGL driver found"
    MESA_VIRGL_AVAILABLE=1
else
    echo "⚠ Mesa VirGL driver not found"
fi

# 4. Check for Mesa software rendering
MESA_LLVMPIPE_AVAILABLE=0
if [ -f "$PREFIX/lib/dri/swrast_dri.so" ] || [ -f "$PREFIX/lib/dri/llvmpipe_dri.so" ]; then
    echo "✓ Mesa software rendering available"
    MESA_LLVMPIPE_AVAILABLE=1
else
    echo "⚠ Mesa software rendering not found"
    echo "Install with: pkg install mesa"
fi

# Check KWin
if [ ! -f "$PREFIX/bin/kwin_wayland" ]; then
    echo "✗ Error: KWin not found"
    echo "Please install KWin first"
    exit 1
fi

echo "✓ KWin found"

# Configure rendering backend (priority order: kgsl > virgl > llvmpipe > basic)
echo ""

if [ $MESA_KGSL_AVAILABLE -eq 1 ]; then
    echo "🚀 Using Mesa kgsl driver (Adreno GPU hardware acceleration)"
    export MESA_LOADER_DRIVER_OVERRIDE=kgsl
    export GALLIUM_DRIVER=freedreno
    ACCELERATION_MODE="Adreno Hardware (kgsl)"
    
elif [ $VIRGL_AVAILABLE -eq 1 ] && [ $MESA_VIRGL_AVAILABLE -eq 1 ]; then
    echo "🚀 Configuring VirGL hardware acceleration..."
    
    # Mesa VirGL configuration
    export MESA_LOADER_DRIVER_OVERRIDE=virpipe
    export GALLIUM_DRIVER=virgl
    
    # VirGL test server configuration
    export VIRGL_VTEST=1
    export VIRGL_VTEST_SOCKET_NAME=/tmp/virgl_test
    
    # Start VirGL test server in background
    echo "Starting VirGL test server..."
    virgl_test_server --use-egl-surfaceless --use-gles &
    VIRGL_SERVER_PID=$!
    
    # Wait a moment for server to start
    sleep 2
    
    # Check if server is running
    if kill -0 $VIRGL_SERVER_PID 2>/dev/null; then
        echo "✓ VirGL test server started (PID: $VIRGL_SERVER_PID)"
        
        # Cleanup function
        cleanup() {
            echo "Stopping VirGL test server..."
            kill $VIRGL_SERVER_PID 2>/dev/null || true
            wait $VIRGL_SERVER_PID 2>/dev/null || true
        }
        trap cleanup EXIT
        
        ACCELERATION_MODE="VirGL Hardware"
    else
        echo "✗ Failed to start VirGL test server, falling back to software"
        export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
        export GALLIUM_DRIVER=llvmpipe
        export LIBGL_ALWAYS_SOFTWARE=1
        ACCELERATION_MODE="Mesa Software (VirGL fallback)"
    fi
    
elif [ $MESA_LLVMPIPE_AVAILABLE -eq 1 ]; then
    echo "⚠ Using Mesa software rendering (llvmpipe)"
    export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
    export GALLIUM_DRIVER=llvmpipe
    export LIBGL_ALWAYS_SOFTWARE=1
    ACCELERATION_MODE="Mesa Software"
    
else
    echo "⚠ No Mesa drivers available, using basic software rendering"
    export LIBGL_ALWAYS_SOFTWARE=1
    ACCELERATION_MODE="Basic Software"
fi

# Set up library preloading
export LD_PRELOAD="$TERMUX_RENDER_LIB:$LD_PRELOAD"

# Create runtime directory
mkdir -p "$XDG_RUNTIME_DIR"

# Performance tuning
export QSG_RENDER_LOOP=basic
export QT_XCB_GL_INTEGRATION=none

# Optional: Enable debug logging
# export QT_LOGGING_RULES="kwin_*.debug=true"
# export KWIN_LOG_LEVEL=debug
# export VIRGL_DEBUG=1

echo ""
echo "Environment setup complete:"
echo "  Acceleration: $ACCELERATION_MODE"
echo "  Mesa driver: ${MESA_LOADER_DRIVER_OVERRIDE:-default}"
echo "  Gallium driver: ${GALLIUM_DRIVER:-default}"
echo "  LD_PRELOAD: $LD_PRELOAD"
echo ""

echo "Starting KWin..."
exec kwin_wayland \
    --xwayland \
    --no-kactivities \
    --no-global-shortcuts \
    --replace \
    "$@"