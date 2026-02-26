# KWin Android Backend - Quick Start Guide

## Prerequisites

### On Your Development Machine

1. **Android NDK** (for cross-compilation)
2. **ADB** (Android Debug Bridge)
3. **CMake** and build tools

### On Your Android Device

1. **Termux** app installed
2. **Modified termux-app** with display server support
3. **termux-display-client** library installed
4. **Root access** (optional, but recommended for best performance)

## Step 1: Build termux-display-client

```bash
cd /Users/chenjiaxin/StudioProjects/termux-display-client

# Build for Android
./build.sh

# Install to Termux
adb push libs/arm64-v8a/libwayland.so /data/data/com.termux/files/usr/lib/
```

## Step 2: Install Modified termux-app

```bash
cd /Users/chenjiaxin/StudioProjects/termux-app

# Build APK
./gradlew assembleDebug

# Install on device
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Step 3: Build KWin Android Backend

```bash
cd /Users/chenjiaxin/VSCodeProjects/kwin

# Create build directory
mkdir build-android
cd build-android

# Configure (adjust paths as needed)
cmake .. \
    -DCMAKE_PREFIX_PATH=/data/data/com.termux/files/usr \
    -DCMAKE_INSTALL_PREFIX=/data/data/com.termux/files/usr \
    -DCMAKE_BUILD_TYPE=Release

# Build only the Android backend
cmake --build . --target KWinAndroidBackend -j$(nproc)
```

## Step 4: Deploy to Device

### Option A: Using the deploy script

```bash
cd src/backends/android
chmod +x build-and-deploy.sh
./build-and-deploy.sh
```

### Option B: Manual deployment

```bash
# Push backend library
adb push build-android/src/backends/android/libKWinAndroidBackend.so \
    /data/data/com.termux/files/usr/lib/qt/plugins/org.kde.kwin.backends/

# Push startup script
adb push src/backends/android/start-kwin-android.sh \
    /data/data/com.termux/files/usr/bin/start-kwin-android

adb shell chmod +x /data/data/com.termux/files/usr/bin/start-kwin-android

# Push config
adb shell mkdir -p /data/data/com.termux/files/home/.config
adb push src/backends/android/kwinrc.android \
    /data/data/com.termux/files/home/.config/kwinrc
```

## Step 5: Install KWin in Termux

Open Termux on your device and run:

```bash
# Update package list
pkg update

# Install KWin and dependencies
pkg install kwin qt6-base qt6-wayland kde-frameworks

# Verify installation
kwin_wayland --version
```

## Step 6: Test the Backend

```bash
# Run the test script
./test-backend.sh
```

Expected output:
```
✓ Backend library found
✓ termux-display-client library found
✓ Display server socket found
✓ EGL extensions available
✓ OpenGL ES 3.0+ available
✓ AHardwareBuffer supported
✓ KWin found
```

## Step 7: Launch KWin

### Option A: Using the startup script

```bash
start-kwin-android
```

### Option B: Manual launch

```bash
# Set environment variables
export KWIN_BACKEND=android
export QT_QPA_PLATFORM=wayland
export XDG_SESSION_TYPE=wayland

# Launch KWin
kwin_wayland --xwayland
```

### Option C: With full Plasma desktop

```bash
# Install Plasma
pkg install plasma-desktop plasma-workspace

# Start Plasma
startplasma-wayland
```

## Step 8: Verify It's Working

You should see:

1. **In logcat/terminal**: EGL initialization messages
2. **On screen**: KWin compositor rendering
3. **Touch input**: Working touch and gestures
4. **Windows**: Able to launch and manage applications

### Test with a simple app:

```bash
# Install and run a terminal
pkg install konsole
konsole &

# Or a graphical app
pkg install kate
kate &
```

## Troubleshooting

### Problem: Backend library not loaded

**Error**: `Cannot load library: libKWinAndroidBackend.so`

**Solution**:
```bash
# Check if library exists
ls -la $PREFIX/lib/qt/plugins/org.kde.kwin.backends/libKWinAndroidBackend.so

# Check dependencies
ldd $PREFIX/lib/qt/plugins/org.kde.kwin.backends/libKWinAndroidBackend.so

# Reinstall if needed
adb push libKWinAndroidBackend.so /data/data/com.termux/files/usr/lib/qt/plugins/org.kde.kwin.backends/
```

### Problem: Display server socket not found

**Error**: `Failed to connect to display server`

**Solution**:
```bash
# Check if termux-app display server is running
ls -la /data/data/com.termux/files/usr/tmp/wayland-0

# If not found, restart termux-app
am force-stop com.termux
am start com.termux/.app.TermuxActivity
```

### Problem: EGL initialization failed

**Error**: `eglInitialize failed`

**Solution**:
```bash
# Check EGL info
eglinfo | grep -i version
eglinfo | grep -i "EGL extensions"

