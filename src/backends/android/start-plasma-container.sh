#!/data/data/com.termux/files/usr/bin/bash
# One-command launcher for the GPU-accelerated container Plasma desktop.
#
# Starts the native compositor (kwin_wayland via `start-plasma --compositor-only`)
# and then launches the full Plasma session inside a proot/glibc container, which
# renders on the GPU via Qt's Vulkan RHI. See packaging/container/README.md for
# the architecture and the one-time container setup.
#
# Env overrides:
#   KWIN_ANDROID_GL_MODE            compositor GL mode (default: zink)
#   PLASMA_CONTAINER_DISTRO         proot-distro name (default: debian)
#   PLASMA_CONTAINER_SESSION_SCRIPT path to the in-container session launcher
#                                   (default: the installed plasma-container-session.sh)
set -u

PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
HOME="${HOME:-/data/data/com.termux/files/home}"

# Must match start-plasma's runtime-dir computation so we bind the same sockets.
KWIN_ANDROID_RUNTIME_ROOT="${KWIN_ANDROID_RUNTIME_ROOT:-$HOME/.termux/kwin-runtime}"
XDG_RUNTIME_DIR="${KWIN_ANDROID_XDG_RUNTIME_DIR:-$KWIN_ANDROID_RUNTIME_ROOT/runtime-$(id -u)}"
WAYLAND_SOCKET="${KWIN_WAYLAND_SOCKET:-wayland-0}"

DISTRO="${PLASMA_CONTAINER_DISTRO:-debian}"
GL_MODE="${KWIN_ANDROID_GL_MODE:-zink}"

# Locate the in-container session launcher (bind-mounted into the container).
SESSION_SCRIPT="${PLASMA_CONTAINER_SESSION_SCRIPT:-}"
if [ -z "$SESSION_SCRIPT" ]; then
    for c in \
        "$PREFIX/share/kwin/android/plasma-container-session.sh" \
        "$(dirname "$0")/../share/kwin/android/plasma-container-session.sh" \
        "$HOME/plasma-container-session.sh"; do
        if [ -r "$c" ]; then SESSION_SCRIPT="$c"; break; fi
    done
fi
if [ -z "$SESSION_SCRIPT" ] || [ ! -r "$SESSION_SCRIPT" ]; then
    echo "Error: container session script not found." >&2
    echo "  Set PLASMA_CONTAINER_SESSION_SCRIPT=/path/to/plasma-container-session.sh" >&2
    exit 1
fi

command -v proot-distro >/dev/null 2>&1 || {
    echo "Error: proot-distro not found (pkg install proot-distro)." >&2
    exit 1
}

# 1) Native compositor (reuse an existing one if already up).
if pgrep -x kwin_wayland >/dev/null 2>&1; then
    echo "kwin_wayland already running; reusing it."
else
    echo "Starting native compositor (start-plasma --compositor-only, GL=$GL_MODE)..."
    setsid env KWIN_ANDROID_GL_MODE="$GL_MODE" start-plasma --compositor-only \
        </dev/null >/dev/null 2>&1 &
fi

# 2) Wait for the Wayland socket to come up.
SOCK="$XDG_RUNTIME_DIR/$WAYLAND_SOCKET"
for ((i = 0; i < 30; ++i)); do
    [ -S "$SOCK" ] && break
    sleep 1
done
if [ ! -S "$SOCK" ]; then
    echo "Error: Wayland socket $SOCK did not appear within 30s." >&2
    echo "  Check the compositor log: $KWIN_ANDROID_RUNTIME_ROOT/start-plasma.log" >&2
    exit 1
fi
echo "Compositor ready: $SOCK"

# 3) Launch the Plasma session inside the container, sharing the runtime dir.
echo "Starting container Plasma session ($DISTRO, Vulkan RHI)..."
exec proot-distro login "$DISTRO" \
    --bind "$XDG_RUNTIME_DIR:/run/wl" \
    --bind "$SESSION_SCRIPT:/tmp/plasma-container-session.sh" \
    -- sh /tmp/plasma-container-session.sh
