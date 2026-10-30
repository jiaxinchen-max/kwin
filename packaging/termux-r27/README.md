# Termux KWin r27 / Qt 6.12 integration

Base: `ff4c62953913ad11f0dedf9658bc8bde52ac3fa5` (`Plasma/6.6`, r26).
The six KWin patches in `patches/` are integrated into this source tree. The Qt AHB/client adapter source is integrated in `tools/termux-qt-ahb-plugin/`; the external QtBase patch is `patches/qt612-android-any-rtti.patch` and must be applied to QtBase 6.12.0, not KWin.

Explicit modes are `system`, `zink`, `llvmpipe`. Unknown/empty/Auto modes, failed initialization, driver mismatch, and conflicting running KWin sessions fail with their original diagnostics. Existing-session conflict is exit 73. No renderer/provider fallback is supported. Container support is XFCE/trixie and Plasma/forky; the protocol version fix does not assert trixie Plasma compatibility.

## Packages and ownership

- `kwin_6.6.0-27_aarch64.deb`: complete r26 package plus the verified v4 KWin binaries/scripts, Qt AHB plugin, private EGL forwarding library and client-renderer probe. The existing SONAME `libkwin.so.6.6.7` remains unchanged.
- `qt6-qtbase_6.12.0-1+termuxkwin1_aarch64.deb`: complete original QtBase package; only `lib/qt6/plugins/wayland-shell-integration/libxdg-shell.so` is replaced with the verified RTTI fix.
- Matching `qt6-qtbase-{gtk-platformtheme,cross-tools}` packages preserve every payload byte. Only version, exact QtBase dependency and generated package bookkeeping change, keeping existing installed subpackages upgradeable.
- `libandroid-stub_30-1_aarch64.deb` supplies the required `libtermux-platform-ns.so`; KWin declares this dependency. There is no duplicate library ownership.
- Original gl4es, libepoxy, termux-render and xwayland release assets are copied verbatim. In particular libepoxy uses the published `c78f1674…` asset, not another builder file with the same name.

KWin requires the exact patched QtBase version. Install the nine native packages together using the App's pinned package list; do not install the unrelated Debian Discover packages into the Termux host. Existing conflicts/replaces/provides, triggers, conffiles and maintainer scripts are preserved. `md5sums` and Installed-Size are regenerated. `PACKAGE-DIFF.json` lists every changed/added payload file and its before/after SHA and mode. Original QtBase GTK/cross-tools payloads must have zero changes.

## Byte-preserving repack

Run only on the designated Linux builder; all input SHA values are hard requirements in `repack.py`:

```sh
python3 packaging/termux-r27/repack.py \
  --inputs-dir /absolute/path/to/original-debs \
  --overlay-tar /absolute/path/to/kwin-qt612-native-fixes-20261030-aarch64-v4.tar.gz \
  --output-dir /absolute/path/to/new-empty-output \
  --source-epoch 1793314800
```

The v4 input SHA is `c358738df64b81e8abe8d4aa57b74978f2959e23f9061389f6d63f1371b2daf8`. Every archived member is checked. The repack changes no ELF bytes beyond replacing the explicitly listed files with those exact already tested ELF bytes. Extracting each final archive must reproduce the intended inventory and maintainer metadata. A second fresh output directory with the same epoch must produce byte-identical debs. `ASSETS.json` exposes `name`, `sha256`, `pkg`, `version`, `architecture`, `depends` for the App pin updater.

## Native source build

`kwin-r26-build.sh` and `qtbase-612-build.sh` preserve the package-build configurations used for the original Termux sysroot. Configure KWin with the integrated source and those same Termux patches/toolchain, apply the external RTTI patch to QtBase 6.12.0 (source tar SHA `a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307`), and configure the standalone Qt AHB tree with `build-qt-ahb.sh` using explicit source/build/cmake/ninja/compiler/prefix/Qt-host arguments. The native source files here match the exact production build-source hashes; r27 publication itself uses the already tested v4 ELF instead of compiling different binaries.

For existing explicitly configured trees, the production build invocation is:

```sh
bash packaging/termux-r27/build-native.sh \
  /home/builder/.termux-build/_cache/cmake-4.4.4/bin/cmake \
  /home/builder/.termux-build/kwin/build \
  /home/builder/.termux-build/qt6-qtbase/build \
  /home/builder/desktop-fixes-20261030/qt-ahb-plugin/build 4
```

The package's original source tar SHA is `da789158db78667b3dabef54a96c781e2505a892f07e81a8f868ee3733a6b6ec`. Native compilation is restricted to the designated builder. Repack scripts never invoke a compiler. Regression sources cover explicit mode contracts, renderer matching, real D-Bus activation environment, production startup conflicts, actual Wayland output-device versions and cross-DSO RTTI (`tests/`). These are separate from the already completed formal App UI acceptance.

## Discover container assets

The four `6.7.4-3+termuxfix4` Debian debs optionally attached to this release are for the existing **Debian forky Plasma container**, never the Bionic Termux host. The App container installer still uses its configured APT source; this release does not make new containers automatically receive these customized debs and does not change that business installation path. Source patches and isolated regression sources are maintained in `jiaxinchen-max/termux-app`, under `docs/desktop-testing/patches/` and `docs/desktop-testing/native-tests/discover-appstream-refresh/`.

## Acceptance and limits

Verified payload: native Plasma System on emulator and Adreno 660, repeated popup/right-click, direct touch and rotation; forky Plasma Software on emulator and Zink on Xiaomi, including Discover directory/search/install/real calculation/uninstall. Existing global-shortcut and Flatpak authentication limitations remain. Concurrent external XCB/native display-owner conflict is recorded but not fixed in r27. Old fix2 initial raw metadata was not preserved, so its exact original instance cannot be replayed; the AppStream refresh mechanism and final fix4 paths were independently verified. No device logs, caches or databases belong in release assets.
