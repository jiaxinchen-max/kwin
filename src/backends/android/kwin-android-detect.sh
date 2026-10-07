#!/data/data/com.termux/files/usr/bin/bash
# Probe which KWin Android rendering modes the current device supports.
#
# This is the diagnostic counterpart to kwin-android-rendering-env. The main
# KWin process no longer auto-detects a renderer; it consumes one resolved mode.
# This tool keeps the old auto/probe knowledge available on demand: it checks
# each mode independently and reports what works, without starting KWin and
# without mutating the caller's environment.
#
# Usage:
#   kwin-android-detect              Print a report of all modes + recommendation
#   kwin-android-detect --recommend  Print only the recommended mode token
#   kwin-android-detect --command    Print a ready-to-run start-plasma command
#   kwin-android-detect --explain-zink  Explain why Zink is or is not usable
#   kwin-android-detect --verbose    Also print the underlying probe output
#   kwin-android-detect --help
#
# Exit status: 0 if at least one hardware mode is supported, 1 if only software
# (llvmpipe) is available, 2 on setup error.

set -u

PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
HOME="${HOME:-/data/data/com.termux/files/home}"

MODE_OUTPUT=report
VERBOSE=0
for arg in "$@"; do
    case "$arg" in
        --recommend) MODE_OUTPUT=recommend ;;
        --command)   MODE_OUTPUT=command ;;
        --explain-zink) MODE_OUTPUT=explain ;;
        --verbose|-v) VERBOSE=1 ;;
        --help|-h)
            # Print the leading comment block (skip the shebang).
            awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"
            exit 0
            ;;
        *)
            echo "kwin-android-detect: unknown argument: $arg" >&2
            exit 2
            ;;
    esac
done

# Reuse the probe helpers instead of duplicating them.
RENDERING_ENV="${KWIN_ANDROID_RENDERING_ENV:-$PREFIX/bin/kwin-android-rendering-env}"
if [ ! -r "$RENDERING_ENV" ]; then
    RENDERING_ENV="$(cd "$(dirname "$0")" && pwd)/android-rendering-env.sh"
fi
if [ ! -r "$RENDERING_ENV" ]; then
    echo "kwin-android-detect: cannot find kwin-android-rendering-env" >&2
    exit 2
fi
# shellcheck source=android-rendering-env.sh
. "$RENDERING_ENV"

arch="$(uname -m)"
case "$arch" in
    aarch64|arm64)
        android_egl="${KWIN_ANDROID_SYSTEM_EGL_LIBRARY:-/system/lib64/libEGL.so}"
        android_gles="${KWIN_ANDROID_SYSTEM_GLES_LIBRARY:-/system/lib64/libGLESv2.so}"
        android_vulkan="${KWIN_ANDROID_SYSTEM_VULKAN_LIBRARY:-/system/lib64/libvulkan.so}"
        ;;
    *)
        android_egl="${KWIN_ANDROID_SYSTEM_EGL_LIBRARY:-/system/lib/libEGL.so}"
        android_gles="${KWIN_ANDROID_SYSTEM_GLES_LIBRARY:-/system/lib/libGLESv2.so}"
        android_vulkan="${KWIN_ANDROID_SYSTEM_VULKAN_LIBRARY:-/system/lib/libvulkan.so}"
        ;;
