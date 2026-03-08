#!/data/data/com.termux/files/usr/bin/bash
# Complete Plasma Desktop startup script for Android (Termux)

set -e

echo "Starting Plasma Desktop on Android..."

# Check if running in Termux
if [ ! -d "/data/data/com.termux" ]; then
    echo "Error: This script must run in Termux environment"
    exit 1
fi

# Environment setup
export KWIN_BACKEND=android
export QT_QPA_PLATFORM=wayland
export XDG_SESSION_TYPE=wayland
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/data/data/com.termux/files/usr/tmp}"
export XDG_CURRENT_DESKTOP=KDE
export KDE_SESSION_VERSION=5

# Optional: Set custom resolution
export KWIN_ANDROID_WIDTH=${KWIN_ANDROID_WIDTH:-1920}
export KWIN_ANDROID_HEIGHT=${KWIN_ANDROID_HEIGHT:-1080}
export KWIN_ANDROID_REFRESH=${KWIN_ANDROID_REFRESH:-60}

# Performance tuning
export QSG_RENDER_LOOP=basic
export QT_XCB_GL_INTEGRATION=none

# Create runtime directory
mkdir -p "$XDG_RUNTIME_DIR"

# Check dependencies
echo "Checking Plasma dependencies..."

REQUIRED_BINARIES=(
    "kwin_wayland"
    "plasmashell" 
    "kded5"
)

for binary in "${REQUIRED_BINARIES[@]}"; do
    if ! command -v "$binary" >/dev/null 2>&1; then
        echo "Error: $binary not found"
        echo "Please install the complete KDE Plasma desktop environment"
        exit 1
    fi
done

echo "Starting Plasma Desktop components..."

# Method 1: Use startplasma-wayland if available
if command -v startplasma-wayland >/dev/null 2>&1; then
    echo "Using startplasma-wayland..."
    exec startplasma-wayland
else
    # Method 2: Manual component startup
    echo "Starting components manually..."
    
    # Start KWin in background
    echo "Starting KWin..."
    kwin_wayland --xwayland --no-kactivities --replace &
    KWIN_PID=$!
    
    # Wait for KWin to initialize
    sleep 3
    
    # Start KDE daemon
    echo "Starting KDE daemon..."
    kded5 &
    KDED_PID=$!
    
    # Start Plasma Shell
    echo "Starting Plasma Shell..."
    plasmashell &
    PLASMA_PID=$!
    
    # Optional: Start additional components
    if command -v krunner >/dev/null 2>&1; then
        echo "Starting KRunner..."
        krunner &
    fi
    
    if command -v systemsettings5 >/dev/null 2>&1; then
        echo "System Settings available"
    fi
    
    echo "Plasma Desktop started successfully!"
    echo "KWin PID: $KWIN_PID"
    echo "KDE Daemon PID: $KDED_PID" 
    echo "Plasma Shell PID: $PLASMA_PID"
    
    # Wait for main processes
    wait $KWIN_PID
fi