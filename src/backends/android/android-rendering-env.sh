#!/data/data/com.termux/files/usr/bin/bash

# Select a renderer for the native Termux/Bionic KWin build. This file is
# sourced by start-plasma and debug-kwin; it intentionally does not enable the
# glibc Vortek broker path.

kwin_android_use_system_gles()
{
    export KWIN_ANDROID_GL_MODE=system
    export KWIN_COMPOSE=O2ES
    unset TERMUX_ANDROID_ZINK TERMUX_VULKAN_BROKER_SOCKET
    unset MESA_LOADER_DRIVER_OVERRIDE GALLIUM_DRIVER LIBGL_ALWAYS_SOFTWARE
    unset VK_DRIVER_FILES VK_ICD_FILENAMES WRAPPER_VULKAN_PATH MESA_VK_WSI_DEBUG
    unset MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE
}

kwin_android_use_zink()
{
    export KWIN_ANDROID_GL_MODE=zink
    export KWIN_COMPOSE=O2
    export TERMUX_ANDROID_ZINK=1
    export MESA_LOADER_DRIVER_OVERRIDE=zink
    export GALLIUM_DRIVER=zink
    unset LIBGL_ALWAYS_SOFTWARE
}

kwin_android_use_llvmpipe()
{
    export KWIN_ANDROID_GL_MODE=llvmpipe
    export KWIN_COMPOSE=O2
    export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
    export GALLIUM_DRIVER=llvmpipe
    export LIBGL_ALWAYS_SOFTWARE=1
    unset TERMUX_ANDROID_ZINK TERMUX_VULKAN_BROKER_SOCKET
    unset VK_DRIVER_FILES VK_ICD_FILENAMES WRAPPER_VULKAN_PATH MESA_VK_WSI_DEBUG
}

kwin_android_find_turnip_icd()
{
    local arch candidate

    arch="${1:-$(uname -m)}"
    for candidate in \
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

kwin_android_probe_zink_icd()
{
    local icd="$1"
    local wrapper_path="${2:-}"
    local expected_renderer="${3:-}"
    local output
    local -a probe_command

    if [ "${KWIN_ANDROID_DISABLE_RENDERER_PROBE:-0}" = "1" ]; then
        return 0
    fi

    if command -v eglinfo >/dev/null 2>&1; then
        probe_command=(eglinfo -p surfaceless -B)
        if command -v timeout >/dev/null 2>&1; then
            probe_command=(timeout "${KWIN_ANDROID_PROBE_TIMEOUT:-10}" "${probe_command[@]}")
        fi
        output="$(
            env -u LIBGL_ALWAYS_SOFTWARE \
                EGL_PLATFORM=surfaceless \
                MESA_LOADER_DRIVER_OVERRIDE=zink \
                GALLIUM_DRIVER=zink \
                TERMUX_ANDROID_ZINK=1 \
                VK_DRIVER_FILES="$icd" \
                VK_ICD_FILENAMES="$icd" \
                WRAPPER_VULKAN_PATH="$wrapper_path" \
                "${probe_command[@]}" 2>&1
        )" || return 1
        printf '%s\n' "$output" | grep -q 'renderer: zink' || return 1
        if printf '%s\n' "$output" | grep -Eqi 'llvmpipe|lavapipe|software rasterizer'; then
            return 1
        fi
        if [ -n "$expected_renderer" ]; then
            printf '%s\n' "$output" | grep -Eqi "$expected_renderer" || return 1
        fi
        return 0
    fi

    if command -v vulkaninfo >/dev/null 2>&1; then
        probe_command=(vulkaninfo --summary)
        if command -v timeout >/dev/null 2>&1; then
            probe_command=(timeout "${KWIN_ANDROID_PROBE_TIMEOUT:-10}" "${probe_command[@]}")
        fi
        output="$(
            VK_DRIVER_FILES="$icd" \
            VK_ICD_FILENAMES="$icd" \
            WRAPPER_VULKAN_PATH="$wrapper_path" \
                "${probe_command[@]}" 2>&1
        )" || return 1
        printf '%s\n' "$output" | grep -Eq 'PHYSICAL_DEVICE_TYPE_(INTEGRATED|DISCRETE|VIRTUAL)_GPU' || return 1
        if [ -n "$expected_renderer" ]; then
            printf '%s\n' "$output" | grep -Eqi "$expected_renderer" || return 1
        fi
    fi

    return 0
}