esac
wrapper_icd="${KWIN_ANDROID_WRAPPER_ICD:-$PREFIX/share/vulkan/icd.d/wrapper_icd.${arch}.json}"
wrapper_lib="${KWIN_ANDROID_WRAPPER_LIBRARY:-$PREFIX/lib/libvulkan_wrapper.so}"
freedreno_lib="$PREFIX/lib/libvulkan_freedreno.so"
kgsl_device="${KWIN_ANDROID_KGSL_DEVICE:-/dev/kgsl-3d0}"
mesa_gl="$PREFIX/lib/libGL.so.1"
turnip_icd="$(kwin_android_find_turnip_icd "$arch" 2>/dev/null || true)"
requested_icd="${VK_DRIVER_FILES:-${VK_ICD_FILENAMES:-}}"
case "$requested_icd" in
    *vortek*|*/glibc/*) requested_icd="" ;;  # not loadable in a Bionic process
esac

# Probe results, filled below.
SUPPORT_SYSTEM=no;   DETAIL_SYSTEM=""
SUPPORT_ZINK=no;     DETAIL_ZINK="";   ZINK_MODE=""     # turnip|wrapper|icd
SUPPORT_LLVMPIPE=no; DETAIL_LLVMPIPE=""

probe_note() {
    [ "$VERBOSE" = "1" ] && printf '    %s\n' "$1" >&2
    return 0
}

# --- system: Android vendor EGL/GLES (hardware on real devices and emulators) -
if [ -r "$android_egl" ] && [ -r "$android_gles" ]; then
    SUPPORT_SYSTEM=yes
    DETAIL_SYSTEM="$android_egl"
else
    DETAIL_SYSTEM="missing $android_egl or $android_gles"
fi

# --- zink: Mesa on a Bionic Vulkan ICD ---------------------------------------
# Priority mirrors kwin_android_select_rendering: explicit ICD, then Turnip/KGSL,
# then the Android Vulkan wrapper.
if [ -n "$requested_icd" ]; then
    if kwin_android_probe_zink_icd "$requested_icd" "" ''; then
        SUPPORT_ZINK=yes; ZINK_MODE=icd
        DETAIL_ZINK="explicit ICD $requested_icd"
    else
        DETAIL_ZINK="explicit ICD $requested_icd: zink probe failed"
    fi
fi

if [ "$SUPPORT_ZINK" = "no" ] && [ -n "$turnip_icd" ]; then
    if [ -r "$freedreno_lib" ] && [ -r "$kgsl_device" ] && [ -w "$kgsl_device" ]; then
        if kwin_android_probe_zink_icd "$turnip_icd" "" 'Turnip|Adreno'; then
            SUPPORT_ZINK=yes; ZINK_MODE=turnip
            DETAIL_ZINK="Turnip/KGSL ($turnip_icd)"
        else
            DETAIL_ZINK="Turnip files present, zink probe failed"
        fi
    else
        DETAIL_ZINK="Turnip ICD present, but $kgsl_device is not read/write"
    fi
fi

if [ "$SUPPORT_ZINK" = "no" ] && [ -r "$wrapper_icd" ] && [ -r "$wrapper_lib" ] && [ -r "$android_vulkan" ]; then
    if kwin_android_probe_zink_icd "$wrapper_icd" "$android_vulkan" ''; then
        SUPPORT_ZINK=yes; ZINK_MODE=wrapper
        DETAIL_ZINK="Android Vulkan wrapper ($wrapper_icd)"
    else
        DETAIL_ZINK="Android Vulkan wrapper present, zink probe failed"
    fi
fi

if [ "$SUPPORT_ZINK" = "no" ] && [ -z "$DETAIL_ZINK" ]; then
    DETAIL_ZINK="no usable Bionic Vulkan ICD (no explicit ICD, Turnip/KGSL, or wrapper)"
fi

# --- llvmpipe: Mesa software rendering ---------------------------------------
if [ -r "$mesa_gl" ] || [ -r "$PREFIX/lib/libGL.so" ]; then
    SUPPORT_LLVMPIPE=yes
    DETAIL_LLVMPIPE="Mesa present ($mesa_gl)"
else
    DETAIL_LLVMPIPE="Mesa libGL not found; install the mesa package"
fi

# --- recommendation: best hardware path, else software ------------------------
# zink (hardware, GPU) > system (hardware, vendor GLES) > llvmpipe (software).
RECOMMEND=""
RECOMMEND_TOKEN=""
if [ "$SUPPORT_ZINK" = "yes" ]; then
    RECOMMEND=zink
    case "$ZINK_MODE" in
        turnip) RECOMMEND_TOKEN=turnip ;;
        wrapper) RECOMMEND_TOKEN=wrapper ;;
        *) RECOMMEND_TOKEN=zink ;;
    esac
elif [ "$SUPPORT_SYSTEM" = "yes" ]; then
    RECOMMEND=system; RECOMMEND_TOKEN=system
elif [ "$SUPPORT_LLVMPIPE" = "yes" ]; then
    RECOMMEND=llvmpipe; RECOMMEND_TOKEN=llvmpipe
fi

build_command() {
    local token="$1"
    if [ "$token" = "zink" ] && [ "$ZINK_MODE" = "icd" ]; then
        printf 'KWIN_ANDROID_GL_MODE=zink VK_DRIVER_FILES=%s start-plasma\n' "$requested_icd"
    else
        printf 'KWIN_ANDROID_GL_MODE=%s start-plasma\n' "$token"
    fi
}

# --- Vulkan-level Zink explanation (why zink works or not) --------------------
# This reproduces, in shell, the check the removed C++ probe did: Zink needs a
# hardware (non-CPU) Vulkan device that exposes VK_EXT_robustness2.nullDescriptor.
# vulkaninfo dumps each device under a "GPUn:" header, so nullDescriptor is
# attributed to the device whose section it falls in.
_vk_parse() {
    awk '
    /^GPU[0-9]+:/ { g=$0; sub(/:.*/,"",g); if(!(g in seen)){seen[g]=1; order[++n]=g} next }
    g!="" && /^[ \t]*deviceType[ \t]*=/ && type[g]=="" { t=$0; sub(/.*=[ \t]*/,"",t); type[g]=t }
    g!="" && /^[ \t]*deviceName[ \t]*=/ && name[g]=="" { t=$0; sub(/.*=[ \t]*/,"",t); name[g]=t }
    g!="" && /nullDescriptor[ \t]*=[ \t]*true/ { nd[g]="yes" }
    END { for(i=1;i<=n;i++){ g=order[i]; printf "%s|%s|%s|%s\n", g, type[g], (nd[g]==""?"no":"yes"), name[g] } }
    '
}

