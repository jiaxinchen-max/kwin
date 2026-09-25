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
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$TMPDIR/runtime-$(id -u)}"
export KWIN_WAYLAND_SOCKET="${KWIN_WAYLAND_SOCKET:-wayland-0}"

mkdir -p "$XDG_RUNTIME_DIR" "$TMPDIR/.X11-unix"
chmod 0700 "$XDG_RUNTIME_DIR" 2>/dev/null || true
chmod 1777 "$TMPDIR/.X11-unix" 2>/dev/null || true

SESSION_LOCK_DIR="$XDG_RUNTIME_DIR/start-plasma.lock"
SESSION_LOCK_PID_FILE="$SESSION_LOCK_DIR/pid"

release_session_lock()
{
    if [ -f "$SESSION_LOCK_PID_FILE" ] && [ "$(cat "$SESSION_LOCK_PID_FILE" 2>/dev/null || true)" = "$$" ]; then
        rm -f "$SESSION_LOCK_PID_FILE"
        rmdir "$SESSION_LOCK_DIR" 2>/dev/null || true
    fi
}

acquire_session_lock()
{
    local owner_pid

    if mkdir "$SESSION_LOCK_DIR" 2>/dev/null; then
        printf '%s\n' "$$" > "$SESSION_LOCK_PID_FILE"
        return 0
    fi

    owner_pid="$(cat "$SESSION_LOCK_PID_FILE" 2>/dev/null || true)"
    if [[ "$owner_pid" =~ ^[0-9]+$ ]] && kill -0 "$owner_pid" 2>/dev/null; then
        echo "Plasma is already starting or running (start-plasma pid $owner_pid)."
        return 1
    fi

    rm -f "$SESSION_LOCK_PID_FILE"
    if ! rmdir "$SESSION_LOCK_DIR" 2>/dev/null || ! mkdir "$SESSION_LOCK_DIR" 2>/dev/null; then
        echo "Error: cannot acquire session lock: $SESSION_LOCK_DIR" >&2
        return 2
    fi
    printf '%s\n' "$$" > "$SESSION_LOCK_PID_FILE"
}

if acquire_session_lock; then
    :
else
    LOCK_STATUS=$?
    exit "$LOCK_STATUS"
fi
trap release_session_lock EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if command -v pgrep >/dev/null 2>&1; then
    EXISTING_KWIN_PID="$(pgrep -x kwin_wayland | head -n 1 || true)"
    if [ -n "$EXISTING_KWIN_PID" ]; then
        echo "Plasma is already running (kwin_wayland pid $EXISTING_KWIN_PID)."
        exit 0
    fi
fi

KWIN_SOCKET_PATH="$XDG_RUNTIME_DIR/$KWIN_WAYLAND_SOCKET"
if [ -S "$KWIN_SOCKET_PATH" ]; then
    if command -v pgrep >/dev/null 2>&1 && pgrep -x kwin_wayland >/dev/null 2>&1; then
        echo "Plasma is already running on $KWIN_WAYLAND_SOCKET."
        exit 0
    fi
    echo "Removing stale Wayland socket: $KWIN_SOCKET_PATH"
    rm -f "$KWIN_SOCKET_PATH" "$KWIN_SOCKET_PATH.lock"
fi

PLASMA_LOG="${PLASMA_LOG:-$PREFIX/tmp/start-plasma.log}"
mkdir -p "$(dirname "$PLASMA_LOG")"
: > "$PLASMA_LOG"
exec 3>&1 4>&2
echo "Starting Plasma; log: $PLASMA_LOG" >&3
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
    RENDER_DEPS=(
        plasma-desktop plasma-workspace kactivitymanagerd
        konsole dolphin dbus mesa vulkan-tools
    )
    if [ -e "${KWIN_ANDROID_KGSL_DEVICE:-/dev/kgsl-3d0}" ]; then
        RENDER_DEPS+=(mesa-vulkan-icd-freedreno)
    fi
    pkg install "${RENDER_DEPS[@]}" -y
    echo "✓ Dependencies installed"
    echo ""
fi

# 环境变量设置
export XDG_CURRENT_DESKTOP="KDE"
export XDG_SESSION_TYPE="wayland"
export DESKTOP_SESSION="${DESKTOP_SESSION:-plasma}"
export KDE_FULL_SESSION="${KDE_FULL_SESSION:-true}"
export KDE_SESSION_VERSION="${KDE_SESSION_VERSION:-6}"
export QT_QPA_PLATFORM="wayland"
export KWIN_BACKEND="android"
export KWIN_ANDROID_DISABLE_INPUT="${KWIN_ANDROID_DISABLE_INPUT:-0}"
export KWIN_ANDROID_REFRESH_RATE="${KWIN_ANDROID_REFRESH_RATE:-30}"
export KWIN_ANDROID_BUFFER_TYPE="${KWIN_ANDROID_BUFFER_TYPE:-ahb}"
export KWIN_ANDROID_ENABLE_EGL="${KWIN_ANDROID_ENABLE_EGL:-1}"
export XDG_CONFIG_DIRS="${XDG_CONFIG_DIRS:-$PREFIX/etc/xdg}"
export XDG_DATA_DIRS="${XDG_DATA_DIRS:-$PREFIX/share}"
export XDG_MENU_PREFIX="${XDG_MENU_PREFIX:-plasma-}"
export XCURSOR_THEME="${XCURSOR_THEME:-breeze_cursors}"
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RENDERING_ENV="${KWIN_ANDROID_RENDERING_ENV:-$PREFIX/bin/kwin-android-rendering-env}"
if [ ! -f "$RENDERING_ENV" ]; then
    RENDERING_ENV="$SCRIPT_DIR/android-rendering-env.sh"
