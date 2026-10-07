#!/data/data/com.termux/files/usr/bin/bash

# Start KWin's X11 backend through Mesa Zink and the native Android Vulkan
# wrapper. The WSI layer exports swapchain AHardwareBuffers to Termux:X11.
set -eu

export PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
export TMPDIR="${TMPDIR:-$PREFIX/tmp}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$TMPDIR/runtime-$(id -u)}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR" 2>/dev/null || true

RENDERING_ENV="${KWIN_ANDROID_RENDERING_ENV:-$PREFIX/bin/kwin-android-rendering-env}"
if [ ! -r "$RENDERING_ENV" ]; then
    RENDERING_ENV="$(cd "$(dirname "$0")" && pwd)/android-rendering-env.sh"
fi
if [ ! -r "$RENDERING_ENV" ]; then
    echo "Missing KWin Android rendering environment helper" >&2
    exit 1
fi

export KWIN_ANDROID_GL_MODE=zink
# shellcheck source=android-rendering-env.sh
. "$RENDERING_ENV"
kwin_android_select_rendering

if [ "${KWIN_ANDROID_GL_MODE}" != "zink" ]; then
    echo "Native Vulkan wrapper unavailable; refusing to start X11 KWin without Zink" >&2
    exit 1
fi

export TERMUX_VULKAN_WSI_LAYER=1
export LIBGL_KOPPER_DRI2=true
unset DISABLE_WSI_LAYER
export KWIN_COMPOSE="${KWIN_COMPOSE:-O2}"

exec kwin_x11 --replace "$@"