_run_vulkaninfo() {  # $1 icd  $2 wrapper_path
    (
        [ -n "$1" ] && export VK_DRIVER_FILES="$1" VK_ICD_FILENAMES="$1"
        [ -n "$2" ] && export WRAPPER_VULKAN_PATH="$2"
        exec timeout "${KWIN_ANDROID_PROBE_TIMEOUT:-15}" vulkaninfo 2>/dev/null
    )
}

VK_ANY=no; VK_HW=no; VK_HWND=no
_explain_one() {  # $1 label  $2 icd  $3 wrapper_path  $4 record_verdict(1/0)
    local label="$1" out rc parsed any=no hw=no hwnd=no t n name g
    printf 'Probe: %s\n' "$label"
    out="$(_run_vulkaninfo "$2" "$3")"; rc=$?
    if [ -z "$out" ]; then
        printf '  vulkaninfo enumerated nothing (rc=%s) - this ICD likely aborts or hangs in-process\n\n' "$rc"
        return 0
    fi
    parsed="$(printf '%s\n' "$out" | _vk_parse)"
    if [ -z "$parsed" ]; then
        printf '  no Vulkan devices reported (rc=%s)\n\n' "$rc"
        return 0
    fi
    printf '  %-6s %-16s %-16s %s\n' GPU TYPE NULLDESC NAME
    while IFS='|' read -r g t n name; do
        [ -n "$g" ] || continue
        any=yes
        case "$t" in
            *_CPU) : ;;
            *) hw=yes; [ "$n" = yes ] && hwnd=yes ;;
        esac
        printf '  %-6s %-16s %-16s %s\n' "$g" "${t#PHYSICAL_DEVICE_TYPE_}" "nullDesc=$n" "$name"
    done <<EOF
$parsed
EOF
    if [ "$4" = 1 ]; then
        VK_ANY=$any; VK_HW=$hw; VK_HWND=$hwnd
    fi
    printf '\n'
}

