#!/data/data/com.termux/files/usr/bin/bash
# Startup script for KWin on Android (Termux)

set -e

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

# Optional: Set custom resolution
# export KWIN_ANDROID_WIDTH=1920
# export KWIN_ANDROID_HEIGHT=1080
# export KWIN_ANDROID_REFRESH=60

# Optional: Enable debug logging
# export QT_LOGGING_RULES="kwin_*.debug=true"
# export KWIN_LOG_LEVEL=debug

# Optional: Performance tuning
export QSG_RENDER_LOOP=basic
export QT_XCB_GL_INTEGRATION=none

# Check dependencies
echo "Checking dependencies..."

# Find termux-render library
TERMUX_RENDER_LIB=""
for lib in "libtermux-render.so" "termux-render.so" "librender.so"; do
    if [ -f "$PREFIX/lib/$lib" ]; then
        TERMUX_RENDER_LIB="$PREFIX/lib/$lib"
        echo "Found termux-render: $TERMUX_RENDER_LIB"
        break
    fi
done

if [ -z "$TERMUX_RENDER_LIB" ]; then
    echo "Error: termux-display-client library not found"
    echo "Please install termux-display-client first"
    exit 1
fi

# Check for VirtualGL libraries
VGL_CLIENT_LIB=""
EGL_INTERCEPT_LIB=""

for lib_path in "$PREFIX/lib"; do
    if [ -f "$lib_path/libvirtualgl-client.so" ]; then
        VGL_CLIENT_LIB="$lib_path/libvirtualgl-client.so"
        echo "Found VirtualGL client: $VGL_CLIENT_LIB"
    fi
    if [ -f "$lib_path/libegl-intercept.so" ]; then
        EGL_INTERCEPT_LIB="$lib_path/libegl-intercept.so"
        echo "Found EGL intercept: $EGL_INTERCEPT_LIB"
    fi
done

# Set up library preloading
PRELOAD_LIBS="$TERMUX_RENDER_LIB"

if [ -n "$EGL_INTERCEPT_LIB" ] && [ -n "$VGL_CLIENT_LIB" ]; then
    echo "✓ VirtualGL libraries found - enabling hardware acceleration"
    PRELOAD_LIBS="$EGL_INTERCEPT_LIB:$VGL_CLIENT_LIB:$PRELOAD_LIBS"
    export VGL_DEBUG=1
    
    # Check if VirtualGL server is running
    if [ -S "/data/data/com.termux/files/usr/tmp/virtualgl-0" ]; then
        echo "✓ VirtualGL server detected - hardware acceleration enabled"
    else
        echo "⚠ VirtualGL server not detected - make sure termux-app is running"
    fi
else
    echo "⚠ VirtualGL libraries not found - using software rendering"
fi

export LD_PRELOAD="$PRELOAD_LIBS:${LD_PRELOAD}"
echo "LD_PRELOAD: $LD_PRELOAD"

if [ ! -f "$PREFIX/bin/kwin_wayland" ]; then
    echo "Error: KWin not found"
    echo "Please install KWin first"
    exit 1
fi

# Create runtime directory if not exists
mkdir -p "$XDG_RUNTIME_DIR"

# Check if display server is running
if [ ! -S "$XDG_RUNTIME_DIR/wayland-0" ]; then
    echo "Warning: Display server socket not found at $XDG_RUNTIME_DIR/wayland-0"
    echo "Make sure termux-app display server is running"
    sleep 2
fi

echo "Starting KWin with Android backend..."
echo "Backend: $KWIN_BACKEND"
echo "Runtime dir: $XDG_RUNTIME_DIR"

# Start KWin
# --xwayland: Enable XWayland for X11 app support
# --drm: Disable (not used on Android)
# --replace: Replace existing window manager
exec kwin_wayland \
    --xwayland \
    --no-kactivities \
    --no-global-shortcuts \
    --replace \
    "$@"
