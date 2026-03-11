#!/data/data/com.termux/files/usr/bin/bash
# KWin startup script with VirtualGL hardware acceleration

set -e

echo "Starting KWin with VirtualGL acceleration..."

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

# VirtualGL configuration
export VGL_DISPLAY=:0
export VGL_COMPRESS=rgb
export VGL_READBACK=sync

# Optional: Set custom resolution
export KWIN_ANDROID_WIDTH=${KWIN_ANDROID_WIDTH:-1920}
export KWIN_ANDROID_HEIGHT=${KWIN_ANDROID_HEIGHT:-1080}
export KWIN_ANDROID_REFRESH=${KWIN_ANDROID_REFRESH:-60}

# Performance tuning
export QSG_RENDER_LOOP=basic

# Create runtime directory
mkdir -p "$XDG_RUNTIME_DIR"

echo "Checking dependencies..."

# Check for KWin
if [ ! -f "$PREFIX/bin/kwin_wayland" ]; then
    echo "Error: KWin not found"
    echo "Please install KWin first"
    exit 1
fi

# Check for VirtualGL client library
VGL_CLIENT_LIB=""
for lib in "$PREFIX/lib/libvirtualgl-client.so" "$PREFIX/lib/virtualgl-client.so"; do
    if [ -f "$lib" ]; then
        VGL_CLIENT_LIB="$lib"
        echo "Found VirtualGL client library: $lib"
        break
    fi
done

# Check for termux-render library
TERMUX_RENDER_LIB=""
for lib in "$PREFIX/lib/libtermux-render.so" "$PREFIX/lib/termux-render.so"; do
    if [ -f "$lib" ]; then
        TERMUX_RENDER_LIB="$lib"
        echo "Found termux-render library: $lib"
        break
    fi
done

if [ -z "$TERMUX_RENDER_LIB" ]; then
    echo "Error: termux-render library not found"
    echo "Please install termux-display-client first"
    exit 1
fi

# Check if VirtualGL server is running
VGL_SERVER_SOCKET="/data/data/com.termux/files/usr/tmp/virtualgl-0"
if [ -S "$VGL_SERVER_SOCKET" ]; then
    echo "VirtualGL server detected - hardware acceleration enabled"
    USE_VGL=1
else
    echo "VirtualGL server not detected - using software rendering"
    echo "Make sure termux-app with VirtualGL support is running"
    USE_VGL=0
fi

# Set library paths
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH}"

# Preload libraries based on availability
if [ "$USE_VGL" = "1" ] && [ -n "$VGL_CLIENT_LIB" ]; then
    export LD_PRELOAD="$VGL_CLIENT_LIB:$TERMUX_RENDER_LIB:${LD_PRELOAD}"
    echo "Preloading VirtualGL client for hardware acceleration"
else
    export LD_PRELOAD="$TERMUX_RENDER_LIB:${LD_PRELOAD}"
    echo "Using software rendering mode"
fi

echo "Starting KWin..."
echo "Backend: $KWIN_BACKEND"
echo "Runtime dir: $XDG_RUNTIME_DIR"
echo "Library path: $LD_LIBRARY_PATH"
echo "Preload: $LD_PRELOAD"

# Start KWin with error handling
if ! kwin_wayland \
    --xwayland \
    --no-kactivities \
    --no-global-shortcuts \
    --replace \
    "$@"; then
    echo "Error: KWin failed to start"
    echo ""
    echo "Troubleshooting:"
    echo "1. Make sure termux-app is running"
    echo "2. Check if termux-display-client is properly installed"
    echo "3. For hardware acceleration, ensure termux-app has VirtualGL support"
    echo "4. Try software-only mode: LIBGL_ALWAYS_SOFTWARE=1 $0"
    exit 1
fi