#!/data/data/com.termux/files/usr/bin/bash

# Select a renderer for the native Termux/Bionic KWin build. This file is
# sourced by start-plasma and debug-kwin; it intentionally does not enable the
# glibc Vortek broker path.

kwin_android_use_system_gles()
{
    export KWIN_ANDROID_GL_MODE=system
    export KWIN_COMPOSE=O2ES
    unset TERMUX_ANDROID_ZINK
    unset MESA_LOADER_DRIVER_OVERRIDE GALLIUM_DRIVER LIBGL_ALWAYS_SOFTWARE
    unset VK_DRIVER_FILES VK_ICD_FILENAMES WRAPPER_VULKAN_PATH
    unset MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE
}

kwin_android_use_zink()
{
        export KWIN_ANDROID_GL_MODE=zink
        export TERMUX_ANDROID_ZINK=1
        export MESA_LOADER_DRIVER_OVERRIDE=zink
    export GALLIUM_DRIVER=zink
    unset LIBGL_ALWAYS_SOFTWARE
}

kwin_android_use_llvmpipe()
{
    export KWIN_ANDROID_GL_MODE=llvmpipe
    export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
    export GALLIUM_DRIVER=llvmpipe
    export LIBGL_ALWAYS_SOFTWARE=1
    unset VK_DRIVER_FILES VK_ICD_FILENAMES WRAPPER_VULKAN_PATH
}

kwin_android_select_rendering()
{
    local arch wrapper_icd wrapper_lib android_vulkan loader_path requested_icd

    arch="$(uname -m)"
    wrapper_icd="${KWIN_ANDROID_WRAPPER_ICD:-$PREFIX/share/vulkan/icd.d/wrapper_icd.${arch}.json}"
    wrapper_lib="${KWIN_ANDROID_WRAPPER_LIBRARY:-$PREFIX/lib/libvulkan_wrapper.so}"
    if [ "$arch" = "aarch64" ]; then
        android_vulkan=/system/lib64/libvulkan.so
    else
        android_vulkan=/system/lib/libvulkan.so
    fi

    # Never accidentally mix the native Bionic path with a glibc/Vortek ICD.
    requested_icd="${VK_DRIVER_FILES:-${VK_ICD_FILENAMES:-}}"
    case "$requested_icd" in
        *vortek*|*/glibc/*)
            echo "Ignoring non-Bionic Vulkan ICD: $requested_icd"
            unset VK_DRIVER_FILES VK_ICD_FILENAMES TERMUX_VULKAN_BROKER_SOCKET
            requested_icd=
            ;;
    esac

    if [ "${KWIN_ANDROID_GL_MODE:-}" = "llvmpipe" ] || [ "${KWIN_ANDROID_GL_MODE:-}" = "software" ]; then
        kwin_android_use_llvmpipe
        ACCELERATION_MODE="Mesa Software (llvmpipe)"
        export ACCELERATION_MODE
        return 0
    fi

    case "${KWIN_ANDROID_GL_MODE:-system}" in
        system|system-gles|gles)
            kwin_android_use_system_gles
            ACCELERATION_MODE="Android system EGL/GLES"
            export ACCELERATION_MODE
            return 0
            ;;
    esac

    # Respect an explicitly selected native ICD. VK_DRIVER_FILES and
    # VK_ICD_FILENAMES may both name the same single ICD for loader-version
    # compatibility; never construct a colon-separated ICD list here.
    if [ -n "$requested_icd" ]; then
        kwin_android_use_zink
        export VK_DRIVER_FILES="$requested_icd"
        export VK_ICD_FILENAMES="$requested_icd"
        ACCELERATION_MODE="Native Vulkan ICD (Zink)"
        export ACCELERATION_MODE
        return 0
    fi

    # Preferred emulator/vendor path:
    # generic Bionic loader -> wrapper ICD -> Android system Vulkan loader.
    if [ -f "$wrapper_icd" ] && [ -f "$wrapper_lib" ] && [ -f "$android_vulkan" ]; then
        kwin_android_use_zink
        export VK_DRIVER_FILES="$wrapper_icd"
        export VK_ICD_FILENAMES="$wrapper_icd"
        export WRAPPER_VULKAN_PATH="$android_vulkan"
        export MESA_VK_WSI_DEBUG="${MESA_VK_WSI_DEBUG:-blit}"
        unset TERMUX_VULKAN_BROKER_SOCKET
        ACCELERATION_MODE="Android Vulkan wrapper (Zink)"
        export ACCELERATION_MODE
        return 0
    fi

    # vulkan-loader-android installs $PREFIX/lib/libvulkan.so as a symlink to
    # the system Android loader. This path is sufficient for surfaceless Zink
    # when no Linux X11/Wayland Vulkan WSI is required.
    loader_path="$(readlink -f "$PREFIX/lib/libvulkan.so" 2>/dev/null || true)"
    case "$loader_path" in
        /system/*/libvulkan.so)
            kwin_android_use_zink
            unset VK_DRIVER_FILES VK_ICD_FILENAMES WRAPPER_VULKAN_PATH TERMUX_VULKAN_BROKER_SOCKET
            ACCELERATION_MODE="Android system Vulkan (Zink)"
            export ACCELERATION_MODE
            return 0
            ;;
    esac

    kwin_android_use_llvmpipe
    ACCELERATION_MODE="Mesa Software (llvmpipe)"
    export ACCELERATION_MODE
}
