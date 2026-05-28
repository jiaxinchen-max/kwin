#!/data/data/com.termux/files/usr/bin/bash
# 完整的Plasma桌面启动脚本 - 集成硬件加速KWin

set -e

INSTALL_DEPS=0
VERBOSE_LOG=0
for arg in "$@"; do
    case "$arg" in
        --install-deps|-i)
            INSTALL_DEPS=1
            ;;
        -verbose|--verbose)
            VERBOSE_LOG=1
            ;;
    esac
done

# 环境变量设置（日志初始化前）
export PREFIX=${PREFIX:-/data/data/com.termux/files/usr}
export TMPDIR="${TMPDIR:-$PREFIX/tmp}"

PLASMA_LOG="${PLASMA_LOG:-$PREFIX/tmp/start-plasma.log}"
mkdir -p "$(dirname "$PLASMA_LOG")"
: > "$PLASMA_LOG"
if [ "$VERBOSE_LOG" = "1" ]; then
    exec > >(tee -a "$PLASMA_LOG") 2>&1
else
    exec > "$PLASMA_LOG" 2>&1
fi

echo "=== Plasma Desktop with Hardware Accelerated KWin ==="
echo "Logging to: $PLASMA_LOG"

# 检查Termux环境
if [ ! -d "/data/data/com.termux" ]; then
    echo "Error: Must run in Termux environment"
    exit 1
fi

# 检查是否需要安装依赖
if [ "$INSTALL_DEPS" = "1" ]; then
    echo "Installing required packages..."
    pkg install plasma-desktop plasma-workspace kactivitymanagerd konsole dolphin dbus mesa virglrenderer-android -y
    echo "✓ Dependencies installed"
    echo ""
fi

# 环境变量设置
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$TMPDIR/runtime-$(id -u)}"
export KWIN_WAYLAND_SOCKET="${KWIN_WAYLAND_SOCKET:-wayland-0}"
export XDG_CURRENT_DESKTOP="KDE"
export XDG_SESSION_TYPE="wayland"
export DESKTOP_SESSION="${DESKTOP_SESSION:-plasma}"
export KDE_FULL_SESSION="${KDE_FULL_SESSION:-true}"
export KDE_SESSION_VERSION="${KDE_SESSION_VERSION:-6}"
export QT_QPA_PLATFORM="wayland"
export KWIN_BACKEND="android"
export KWIN_ANDROID_DISABLE_INPUT="${KWIN_ANDROID_DISABLE_INPUT:-0}"
export KWIN_ANDROID_REFRESH_RATE="${KWIN_ANDROID_REFRESH_RATE:-27}"
export KWIN_ANDROID_BUFFER_TYPE="${KWIN_ANDROID_BUFFER_TYPE:-fd}"
export KWIN_ANDROID_ENABLE_EGL="${KWIN_ANDROID_ENABLE_EGL:-1}"
export XDG_CONFIG_DIRS="${XDG_CONFIG_DIRS:-$PREFIX/etc/xdg}"
export XDG_DATA_DIRS="${XDG_DATA_DIRS:-$PREFIX/share}"
export XDG_MENU_PREFIX="${XDG_MENU_PREFIX:-plasma-}"
export XCURSOR_THEME="${XCURSOR_THEME:-breeze_cursors}"
mkdir -p "$XDG_RUNTIME_DIR" "$TMPDIR/.X11-unix"
chmod 1777 "$TMPDIR/.X11-unix" 2>/dev/null || true

find_kactivitymanagerd() {
    local candidate
    for candidate in \
        kactivitymanagerd \
        kactivitymanagerd6 \
        "$PREFIX/lib/libexec/kactivitymanagerd" \
        "$PREFIX/libexec/kactivitymanagerd" \
        "$PREFIX/lib/kf6/kactivitymanagerd" \
        "$PREFIX/lib/qt6/libexec/kactivitymanagerd"; do
        if command -v "$candidate" >/dev/null 2>&1; then
            command -v "$candidate"
            return 0
        fi
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done

    local service exec_line
    for service in \
        "$PREFIX/share/dbus-1/services/org.kde.ActivityManager.service" \
        "$PREFIX/share/dbus-1/services/org.kde.kactivitymanagerd.service"; do
        if [ -f "$service" ]; then
            exec_line="$(sed -n 's/^Exec=//p' "$service" | head -n 1)"
            if [ -n "$exec_line" ]; then
                echo "$exec_line"
                return 0
            fi
        fi
    done

    return 1
}

