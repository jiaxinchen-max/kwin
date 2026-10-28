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
#   TURNIP_TARBALL_URL              Turnip (Adreno Vulkan) tarball to unpack into the container
#                                   when its ICD is missing (default: the latest turnip_* asset
#                                   for the container's distro from lfdevs/mesa-for-android-container)
#   PLASMA_CONTAINER_ROOTFS         the container's rootfs directory (default: auto-detected)
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
    echo "proot-distro not found; installing it..."
    if ! pkg install -y proot-distro; then
        echo "Error: failed to install proot-distro." >&2
        exit 1
    fi
}

# Ensure the target distro is installed (a bare install still needs the Plasma
# packages + mesa tarball; the container session script handles/checks those).
if ! proot-distro login "$DISTRO" -- true >/dev/null 2>&1; then
    echo "Container distro '$DISTRO' is not installed; installing it..."
    if ! proot-distro install "$DISTRO"; then
        echo "Error: failed to install proot-distro distro '$DISTRO'." >&2
        exit 1
    fi
fi

# The Turnip Vulkan ICD is not an apt package (Debian's Turnip has no KGSL backend); the
# session script aborts without it. Provision it from the host, where curl and any proxy
# configuration (~/.curlrc, https_proxy) already work.
ICD_REL="usr/share/vulkan/icd.d/freedreno_icd.aarch64.json"
TURNIP_REPO="lfdevs/mesa-for-android-container"
TURNIP_FALLBACK_TAG="turnip-27.0.0-devel-20261025"

container_rootfs() {
    if [ -n "${PLASMA_CONTAINER_ROOTFS:-}" ]; then printf '%s\n' "$PLASMA_CONTAINER_ROOTFS"; return; fi
    for c in "$PREFIX/var/lib/proot-distro/containers/$DISTRO/rootfs" \
             "$PREFIX/var/lib/proot-distro/installed-rootfs/$DISTRO"; do
        [ -d "$c/usr" ] && { printf '%s\n' "$c"; return; }
    done
    return 1
}

turnip_asset_flavour() {
    # <id>_<codename>, e.g. debian_trixie, matching the release asset names.
    rootfs="$1"
    id=$(sed -n 's/^ID=//p' "$rootfs/etc/os-release" 2>/dev/null | tr -d '"' | head -n1)
    codename=$(sed -n 's/^VERSION_CODENAME=//p' "$rootfs/etc/os-release" 2>/dev/null | tr -d '"' | head -n1)
    [ -n "$id" ] && [ -n "$codename" ] || return 1
    printf '%s_%s\n' "$id" "$codename"
}

ensure_turnip_icd() {
    rootfs=$(container_rootfs) || {
        echo "Warning: cannot locate the rootfs of '$DISTRO'; skipping the Turnip ICD check." >&2
        return 0
    }
    [ -r "$rootfs/$ICD_REL" ] && [ -e "$rootfs/usr/lib/aarch64-linux-gnu/libvulkan_freedreno.so" ] && return 0

    url="${TURNIP_TARBALL_URL:-}"
    if [ -z "$url" ]; then
        flavour=$(turnip_asset_flavour "$rootfs") || {
            echo "Error: cannot tell which distro '$DISTRO' is; set TURNIP_TARBALL_URL." >&2
            return 1
        }
        tag=$(curl -fsL -m 30 "https://api.github.com/repos/$TURNIP_REPO/releases?per_page=20" 2>/dev/null \
              | sed -n 's/.*"tag_name": *"\(turnip-[^"]*\)".*/\1/p' | head -n1)
        tag="${tag:-$TURNIP_FALLBACK_TAG}"
        url="https://github.com/$TURNIP_REPO/releases/download/$tag/turnip_${tag#turnip-}_${flavour}_arm64.tar.gz"
    fi

    echo "Installing the Turnip Vulkan ICD into the container: $url"
    tmp="${TMPDIR:-$PREFIX/tmp}/turnip.$$.tar.gz"
    if ! curl -fL -m 300 -o "$tmp" "$url"; then
        rm -f "$tmp"
        echo "Error: failed to download $url (set https_proxy or ~/.curlrc if GitHub is unreachable)." >&2
        return 1
    fi
    if ! tar -xzf "$tmp" -C "$rootfs"; then
        rm -f "$tmp"
        echo "Error: failed to unpack the Turnip tarball into $rootfs." >&2
        return 1
    fi
    rm -f "$tmp"
    [ -r "$rootfs/$ICD_REL" ] || {
        echo "Error: the Turnip tarball did not provide /$ICD_REL." >&2
        return 1
    }
}
ensure_turnip_icd || exit 1

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