explain_zink() {
    if ! command -v vulkaninfo >/dev/null 2>&1; then
        echo "vulkaninfo not found; install the vulkan-tools package to explain Zink readiness." >&2
        return 2
    fi
    echo "Zink readiness explanation (Vulkan level + end-to-end)"
    echo

    # 1) Everything the default loader exposes. This is the authoritative device
    #    inventory and drives the diagnosis below.
    _explain_one "default loader (all ICDs)" "" "" 1

    # 2) The ICD that zink mode would actually select, to surface aborts/hangs.
    if [ -n "$requested_icd" ]; then
        _explain_one "explicit ICD: $requested_icd" "$requested_icd" "" 0
    elif [ -n "$turnip_icd" ] && [ -r "$freedreno_lib" ] && [ -r "$kgsl_device" ] && [ -w "$kgsl_device" ]; then
        _explain_one "Turnip/KGSL: $turnip_icd" "$turnip_icd" "" 0
    elif [ -r "$wrapper_icd" ] && [ -r "$wrapper_lib" ] && [ -r "$android_vulkan" ]; then
        _explain_one "Android Vulkan wrapper: $wrapper_icd" "$wrapper_icd" "$android_vulkan" 0
    fi

    echo "Diagnosis:"
    if [ "$VK_ANY" = no ]; then
        echo "  - The default loader enumerated no Vulkan device. The loader or every ICD is broken."
    elif [ "$VK_HW" = no ]; then
        echo "  - Only software Vulkan (CPU/llvmpipe) is present; there is no hardware GPU for Zink."
    elif [ "$VK_HWND" = no ]; then
        echo "  - A hardware GPU exists but does NOT expose VK_EXT_robustness2.nullDescriptor."
        echo "    Zink requires that feature, so hardware Zink is unavailable on this device."
    else
        echo "  - A hardware GPU exposing nullDescriptor exists: Vulkan meets Zink's requirement."
    fi
    if [ "$SUPPORT_ZINK" = yes ]; then
        echo "  - End-to-end eglinfo Zink probe: PASS ($DETAIL_ZINK)."
    else
        echo "  - End-to-end eglinfo Zink probe: FAIL ($DETAIL_ZINK)."
        if [ "$VK_HWND" = yes ]; then
            echo "    Vulkan looks capable, so the blocker is Mesa/Zink/WSI or ICD selection, not the GPU."
            echo "    Try a specific ICD: VK_DRIVER_FILES=<icd.json> kwin-android-detect --explain-zink"
        fi
    fi
}

case "$MODE_OUTPUT" in
    recommend)
        [ -n "$RECOMMEND_TOKEN" ] || { echo "none" ; exit 1; }
        echo "$RECOMMEND_TOKEN"
        ;;
    command)
        [ -n "$RECOMMEND_TOKEN" ] || { echo "# no supported renderer found" >&2; exit 1; }
        build_command "$RECOMMEND_TOKEN"
        ;;
    explain)
        explain_zink
        ;;
    report)
        printf 'KWin Android renderer probe (arch: %s)\n\n' "$arch"
        printf '%-10s %-10s %s\n' "MODE" "SUPPORTED" "DETAIL"
        printf '%-10s %-10s %s\n' "----" "---------" "------"
        printf '%-10s %-10s %s\n' "system"   "$SUPPORT_SYSTEM"   "$DETAIL_SYSTEM"
        printf '%-10s %-10s %s\n' "zink"     "$SUPPORT_ZINK"     "$DETAIL_ZINK"
        printf '%-10s %-10s %s\n' "llvmpipe" "$SUPPORT_LLVMPIPE" "$DETAIL_LLVMPIPE"
        echo
        if [ "$SUPPORT_ZINK" = no ]; then
            echo "(run 'kwin-android-detect --explain-zink' to see why zink is unavailable)"
            echo
        fi
        if [ -n "$RECOMMEND_TOKEN" ]; then
            printf 'Recommended: %s\n' "$RECOMMEND_TOKEN"
            printf 'Launch with:\n  %s\n' "$(build_command "$RECOMMEND_TOKEN")"
        else
            printf 'No renderer is available. Install mesa for software rendering.\n'
        fi
        ;;
esac

# Exit status: 0 hardware available, 1 software only, 2 nothing.
if [ "$SUPPORT_ZINK" = "yes" ] || [ "$SUPPORT_SYSTEM" = "yes" ]; then
    exit 0
elif [ "$SUPPORT_LLVMPIPE" = "yes" ]; then
    exit 1
else
    exit 2
fi