fi
if [ ! -f "$RENDERING_ENV" ]; then
    echo "Error: KWin Android rendering environment helper not found"
    exit 1
fi
# shellcheck source=android-rendering-env.sh
source "$RENDERING_ENV"
kwin_android_select_rendering

cleanup_session()
{
    local status=$?
    local pid

    trap - EXIT INT TERM
    for pid in \
        "${PLASMASHELL_PID:-}" \
        "${KACTIVITYMANAGERD_PID:-}" \
        "${KDED_PID:-}" \
        "${KDEINIT_PID:-}" \
        "${KWIN_PID:-}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    for pid in \
        "${PLASMASHELL_PID:-}" \
        "${KACTIVITYMANAGERD_PID:-}" \
        "${KDED_PID:-}" \
        "${KDEINIT_PID:-}" \
        "${KWIN_PID:-}"; do
        if [ -n "$pid" ]; then
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [ -n "${KWIN_ANDROID_AUX_PID:-}" ]; then
        kill "$KWIN_ANDROID_AUX_PID" 2>/dev/null || true
        wait "$KWIN_ANDROID_AUX_PID" 2>/dev/null || true
    fi
    if [ "${STARTED_DBUS:-0}" = "1" ] && [ -n "${DBUS_SESSION_BUS_PID:-}" ]; then
        kill "$DBUS_SESSION_BUS_PID" 2>/dev/null || true
    fi
    release_session_lock
    exit "$status"
}
trap cleanup_session EXIT

# 性能调优
export QSG_RENDER_LOOP=basic
export QT_XCB_GL_INTEGRATION=none

echo ""
echo "Environment setup complete:"
echo "  Acceleration: $ACCELERATION_MODE"
echo "  Mesa driver: ${MESA_LOADER_DRIVER_OVERRIDE:-default}"
echo "  Vulkan ICD: ${VK_DRIVER_FILES:-${VK_ICD_FILENAMES:-Android system loader}}"
echo ""

# 启动D-Bus
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    echo "Starting D-Bus..."
    if command -v dbus-launch >/dev/null 2>&1; then
        eval "$(dbus-launch --sh-syntax)"
        STARTED_DBUS=1
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
        KWIN_ANDROID_GL_MODE \
        KWIN_WAYLAND_SOCKET \
        GALLIUM_DRIVER \
        MESA_LOADER_DRIVER_OVERRIDE \
        QT_QPA_PLATFORM \
        VK_DRIVER_FILES \
        VK_ICD_FILENAMES \
        WRAPPER_VULKAN_PATH \
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
KWIN_READY=0
KWIN_STARTUP_TIMEOUT="${KWIN_ANDROID_STARTUP_TIMEOUT:-15}"
for ((second = 0; second < KWIN_STARTUP_TIMEOUT; ++second)); do
    if ! kill -0 "$KWIN_PID" 2>/dev/null; then
        echo "✗ Error: kwin_wayland exited during startup"
        echo "KWin startup failed; see $PLASMA_LOG" >&4
        wait "$KWIN_PID" || true
        exit 1
    fi
    if [ -S "$KWIN_SOCKET_PATH" ] && grep -q "Android EGL backend initialized" "$PLASMA_LOG"; then
        KWIN_READY=1
        break
    fi
    sleep 1
done
if [ "$KWIN_READY" != "1" ]; then
    echo "✗ Error: kwin_wayland did not initialize the Android EGL backend within ${KWIN_STARTUP_TIMEOUT}s"
    echo "KWin startup timed out; see $PLASMA_LOG" >&4
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
    KDEINIT_PID=$!
elif command -v kdeinit5 >/dev/null 2>&1; then
    kdeinit5 &
    KDEINIT_PID=$!
else
    echo "⚠ kdeinit not found, skipping"
fi
sleep 1

if command -v kded6 >/dev/null 2>&1; then
    kded6 &
    KDED_PID=$!
elif command -v kded5 >/dev/null 2>&1; then
    kded5 &
    KDED_PID=$!
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
echo "Plasma started successfully (KWin pid $KWIN_PID)." >&3

set +e
wait "$KWIN_PID"
KWIN_STATUS=$?
set -e
exit "$KWIN_STATUS"