echo "Checking dependencies..."

# 检查termux-render库
TERMUX_RENDER_LIB="$PREFIX/lib/libtermux-render.so"
if [ -f "$TERMUX_RENDER_LIB" ]; then
    echo "✓ Found termux-render: $TERMUX_RENDER_LIB"
else
    echo "✗ Error: termux-display-client library not found"
    exit 1
fi

MISSING_COMMANDS=()
for cmd in dbus-launch kwin_wayland plasmashell; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        MISSING_COMMANDS+=("$cmd")
    fi
done
KACTIVITYMANAGERD_CMD="$(find_kactivitymanagerd || true)"
if [ -z "$KACTIVITYMANAGERD_CMD" ]; then
    MISSING_COMMANDS+=("kactivitymanagerd")
fi

if [ ${#MISSING_COMMANDS[@]} -gt 0 ]; then
    echo "✗ Missing required commands: ${MISSING_COMMANDS[*]}"
    echo "Install dependencies with:"
    echo "  pkg install plasma-desktop plasma-workspace kactivitymanagerd dbus"
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
    export VIRGL_VTEST_SOCKET_NAME="$TMPDIR/virgl_test"
    
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

# 性能调优
export QSG_RENDER_LOOP=basic
export QT_XCB_GL_INTEGRATION=none

echo ""
echo "Environment setup complete:"
echo "  Acceleration: $ACCELERATION_MODE"
echo "  Mesa driver: ${MESA_LOADER_DRIVER_OVERRIDE:-default}"
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

if command -v dbus-update-activation-environment >/dev/null 2>&1; then
    dbus-update-activation-environment \
        DBUS_SESSION_BUS_ADDRESS \
        DESKTOP_SESSION \
        KDE_FULL_SESSION \
        KDE_SESSION_VERSION \
        KWIN_BACKEND \
        KWIN_WAYLAND_SOCKET \
        QT_QPA_PLATFORM \
        WAYLAND_DISPLAY \
        XDG_CONFIG_DIRS \
        XDG_CURRENT_DESKTOP \
        XDG_DATA_DIRS \
        XDG_MENU_PREFIX \
        XDG_RUNTIME_DIR \
        XDG_SESSION_TYPE || true
fi

# 启动KWin Wayland
echo "Starting KWin with hardware acceleration..."
kwin_wayland \
    --socket "$KWIN_WAYLAND_SOCKET" \
    --xwayland \
    --no-global-shortcuts &
KWIN_PID=$!
sleep 3
if ! kill -0 "$KWIN_PID" 2>/dev/null; then
    echo "✗ Error: kwin_wayland exited during startup"
    wait "$KWIN_PID" || true
    exit 1
fi
export WAYLAND_DISPLAY="$KWIN_WAYLAND_SOCKET"
if command -v dbus-update-activation-environment >/dev/null 2>&1; then
    dbus-update-activation-environment WAYLAND_DISPLAY || true
fi

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

echo "Starting kactivitymanagerd: $KACTIVITYMANAGERD_CMD"
if [ -x "$KACTIVITYMANAGERD_CMD" ]; then
    "$KACTIVITYMANAGERD_CMD" &
else
    sh -c "$KACTIVITYMANAGERD_CMD" &
fi
KACTIVITYMANAGERD_PID=$!
sleep 1
if ! kill -0 "$KACTIVITYMANAGERD_PID" 2>/dev/null; then
    echo "✗ Error: kactivitymanagerd exited during startup"
    wait "$KACTIVITYMANAGERD_PID" || true
    exit 1
fi

if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
elif command -v kbuildsycoca5 >/dev/null 2>&1; then
    kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
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
