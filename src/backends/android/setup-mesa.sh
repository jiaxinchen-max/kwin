#!/bin/bash
# Setup script for Mesa rendering in Termux (supports both Zink and llvmpipe)

echo "Setting up Mesa rendering for KWin..."

# Install Mesa if not already installed
if ! pkg list-installed | grep -q mesa; then
    echo "Installing Mesa..."
    pkg install mesa mesa-dev -y
else
    echo "Mesa already installed"
fi

# Detect environment type
detect_environment() {
    echo "Detecting environment..."
    
    # Check for PRoot
    if [ -n "$PROOT_TMP_DIR" ] || [ -n "$PROOT_LOADER" ]; then
        echo "📦 PRoot container detected"
        ENV_TYPE="proot"
    elif [ -f "/.dockerenv" ] || grep -q "docker\|lxc" /proc/1/cgroup 2>/dev/null; then
        echo "🐳 Real container detected"
        ENV_TYPE="container"
    else
        echo "📱 Plain Termux detected"
        ENV_TYPE="termux"
    fi
}

# Check for GPU hardware acceleration support
check_gpu_support() {
    echo "Checking GPU support..."
    
    # Check for Vulkan support
    if [ -f "$PREFIX/lib/libvulkan.so" ] || [ -f "$PREFIX/lib/libvulkan.so.1" ] || \
       [ -f "/usr/lib/libvulkan.so" ] || [ -f "/usr/lib/libvulkan.so.1" ]; then
        echo "✅ Vulkan library found"
        VULKAN_AVAILABLE=1
    else
        echo "❌ Vulkan library not found"
        VULKAN_AVAILABLE=0
    fi
    
    # Check for GPU device files (environment-specific)
    GPU_DEVICES=0
    case $ENV_TYPE in
        "container")
            # Real containers: check standard paths
            if [ -e "/dev/dri/card0" ] || [ -e "/dev/dri/renderD128" ]; then
                echo "✅ Container: GPU device files accessible"
                GPU_DEVICES=1
            fi
            ;;
        "proot")
            # PRoot: check multiple possible paths
            for device in /dev/dri/card0 /dev/dri/renderD128 /dev/mali /dev/kgsl-3d0; do
                if [ -e "$device" ]; then
                    echo "✅ PRoot: GPU device found: $device"
                    GPU_DEVICES=1
                    break
                fi
            done
            if [ $GPU_DEVICES -eq 0 ]; then
                echo "⚠️  PRoot: No GPU devices found (may need device mapping)"
                echo "   Try: proot -b /dev/dri -b /dev/mali ..."
            fi
            ;;
        "termux")
            # Plain Termux: very limited access
            echo "❌ Plain Termux: GPU devices not accessible (Android sandbox)"
            GPU_DEVICES=0
            ;;
    esac
    
    # Determine best rendering mode
    if [ $VULKAN_AVAILABLE -eq 1 ] && [ $GPU_DEVICES -eq 1 ]; then
        echo "🚀 Hardware acceleration available - using Zink"
        setup_zink_rendering
    else
        echo "🔧 Using software rendering - llvmpipe"
        setup_software_rendering
        
        # Provide environment-specific advice
        case $ENV_TYPE in
            "proot")
                echo ""
                echo "💡 To enable GPU acceleration in PRoot:"
                echo "   proot -b /dev/dri -b /dev/mali -r your-rootfs"
                ;;
            "termux")
                echo ""
                echo "💡 For GPU acceleration, consider:"
                echo "   1. Use PRoot with device mapping"
                echo "   2. Use a real container (Docker/LXC)"
                ;;
        esac
    fi
}

setup_zink_rendering() {
    echo "Configuring Zink hardware acceleration..."
    export MESA_LOADER_DRIVER_OVERRIDE=zink
    export GALLIUM_DRIVER=zink
    export MESA_GL_VERSION_OVERRIDE=3.3
    export MESA_GLSL_VERSION_OVERRIDE=330
    export MESA_NO_ERROR=1
    export MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE=1
    unset LIBGL_ALWAYS_SOFTWARE
    
    echo "Zink configuration:"
    echo "GALLIUM_DRIVER=$GALLIUM_DRIVER"
    echo "MESA_GL_VERSION_OVERRIDE=$MESA_GL_VERSION_OVERRIDE"
}

setup_software_rendering() {
    echo "Configuring llvmpipe software rendering..."
    export LIBGL_ALWAYS_SOFTWARE=1
    export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
    export GALLIUM_DRIVER=llvmpipe
    export MESA_GL_VERSION_OVERRIDE=2.1
    export MESA_GLSL_VERSION_OVERRIDE=120
    export MESA_NO_ERROR=1
    export LP_NUM_THREADS=4
    
    echo "Software rendering configuration:"
    echo "LIBGL_ALWAYS_SOFTWARE=$LIBGL_ALWAYS_SOFTWARE"
    echo "GALLIUM_DRIVER=$GALLIUM_DRIVER"
}

# Common Mesa settings
export MESA_DEBUG=silent

# Run detection and setup
detect_environment
check_gpu_support

echo ""
echo "Setup complete! Environment: $ENV_TYPE"
echo "To verify: glxinfo | grep -E '(vendor|renderer)'"

# Check Mesa installation
if [ -f "$PREFIX/lib/libGL.so" ] || [ -f "$PREFIX/lib/libGL.so.1" ]; then
    echo "✅ Mesa library found"
    
    # Test basic OpenGL functionality
    if command -v glxinfo >/dev/null 2>&1; then
        echo "Testing OpenGL..."
        glxinfo | head -10
    fi
else
    echo "❌ Mesa library not found"
    echo "Try: pkg install mesa"
fi

echo "Setup complete!"
echo ""
echo "To use Mesa with KWin:"
echo "1. Source this script: source setup-mesa.sh"
echo "2. Set compositor to OpenGL in KWin config"
echo "3. Start KWin"