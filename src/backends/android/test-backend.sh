#!/data/data/com.termux/files/usr/bin/bash
# Test script for KWin Android backend
# Run this on the Android device in Termux

set -e

PREFIX=/data/data/com.termux/files/usr
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== KWin Android Backend Test ===${NC}\n"

# Test 1: Check backend library exists
echo -e "${YELLOW}Test 1: Checking backend library...${NC}"
BACKEND_LIB="$PREFIX/lib/qt/plugins/org.kde.kwin.backends/libKWinAndroidBackend.so"
if [ -f "$BACKEND_LIB" ]; then
    echo -e "${GREEN}✓ Backend library found${NC}"
    file "$BACKEND_LIB"
else
    echo -e "${RED}✗ Backend library not found at $BACKEND_LIB${NC}"
    exit 1
fi

# Test 2: Check termux-display-client
echo -e "\n${YELLOW}Test 2: Checking termux-display-client...${NC}"
if [ -f "$PREFIX/lib/libwayland.so" ]; then
    echo -e "${GREEN}✓ termux-display-client library found${NC}"
else
    echo -e "${RED}✗ termux-display-client library not found${NC}"
    exit 1
fi

# Test 3: Check display server socket
echo -e "\n${YELLOW}Test 3: Checking display server socket...${NC}"
SOCKET="$PREFIX/tmp/wayland-0"
if [ -S "$SOCKET" ]; then
    echo -e "${GREEN}✓ Display server socket found${NC}"
    ls -la "$SOCKET"
else
    echo -e "${RED}✗ Display server socket not found${NC}"
    echo "Make sure termux-app display server is running"
fi

# Test 4: Check EGL extensions
echo -e "\n${YELLOW}Test 4: Checking EGL extensions...${NC}"
if command -v eglinfo &> /dev/null; then
    REQUIRED_EXTS=(
        "EGL_KHR_image_base"
        "EGL_ANDROID_get_native_client_buffer"
        "EGL_ANDROID_image_native_buffer"
    )
    
    EGL_EXTS=$(eglinfo 2>/dev/null | grep "EGL extensions")
    
    for ext in "${REQUIRED_EXTS[@]}"; do
        if echo "$EGL_EXTS" | grep -q "$ext"; then
            echo -e "${GREEN}✓ $ext${NC}"
        else
            echo -e "${YELLOW}⚠ $ext not found (may still work)${NC}"
        fi
    done
else
    echo -e "${YELLOW}⚠ eglinfo not found, skipping extension check${NC}"
fi

# Test 5: Check OpenGL ES version
echo -e "\n${YELLOW}Test 5: Checking OpenGL ES version...${NC}"
if command -v glxinfo &> /dev/null; then
    GL_VERSION=$(glxinfo 2>/dev/null | grep "OpenGL ES" | head -n1)
    if [ -n "$GL_VERSION" ]; then
        echo -e "${GREEN}✓ $GL_VERSION${NC}"
    fi
else
    echo -e "${YELLOW}⚠ glxinfo not found, skipping GL version check${NC}"
fi

# Test 6: Check AHardwareBuffer support
echo -e "\n${YELLOW}Test 6: Checking AHardwareBuffer support...${NC}"
SDK_VERSION=$(getprop ro.build.version.sdk)
if [ "$SDK_VERSION" -ge 26 ]; then
    echo -e "${GREEN}✓ Android SDK $SDK_VERSION (AHardwareBuffer supported)${NC}"
else
    echo -e "${RED}✗ Android SDK $SDK_VERSION (requires 26+)${NC}"
    exit 1
fi

# Test 7: Test KWin startup (dry run)
echo -e "\n${YELLOW}Test 7: Testing KWin startup (dry run)...${NC}"
if command -v kwin_wayland &> /dev/null; then
    echo -e "${GREEN}✓ kwin_wayland found${NC}"
    
    # Try to get version
    if kwin_wayland --version 2>/dev/null; then
        echo -e "${GREEN}✓ KWin version check passed${NC}"
    fi
else
    echo -e "${RED}✗ kwin_wayland not found${NC}"
    echo "Please install KWin first: pkg install kwin"
    exit 1
fi

# Test 8: Check dependencies
echo -e "\n${YELLOW}Test 8: Checking runtime dependencies...${NC}"
DEPS=(
    "libQt6Core.so"
    "libQt6Gui.so"
    "libKF6WindowSystem.so"
    "libEGL.so"
    "libGLESv3.so"
)

for dep in "${DEPS[@]}"; do
    if [ -f "$PREFIX/lib/$dep" ] || ldconfig -p 2>/dev/null | grep -q "$dep"; then
        echo -e "${GREEN}✓ $dep${NC}"
    else
        echo -e "${YELLOW}⚠ $dep not found${NC}"
    fi
done

# Summary
echo -e "\n${GREEN}=== Test Summary ===${NC}"
echo "If all tests passed, you can start KWin with:"
echo "  export KWIN_BACKEND=android"
echo "  kwin_wayland --xwayland"
echo ""
echo "Or use the startup script:"
echo "  start-kwin-android"

# Interactive test option
echo -e "\n${YELLOW}Run interactive test? (y/n)${NC}"
read -r response
if [[ "$response" =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}Starting KWin with Android backend...${NC}"
    echo "Press Ctrl+C to stop"
    sleep 2
    
    export KWIN_BACKEND=android
    export QT_LOGGING_RULES="kwin_*.debug=true"
    
    timeout 10s kwin_wayland --no-kactivities --no-global-shortcuts 2>&1 | head -n 50
    
    if [ $? -eq 124 ]; then
        echo -e "\n${GREEN}✓ KWin started successfully (timed out as expected)${NC}"
    fi
fi

echo -e "\n${GREEN}Testing complete!${NC}"