kwin_android_configure_icd_zink()
{
    local icd="$1"
    local description="$2"

    kwin_android_use_zink
    export VK_DRIVER_FILES="$icd"
    export VK_ICD_FILENAMES="$icd"
    unset WRAPPER_VULKAN_PATH MESA_VK_WSI_DEBUG TERMUX_VULKAN_BROKER_SOCKET
    ACCELERATION_MODE="$description"
    export ACCELERATION_MODE
}

kwin_android_configure_wrapper_zink()
{
    local wrapper_icd="$1"
    local android_vulkan="$2"

    kwin_android_use_zink
    export VK_DRIVER_FILES="$wrapper_icd"
    export VK_ICD_FILENAMES="$wrapper_icd"
    export WRAPPER_VULKAN_PATH="$android_vulkan"
    export MESA_VK_WSI_DEBUG="${MESA_VK_WSI_DEBUG:-blit}"
    unset TERMUX_VULKAN_BROKER_SOCKET
    ACCELERATION_MODE="Android Vulkan wrapper (Zink)"
    export ACCELERATION_MODE
}

kwin_android_select_rendering()
{
    local arch mode wrapper_icd wrapper_lib android_vulkan android_egl android_gles
    local loader_path requested_icd turnip_icd kgsl_device

    arch="$(uname -m)"
    mode="${KWIN_ANDROID_GL_MODE:-auto}"
    mode="$(printf '%s' "$mode" | tr '[:upper:]' '[:lower:]')"
    wrapper_icd="${KWIN_ANDROID_WRAPPER_ICD:-$PREFIX/share/vulkan/icd.d/wrapper_icd.${arch}.json}"
    wrapper_lib="${KWIN_ANDROID_WRAPPER_LIBRARY:-$PREFIX/lib/libvulkan_wrapper.so}"
    kgsl_device="${KWIN_ANDROID_KGSL_DEVICE:-/dev/kgsl-3d0}"
    if [ "$arch" = "aarch64" ] || [ "$arch" = "arm64" ]; then
        android_vulkan="${KWIN_ANDROID_SYSTEM_VULKAN_LIBRARY:-/system/lib64/libvulkan.so}"
        android_egl="${KWIN_ANDROID_SYSTEM_EGL_LIBRARY:-/system/lib64/libEGL.so}"
        android_gles="${KWIN_ANDROID_SYSTEM_GLES_LIBRARY:-/system/lib64/libGLESv2.so}"
    else
        android_vulkan="${KWIN_ANDROID_SYSTEM_VULKAN_LIBRARY:-/system/lib/libvulkan.so}"
        android_egl="${KWIN_ANDROID_SYSTEM_EGL_LIBRARY:-/system/lib/libEGL.so}"
        android_gles="${KWIN_ANDROID_SYSTEM_GLES_LIBRARY:-/system/lib/libGLESv2.so}"
    fi

    requested_icd="${VK_DRIVER_FILES:-${VK_ICD_FILENAMES:-}}"
    case "$requested_icd" in
        *vortek*|*/glibc/*)
            echo "Ignoring non-Bionic Vulkan ICD: $requested_icd"
            unset VK_DRIVER_FILES VK_ICD_FILENAMES TERMUX_VULKAN_BROKER_SOCKET
            requested_icd=
            ;;
    esac

    case "$mode" in
        system|system-gles|gles)
            kwin_android_use_system_gles
            ACCELERATION_MODE="Android system EGL/GLES (forced)"
            export ACCELERATION_MODE
            return 0
            ;;
        llvmpipe|software)
            kwin_android_use_llvmpipe
            ACCELERATION_MODE="Mesa Software (llvmpipe, forced)"
            export ACCELERATION_MODE
            return 0
            ;;
        auto|zink|hardware|turnip|kgsl|wrapper)
            ;;
        *)
            echo "Unsupported KWIN_ANDROID_GL_MODE: $mode" >&2
            return 2
            ;;
    esac

    # An explicit native Bionic ICD is the most specific Vulkan override.
    if [ -n "$requested_icd" ]; then
        kwin_android_configure_icd_zink "$requested_icd" "Explicit native Vulkan ICD (Zink)"
        return 0
    fi

    turnip_icd="$(kwin_android_find_turnip_icd "$arch" || true)"
    if [ -n "$turnip_icd" ] \
        && [ -r "$PREFIX/lib/libvulkan_freedreno.so" ] \
        && [ -r "$kgsl_device" ] \
        && [ -w "$kgsl_device" ]; then
        if kwin_android_probe_zink_icd "$turnip_icd" "" 'Turnip|Adreno'; then
            kwin_android_configure_icd_zink "$turnip_icd" "Mesa Turnip KGSL (Zink)"
            return 0
        fi
        echo "Turnip/KGSL files found, but the Zink probe failed" >&2
    fi

    if [ "$mode" = "turnip" ] || [ "$mode" = "kgsl" ]; then
        echo "Requested Turnip/KGSL rendering is unavailable" >&2
        return 1
    fi

    if [ -r "$wrapper_icd" ] && [ -r "$wrapper_lib" ] && [ -r "$android_vulkan" ]; then
        if [ "$mode" = "wrapper" ] \
            || [ "$mode" = "zink" ] \
            || [ "$mode" = "hardware" ] \
            || [ "${KWIN_ANDROID_AUTO_PREFER_WRAPPER:-0}" = "1" ]; then
            if kwin_android_probe_zink_icd "$wrapper_icd" "$android_vulkan" ''; then
                kwin_android_configure_wrapper_zink "$wrapper_icd" "$android_vulkan"
                return 0
            fi
            echo "Android Vulkan wrapper files found, but the Zink probe failed" >&2
        fi
    elif [ "$mode" = "wrapper" ]; then
        echo "Requested Android Vulkan wrapper is unavailable" >&2
        return 1
    fi

    # Direct Android EGL/GLES is the reliable hardware path for emulators and
    # non-KGSL devices. It also avoids an unnecessary GL-to-Vulkan translation.
    if [ "$mode" = "auto" ] && [ -r "$android_egl" ] && [ -r "$android_gles" ]; then
        kwin_android_use_system_gles
        ACCELERATION_MODE="Android system EGL/GLES (auto)"
        export ACCELERATION_MODE
        return 0
    fi

    # vulkan-loader-android exposes the Android loader through this symlink.
    loader_path="$(readlink -f "$PREFIX/lib/libvulkan.so" 2>/dev/null || true)"
    case "$loader_path" in
        /system/*/libvulkan.so)
            if [ "$mode" = "zink" ] || [ "$mode" = "hardware" ]; then
                kwin_android_use_zink
                unset VK_DRIVER_FILES VK_ICD_FILENAMES WRAPPER_VULKAN_PATH TERMUX_VULKAN_BROKER_SOCKET
                ACCELERATION_MODE="Android system Vulkan (Zink)"
                export ACCELERATION_MODE
                return 0
            fi
            ;;
    esac

    if [ "$mode" = "zink" ] || [ "$mode" = "hardware" ] || [ "$mode" = "wrapper" ]; then
        echo "Requested hardware-backed Zink rendering is unavailable" >&2
        return 1
    fi

    kwin_android_use_llvmpipe
    ACCELERATION_MODE="Mesa Software (llvmpipe, auto fallback)"
    export ACCELERATION_MODE
}