# Ensure you have required extensions:
# - EGL_KHR_image_base
# - EGL_ANDROID_get_native_client_buffer
# - EGL_ANDROID_image_native_buffer
```

### Problem: Black screen / No rendering

**Error**: No visible output

**Solutions**:
1. Check KWin logs:
   ```bash
   journalctl -f | grep kwin
   # or
   kwin_wayland 2>&1 | tee kwin.log
   ```

2. Verify GPU driver:
   ```bash
   glxinfo | grep "OpenGL renderer"
   # Should show your device's GPU (e.g., Adreno, Mali)
   ```

3. Check framebuffer status in logs:
   ```
   Should see: "Framebuffer created and complete"
   Should NOT see: "Framebuffer incomplete"
   ```

### Problem: Poor performance / Lag

**Solutions**:

1. **Lower resolution**:
   ```bash
   export KWIN_ANDROID_WIDTH=1280
   export KWIN_ANDROID_HEIGHT=720
   start-kwin-android
   ```

2. **Disable effects**:
   ```bash
   # Edit ~/.config/kwinrc
   [Compositing]
   AnimationSpeed=0
   
   [Plugins]
   blurEnabled=false
   contrastEnabled=false
   ```

3. **Use GPU governor**:
   ```bash
   # Requires root
   su
   echo performance > /sys/class/kgsl/kgsl-3d0/devfreq/governor
   ```

4. **Use lighter desktop**:
   ```bash
   # Instead of Plasma, use LXQt
   pkg install lxqt
   startlxqt
   ```

### Problem: Input not working

**Error**: Touch/keyboard/mouse not responding

**Solution**:
```bash
# Check input devices are created
dmesg | grep "Android Virtual"

# Test socket communication
cat /data/data/com.termux/files/usr/tmp/wayland-0
# Should show binary data when you touch screen

# Enable input debugging
export QT_LOGGING_RULES="kwin_input*.debug=true"
kwin_wayland 2>&1 | grep input
```

## Performance Tuning

### For Best Performance

```bash
# ~/.bashrc or startup script

# Low-latency rendering
export QSG_RENDER_LOOP=basic

# Disable vsync (if causing issues)
export KWIN_FORCE_SW_VSYNC=1

# Reduce memory usage
export QT_QPA_PLATFORM=wayland-egl
export QT_WAYLAND_FORCE_DPI=96

# GPU performance
export MESA_GLTHREAD=true
export mesa_glthread=true
```

### For Low-End Devices

```bash
# Use software rendering for window decorations
export QT_QUICK_BACKEND=software

# Disable animations
export KDE_SKIP_KDESU_ANIMATION=1

# Reduce quality
export KWIN_COMPOSE=O2
```

### For High-End Devices

```bash
# Enable triple buffering
export KWIN_TRIPLE_BUFFER=1

# Higher resolution
export KWIN_ANDROID_WIDTH=2560
export KWIN_ANDROID_HEIGHT=1440
export KWIN_ANDROID_REFRESH=120

# Enable all effects
export KWIN_EFFECTS_FORCE_ANIMATIONS=1
```

## Monitoring Performance

### Check FPS

```bash
# Install and run glmark2
pkg install glmark2-es2
glmark2-es2

# Monitor KWin FPS
qdbus org.kde.KWin /Compositor org.kde.kwin.Compositing.refresh
```

### Monitor GPU Usage

```bash
# Requires root
watch -n 1 cat /sys/class/kgsl/kgsl-3d0/gpubusy_percentage

# Or use Android tools
adb shell dumpsys gfxinfo com.termux
```

### Monitor Memory

```bash
# KWin memory usage
ps aux | grep kwin_wayland

# Detailed memory info
cat /proc/$(pidof kwin_wayland)/status | grep -E "Vm|Rss"
```

## Advanced Configuration

### Custom Resolution

```bash
# Create ~/.config/kwin_android.conf
cat > ~/.config/kwin_android.conf << EOF
[Display]
Width=1920
Height=1080
RefreshRate=60
Scale=1.0
EOF
```

### Custom Input Mapping

Edit `android_backend.cpp` and rebuild:

```cpp
static const int android_to_linux_keycode[304] = {
    // Add your custom mappings here
    [4] = KEY_ESC,  // Android BACK = Linux ESC
    // ...
};
```

### Enable Debug Logging

```bash
# Create ~/.config/QtProject/qtlogging.ini
[Rules]
kwin_*.debug=true
qt.qpa.*.debug=true
```

## Next Steps

1. **Install applications**:
   ```bash
   pkg install firefox gimp inkscape libreoffice
   ```

2. **Setup desktop environment**:
   ```bash
   pkg install plasma-desktop plasma-workspace
   systemctl --user enable plasma-plasmashell
   ```

3. **Configure appearance**:
   ```bash
   systemsettings5  # KDE System Settings
   ```

4. **Optimize for your device**: See Performance Tuning section above

## Getting Help

- **KWin logs**: `journalctl -xe | grep kwin`
- **System logs**: `logcat | grep -i kwin`
- **Test script**: `./test-backend.sh`
- **Debug build**: Build with `-DCMAKE_BUILD_TYPE=Debug`

## Known Limitations

- Single output only (no external display support yet)
- Software cursor (no hardware cursor overlay)
- No screen recording via KWin (use Android screen recorder)
- Some effects may be slow on low-end devices

## Contributing

Found a bug? Have an improvement? Please contribute!

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

GPL-2.0-or-later (same as KWin)
