#!/data/data/com.termux/files/usr/bin/bash

set -euo pipefail

PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
HOME="${HOME:-/data/data/com.termux/files/home}"
RUNTIME_ROOT="${KWIN_ANDROID_RUNTIME_ROOT:-$HOME/.termux/kwin-runtime}"
SESSION_ENV="${KWIN_ANDROID_SESSION_ENV:-$RUNTIME_ROOT/session.env}"
GL4ES_LIBDIR="${KWIN_GL4ES_LIBDIR:-$PREFIX/lib/gl4es}"
GL4ES_FB_MODE="${KWIN_XWAYLAND_GL4ES_FB:-3}"

usage()
{
    echo "Usage: kwin-glxgears-test [--info|--help] [glxgears arguments...]"
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

# shellcheck disable=SC1090
source "$SESSION_ENV"

if [ -z "${DISPLAY:-}" ]; then
    echo "DISPLAY is missing from $SESSION_ENV" >&2
    exit 1
fi
if [ ! -d "$GL4ES_LIBDIR" ]; then
    echo "gl4es library directory not found: $GL4ES_LIBDIR" >&2
    exit 1
fi
if ! command -v "$PROGRAM" >/dev/null 2>&1; then
    echo "$PROGRAM is not installed; run: pkg install mesa-demos" >&2
    exit 1
fi

export LIBGL_FB="$GL4ES_FB_MODE"
export LD_LIBRARY_PATH="$GL4ES_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

echo "DISPLAY=$DISPLAY"
echo "LIBGL_FB=$LIBGL_FB"
exec "$PROGRAM" "$@"
