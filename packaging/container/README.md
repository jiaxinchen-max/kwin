# GPU-accelerated Plasma in a proot container

This directory holds the pieces for running the **Plasma desktop inside a
proot/glibc container** while only `kwin_wayland` runs natively (Termux/Bionic),
with container apps rendering on the **GPU (Adreno via Turnip/Vulkan)**.

## Architecture

```
  Termux (Bionic)                     proot container (Debian/glibc)
  ┌──────────────────────┐           ┌───────────────────────────────┐
  │ kwin_wayland          │  wayland  │ plasmashell + Plasma apps      │
  │  (Android backend,    │◄──socket──┤  QSG_RHI_BACKEND=vulkan        │
  │   zink/Turnip)        │           │  → Turnip Vulkan WSI → dma-buf │
  └──────────────────────┘           └───────────────────────────────┘
        shared $XDG_RUNTIME_DIR  (bind-mounted in the container as /run/wl)
```

Only `kwin_wayland` is native. The shell, its daemons and all apps live in the
container and talk to the compositor over the **shared Wayland socket**
(`wayland-0`, exposed in `$XDG_RUNTIME_DIR` → `/run/wl`). The container runs its
**own D-Bus session**: the native bus cannot be shared across the proot boundary
(proot cannot forge `SO_PEERCRED`, so dbus `EXTERNAL` auth fails). Window
management still works over Wayland (`plasma-window-management`); only
`org.kde.KWin` D-Bus scripting is unavailable across the split.

## Why Vulkan RHI (the black-screen fix)

Container Qt/QtQuick apps black-screened under the default OpenGL path. Root
cause: the container has **no DRM render node** (Adreno is driven through KGSL;
`/dev/kgsl-3d0` is not a DRM device). Qt's default OpenGL RHI uses Mesa's
EGL-Wayland path, which needs `gbm_create_device()` on a DRM node to allocate
window buffers. On KGSL that fails, so Mesa silently falls back to the
**swrast / wl_shm** present path: zink still renders on the GPU, but the present
copies the tiled GPU image into a linear `wl_shm` buffer on the CPU and the
result is **black, with no error logged**.

The fix is to bypass GL/zink/gbm entirely and use **Qt's Vulkan RHI**
(`QSG_RHI_BACKEND=vulkan`): Qt renders with Turnip and presents through Turnip's
**Vulkan WSI** (a `VkSwapchain` on the `wl_surface` → dma-buf, no gbm). KWin
imports those dma-bufs fine — the exact path `vkcube` uses.

Diagnostic evidence (`WAYLAND_DEBUG=1`): a GL client (`es2gears`) connects on the
`mesa egl swrast display queue` and only ever attaches `wl_shm_pool` buffers,
never a dma-buf; `vkcube` (Vulkan WSI) attaches dma-buf and renders correctly.

## One-time container setup

```sh
proot-distro login debian -- bash -c '
  apt-get update
  # The Plasma shell + apps:
  apt-get install -y plasma-workspace plasma-desktop dbus-x11 kde-cli-tools
  # Qt needs the *unversioned* Vulkan loader symlink (libvulkan1 ships only
  # libvulkan.so.1, which QLibrary("vulkan") does not look for):
  apt-get install -y libvulkan-dev
'
```

The Turnip Vulkan ICD (`/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json` →
`libvulkan_freedreno.so`, KGSL-capable) comes from the mesa-for-android-container
tarball, not from Debian's `mesa-vulkan-drivers` (whose Turnip has no KGSL
backend). Verify with:

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json \
  vulkaninfo | grep -E 'deviceName|VK_KHR_wayland_surface|VK_KHR_swapchain'
```

Then copy the session launcher into the container:

```sh
cp plasma-container-session.sh \
   "$(proot-distro ... rootfs)/root/plasma-container-session.sh"
# or bind-mount it at launch (see below)
```

## Launching

1. Native side — compositor only (no native Plasma shell):

   ```sh
   KWIN_ANDROID_GL_MODE=zink start-plasma --compositor-only
   ```

   This starts `kwin_wayland` only (no native Plasma shell) and prints the exact
   container command to run.

2. Container side — the Plasma session on the GPU:

   ```sh
   proot-distro login debian \
       --bind "$HOME/.termux/kwin-runtime/runtime-$(id -u)":/run/wl \
       -- /root/plasma-container-session.sh
   ```

The shell renders on the GPU via Vulkan (verify in
`/run/wl/plasma-container.log`: `Initializing QRhi Vulkan backend` +
`using this physical device: Turnip Adreno ...`).

## Status / caveats

- Verified on a real Mi 11 (KWin 6.6): container plasmashell renders the full
  desktop (wallpaper + panel, live clock) on the GPU. See
  `plasma-vulkan-desktop.png` in this directory.
- Client dma-buf windows only map while a shell (plasmashell) is running; on a
  bare compositor they do not appear.
- GL (zink) container apps still use the broken swrast/wl_shm path — use Vulkan
  RHI for Qt apps, or run GL apps through the native side.
- Container GL stack is Debian Mesa (zink) while the Turnip Vulkan ICD is the
  newer tarball build; the Vulkan path works across that version mix.
