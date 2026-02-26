#!/bin/bash
# Build and deploy KWin Android backend to Termux

set -e

# Configuration
KWIN_SRC="/Users/chenjiaxin/VSCodeProjects/kwin"
BUILD_DIR="$KWIN_SRC/build-android"
TERMUX_PREFIX="/data/data/com.termux/files/usr"
TERMUX_DISPLAY_CLIENT="/Users/chenjiaxin/StudioProjects/termux-display-client"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== KWin Android Backend Build Script ===${NC}"

# Check dependencies
echo -e "${YELLOW}Checking dependencies...${NC}"

if [ ! -d "$TERMUX_DISPLAY_CLIENT" ]; then
    echo -e "${RED}Error: termux-display-client not found at $TERMUX_DISPLAY_CLIENT${NC}"
    exit 1
fi

if ! command -v adb &> /dev/null; then
    echo -e "${RED}Error: adb not found. Please install Android SDK platform-tools${NC}"
    exit 1
fi

# Check if device is connected
if ! adb devices | grep -q "device$"; then
    echo -e "${RED}Error: No Android device connected${NC}"
    echo "Please connect your device and enable USB debugging"
    exit 1
fi

echo -e "${GREEN}✓ All dependencies found${NC}"

# Create build directory
echo -e "${YELLOW}Creating build directory...${NC}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure CMake
echo -e "${YELLOW}Configuring CMake...${NC}"
cmake "$KWIN_SRC" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$TERMUX_PREFIX" \
    -DCMAKE_PREFIX_PATH="$TERMUX_PREFIX" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DBUILD_TESTING=OFF

# Build only the Android backend
echo -e "${YELLOW}Building Android backend...${NC}"
cmake --build . --target KWinAndroidBackend -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build successful${NC}"

# Deploy to device
echo -e "${YELLOW}Deploying to device...${NC}"

# Push the backend library
BACKEND_LIB="$BUILD_DIR/src/backends/android/libKWinAndroidBackend.so"
if [ -f "$BACKEND_LIB" ]; then
    adb push "$BACKEND_LIB" "$TERMUX_PREFIX/lib/qt/plugins/org.kde.kwin.backends/"
    echo -e "${GREEN}✓ Backend library deployed${NC}"
else
    echo -e "${RED}Error: Backend library not found at $BACKEND_LIB${NC}"
    exit 1
fi

# Push startup script
STARTUP_SCRIPT="$KWIN_SRC/src/backends/android/start-kwin-android.sh"
if [ -f "$STARTUP_SCRIPT" ]; then
    adb push "$STARTUP_SCRIPT" "$TERMUX_PREFIX/bin/start-kwin-android"
    adb shell chmod +x "$TERMUX_PREFIX/bin/start-kwin-android"
    echo -e "${GREEN}✓ Startup script deployed${NC}"
else
    echo -e "${YELLOW}Warning: Startup script not found${NC}"
fi

# Push config file
CONFIG_FILE="$KWIN_SRC/src/backends/android/kwinrc.android"
if [ -f "$CONFIG_FILE" ]; then
    adb shell mkdir -p /data/data/com.termux/files/home/.config
    adb push "$CONFIG_FILE" /data/data/com.termux/files/home/.config/kwinrc
    echo -e "${GREEN}✓ Config file deployed${NC}"
else
    echo -e "${YELLOW}Warning: Config file not found${NC}"
fi

echo -e "${GREEN}=== Deployment Complete ===${NC}"
echo ""
echo "To start KWin on your device, run:"
echo "  adb shell"
echo "  start-kwin-android"
echo ""
echo "Or with logging:"
echo "  start-kwin-android 2>&1 | tee kwin.log"
