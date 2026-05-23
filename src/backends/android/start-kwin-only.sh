#!/data/data/com.termux/files/usr/bin/bash
# 仅启动KWin Wayland合成器 - 支持硬件加速

set -e

echo "=== Starting KWin Wayland Compositor ==="

# 检查Termux环境
if [ ! -d "/data/data/com.termux" ]; then
    echo "Error: Must run in Termux environment"
    exit 1
fi

# 环境变量设置
export PREFIX=${PREFIX:-/data/data/com.termux/files/usr}
export TMPDIR="${TMPDIR:-$PREFIX/tmp}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$TMPDIR/runtime-$(id -u)}"
TERMUX_RENDER_SOCKET_DIR="/data/data/com.termux/files/home/tmp"
TERMUX_RENDER_SOCKET="$TERMUX_RENDER_SOCKET_DIR/termux-render"
export KWIN_WAYLAND_SOCKET="${KWIN_WAYLAND_SOCKET:-wayland-1}"
unset WAYLAND_DISPLAY
export XDG_CURRENT_DESKTOP="KDE"
export XDG_SESSION_TYPE="wayland"
export QT_QPA_PLATFORM="wayland"
export KWIN_BACKEND="android"
export KWIN_ANDROID_DISABLE_INPUT="${KWIN_ANDROID_DISABLE_INPUT:-0}"
export KWIN_ANDROID_REFRESH_RATE="${KWIN_ANDROID_REFRESH_RATE:-27}"
mkdir -p "$XDG_RUNTIME_DIR" "$TMPDIR/.X11-unix"
chmod 1777 "$TMPDIR/.X11-unix" 2>/dev/null || true
mkdir -p "$TERMUX_RENDER_SOCKET_DIR"

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
    echo "✓ Mesa kgsl driver found (Adreno GPU)"
    MESA_KGSL_AVAILABLE=1
fi

VIRGL_AVAILABLE=0
if command -v virgl_test_server >/dev/null 2>&1 && [ -f "$PREFIX/lib/dri/virpipe_dri.so" ]; then
    echo "✓ VirGL available"
    VIRGL_AVAILABLE=1
fi

MESA_LLVMPIPE_AVAILABLE=0
if [ -f "$PREFIX/lib/dri/swrast_dri.so" ] || [ -f "$PREFIX/lib/dri/llvmpipe_dri.so" ]; then
    echo "✓ Mesa software rendering available"
    MESA_LLVMPIPE_AVAILABLE=1
fi

# 配置渲染后端
echo ""
if [ $MESA_KGSL_AVAILABLE -eq 1 ]; then
    echo "🚀 Using Mesa kgsl (Adreno GPU hardware acceleration)"
    export MESA_LOADER_DRIVER_OVERRIDE=kgsl
    export GALLIUM_DRIVER=freedreno
    ACCELERATION_MODE="Adreno Hardware"
    
elif [ $VIRGL_AVAILABLE -eq 1 ]; then
    echo "🚀 Using VirGL hardware acceleration"
    export MESA_LOADER_DRIVER_OVERRIDE=virpipe
    export GALLIUM_DRIVER=virgl
    export VIRGL_VTEST=1
    export VIRGL_VTEST_SOCKET_NAME="$TMPDIR/virgl_test"
    
    virgl_test_server --use-egl-surfaceless --use-gles &
    VIRGL_PID=$!
    sleep 2
    
    if kill -0 $VIRGL_PID 2>/dev/null; then
        echo "✓ VirGL server started"
        trap "kill $VIRGL_PID 2>/dev/null || true" EXIT
        ACCELERATION_MODE="VirGL Hardware"
    else
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

if [ "${KWIN_ANDROID_PRELOAD_RENDER:-0}" = "1" ]; then
    export LD_PRELOAD="$TERMUX_RENDER_LIB:$LD_PRELOAD"
fi

# 查找KWin
KWIN_BINARY=""
if [ -f "../../../build/bin/kwin" ]; then
    KWIN_BINARY="../../../build/bin/kwin"
elif [ -f "../../../build/src/kwin" ]; then
    KWIN_BINARY="../../../build/src/kwin"
elif [ -f "$PREFIX/bin/kwin" ]; then
    KWIN_BINARY="$PREFIX/bin/kwin"
elif command -v kwin_wayland >/dev/null 2>&1; then
    KWIN_BINARY="kwin_wayland"
else
    echo "✗ Error: KWin not found"
    echo "Please build KWin first: cmake --build build"
    exit 1
fi

echo "✓ Found KWin: $KWIN_BINARY"

# 启动D-Bus
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then
    echo "Starting D-Bus..."
    eval $(dbus-launch --sh-syntax)
    export DBUS_SESSION_BUS_ADDRESS
fi

echo ""
echo "Starting KWin Wayland compositor..."
echo "Acceleration: $ACCELERATION_MODE"
echo "AHardwareBuffer zero-copy: supported"
echo "WAYLAND_DISPLAY: $KWIN_WAYLAND_SOCKET"
echo "termux-render socket: $TERMUX_RENDER_SOCKET"
echo "Android input: $([ "$KWIN_ANDROID_DISABLE_INPUT" = "1" ] && echo disabled || echo enabled)"
echo "Connect-only: $([ "${KWIN_ANDROID_CONNECT_ONLY:-0}" = "1" ] && echo enabled || echo disabled)"
echo ""

# 启动KWin
exec "$KWIN_BINARY" \
    --socket "$KWIN_WAYLAND_SOCKET" \
    --no-kactivities \
    --no-global-shortcuts \
    "$@"
