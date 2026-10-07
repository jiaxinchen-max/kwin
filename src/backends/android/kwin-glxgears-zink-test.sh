#!/data/data/com.termux/files/usr/bin/bash

set -euo pipefail

PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
HOME="${HOME:-/data/data/com.termux/files/home}"
RUNTIME_ROOT="${KWIN_ANDROID_RUNTIME_ROOT:-$HOME/.termux/kwin-runtime}"
SESSION_ENV="${KWIN_ANDROID_SESSION_ENV:-$RUNTIME_ROOT/session.env}"
MESA_LIBDIR="${KWIN_GLX_ZINK_MESA_LIBDIR:-$PREFIX/lib}"
MESA_DRIVERS_DIR="${KWIN_GLX_ZINK_DRIVERS_DIR:-$MESA_LIBDIR/dri}"

usage()
{
    cat <<'EOF'
Usage: kwin-glxgears-zink-test [--info|--help] [glxgears arguments...]

Environment overrides:
  KWIN_GLX_ZINK_ICD          Vulkan ICD JSON used by the GLX client
  KWIN_GLX_ZINK_WSI_DEBUG    MESA_VK_WSI_DEBUG value (default: sw; none disables it)
  KWIN_GLX_ZINK_MESA_LIBDIR  Mesa/GLVND library directory
  KWIN_GLX_ZINK_DRIVERS_DIR  Mesa DRI driver directory
EOF
}

case "${1:-}" in
    --help|-h)
        usage
        exit 0
        ;;
    --info|-i)
        shift
        PROGRAM=glxinfo
        set -- -B "$@"
        ;;
    *)
        PROGRAM=glxgears
        ;;
esac

if [ ! -r "$SESSION_ENV" ]; then
    echo "KWin session environment not found: $SESSION_ENV" >&2
    echo "Start Plasma before running this test." >&2
    exit 1
fi

# Import only the display and runtime paths written by start-plasma.
# shellcheck disable=SC1090
source "$SESSION_ENV"

if [ -z "${DISPLAY:-}" ]; then
    echo "DISPLAY is missing from $SESSION_ENV" >&2
    exit 1
fi
if ! command -v "$PROGRAM" >/dev/null 2>&1; then
    echo "$PROGRAM is not installed; run: pkg install mesa-demos" >&2
    exit 1
fi
if [ ! -r "$MESA_LIBDIR/libGLX_mesa.so.0" ]; then
    echo "Mesa GLX library not found: $MESA_LIBDIR/libGLX_mesa.so.0" >&2
    exit 1
fi
if [ ! -r "$MESA_DRIVERS_DIR/zink_dri.so" ]; then
    echo "Mesa Zink DRI driver not found: $MESA_DRIVERS_DIR/zink_dri.so" >&2
    exit 1
fi

find_turnip_icd()
{
    local arch candidate

    arch="$(uname -m)"
    for candidate in \
        "${KWIN_GLX_ZINK_ICD:-}" \
        "${KWIN_ANDROID_TURNIP_ICD:-}" \
        "$PREFIX/share/vulkan/icd.d/freedreno_icd.${arch}.json" \
        "$PREFIX/share/vulkan/icd.d/freedreno_icd.aarch64.json" \
        "$PREFIX/share/vulkan/icd.d/freedreno_icd.arm64.json"; do
        if [ -n "$candidate" ] && [ -r "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

ICD="${KWIN_GLX_ZINK_ICD:-${VK_DRIVER_FILES:-${VK_ICD_FILENAMES:-}}}"
if [ -z "$ICD" ]; then
    ICD="$(find_turnip_icd || true)"
fi
if [ -z "$ICD" ]; then
    echo "No Vulkan ICD selected and no Turnip ICD was found" >&2
    echo "Set KWIN_GLX_ZINK_ICD=/path/to/icd.json" >&2
    exit 1
fi

# Force the client-side GLVND/Mesa GLX path. Prepending Mesa's library
# directory prevents a caller's gl4es LD_LIBRARY_PATH from taking precedence.
export LD_LIBRARY_PATH="$MESA_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBGL_DRIVERS_PATH="$MESA_DRIVERS_DIR"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export MESA_LOADER_DRIVER_OVERRIDE=zink
export GALLIUM_DRIVER=zink
export TERMUX_ANDROID_ZINK=1
export VK_DRIVER_FILES="$ICD"
export VK_ICD_FILENAMES="$ICD"
unset LIBGL_FB LIBGL_ALWAYS_SOFTWARE

WSI_DEBUG="${KWIN_GLX_ZINK_WSI_DEBUG:-sw}"
case "$WSI_DEBUG" in
    none|off|direct)
        unset MESA_VK_WSI_DEBUG
        WSI_DEBUG=disabled
        ;;
    *)
        export MESA_VK_WSI_DEBUG="$WSI_DEBUG"
        ;;
esac

echo "DISPLAY=$DISPLAY"
echo "GLX vendor: Mesa"
echo "Gallium driver: zink"
echo "Vulkan ICD: $ICD"
echo "MESA_VK_WSI_DEBUG: $WSI_DEBUG"
exec "$PROGRAM" "$@"
