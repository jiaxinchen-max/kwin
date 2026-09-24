# Qt Wayland AHardwareBuffer client integration

Qt 6 Wayland client buffer plugin for native Bionic applications. It renders
Qt Quick/OpenGL ES into a triple-buffered `AHardwareBuffer` swapchain and sends
the buffers to KWin through `termux_ahardware_buffer_manager_v1`.

Build and install this plugin against the same Qt private-header version used at
runtime. Start applications through the included launcher:

```sh
qt-system-gles-run qml6 application.qml
```

The launcher selects Android system EGL without globally replacing Termux Mesa.
The plugin resolves the active vendor GLES implementation inside Android's
`sphal` linker namespace, including emulator gfxstream (`ro.hardware.egl`) and
device vendor drivers.

Requirements:

- native Bionic Qt 6 and QtWayland;
- KWin advertising `termux_ahardware_buffer_manager_v1`;
- Android API 26 or newer for the exported linker namespace API;
- the plugin must match Qt's private ABI exactly.

This stage does not cover XWayland or glibc/proot clients.
