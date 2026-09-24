# Termux AHardwareBuffer Wayland smoke test

This native Bionic client allocates an `AHardwareBuffer`, renders a test pattern
with Android system EGL/GLES, transfers the handle through
`termux_ahardware_buffer_manager_v1`, and attaches the resulting `wl_buffer` to
an xdg-shell surface.

Version 1 deliberately uses `glFinish()` for producer synchronization. It is a
bring-up tool, not the final QtWayland integration.
