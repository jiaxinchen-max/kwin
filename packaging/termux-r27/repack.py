#!/usr/bin/env python3
"""Repack r27 on the designated Linux builder; never compile or substitute ELF."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import tarfile

BASE = {
    'kwin_6.6.0-26_aarch64.deb': '8973972bc8538902f6cd4940a2b388b11e10aa2b968d015c9a03ad559129a5e3',
    'qt6-qtbase_6.12.0_aarch64.deb': 'd3075b12483b94d9ac75404378c7dd5b944321201b2b50b4166b66f806c735d8',
    'qt6-qtbase-gtk-platformtheme_6.12.0_aarch64.deb': '675942fb7082a1b615658d064e4b69a585bc4ea6eec93487e21e7dd04372fc99',
    'qt6-qtbase-cross-tools_6.12.0_all.deb': 'fa572b4ca8529f658602b70ba38cdc6e665f777f96a526cefbd18bea3a50310d',
}
PASSTHROUGH = {
    'gl4es_1.1.7.20260927_aarch64.deb': 'eb0ba70d084c87b84b144ef2ce6ad415ca3810dcb356d77e4e8cefe962157913',
    'libepoxy_1.5.10-6_aarch64.deb': 'c78f1674337e4a90a9c933d6301d058bf2be85bc926baae6202320e803582d83',
    'termux-render_0.1.0-1_aarch64.deb': '4dc75a1aed887a59790f03a8c76948de514678fd7c8a00fb04fbc2975a0ac0d6',
    'xwayland_24.1.11-1_aarch64.deb': '3cc2999ae0ba90495abad71445f794723efc06a97c001b383467665e3038e8a4',
    'libandroid-stub_30-1_aarch64.deb': '5b392496146309b8454780821591dccb7e6a7050f8789f3bc97b4bb6c2b6bc67',
}
OVERLAY_SHA = 'c358738df64b81e8abe8d4aa57b74978f2959e23f9061389f6d63f1371b2daf8'
PREFIX = Path('data/data/com.termux/files/usr')
QT_VERSION = '6.12.0-1+termuxkwin1'
KWIN_VERSION = '6.6.0-27'
KWIN_FILES = [
    'bin/kwin_wayland', 'bin/start-plasma', 'bin/kwin-android-rendering-env',
    'lib/libkwin.so.6.6.7', 'bin/kwin-qt-client-renderer-probe',
    'lib/qt6/termux-system-egl/libEGL.so', 'lib/qt6/termux-system-egl/libEGL.so.1',
    'lib/qt6/plugins/wayland-graphics-integration-client/libqt-plugin-termux-ahardware-buffer.so',
]
QT_FILE = 'lib/qt6/plugins/wayland-shell-integration/libxdg-shell.so'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def read_field(root, field):
    data = (root / 'DEBIAN/control').read_text()
    match = re.search(r'^' + re.escape(field) + r': (.*)$', data, re.M)
    if not match:
        raise RuntimeError('missing control field ' + field)
    return match.group(1)


def set_field(root, field, value):
    path = root / 'DEBIAN/control'
    data = path.read_text()
    data, count = re.subn(r'^' + re.escape(field) + r':.*(?:\n[ \t].*)*$', field + ': ' + value, data, flags=re.M)
    if count == 0:
        data = data.rstrip() + '\n' + field + ': ' + value + '\n'
    elif count != 1:
        raise RuntimeError('duplicate control field ' + field)
    path.write_text(data)


def inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if relative.parts[0] == 'DEBIAN' or path.is_dir():
            continue
        info = path.lstat()
        result[str(relative)] = {'mode': stat.S_IMODE(info.st_mode)}
        if path.is_symlink():
            result[str(relative)]['symlink'] = os.readlink(path)
        else:
            result[str(relative)]['sha256'] = sha(path)
    return result


def rebuild_metadata(root):
    paths = [p for p in root.rglob('*') if p.is_file() and not p.is_symlink() and p.relative_to(root).parts[0] != 'DEBIAN']
    md5 = ''.join(hashlib.md5(p.read_bytes()).hexdigest() + '  ' + str(p.relative_to(root)) + '\n' for p in sorted(paths))
    (root / 'DEBIAN/md5sums').write_text(md5)
    set_field(root, 'Installed-Size', str((sum(p.stat().st_size for p in paths) + 1023) // 1024))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--inputs-dir', required=True, type=Path)
    parser.add_argument('--overlay-tar', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--source-epoch', required=True, type=int)
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('run on the designated Linux builder; no local packaging/build')
    if args.output_dir.exists():
        parser.error('output directory already exists; use a fresh explicit directory')
    for name, expected in {**BASE, **PASSTHROUGH}.items():
        if sha(args.inputs_dir / name) != expected:
            raise RuntimeError('input SHA mismatch: ' + name)
    if sha(args.overlay_tar) != OVERLAY_SHA:
        raise RuntimeError('verified v4 overlay SHA mismatch')
    args.output_dir.mkdir(parents=True)
    work = args.output_dir / 'work'
    work.mkdir()
    with tarfile.open(args.overlay_tar) as archive:
        archive.extractall(work, filter='data')
    overlay = work / 'kwin-qt612-native-fixes-20261030-aarch64-v4'
    for line in (overlay / 'SHA256SUMS').read_text().splitlines():
        expected, name = line.split(None, 1)
        if sha(overlay / name.strip()) != expected:
            raise RuntimeError('overlay member SHA mismatch: ' + name)
    manifests = {}
    for name in BASE:
        root = work / name.removesuffix('.deb')
        run('dpkg-deb', '--raw-extract', args.inputs_dir / name, root)
        package = read_field(root, 'Package')
        old_version = read_field(root, 'Version')
        architecture = read_field(root, 'Architecture')
        before = inventory(root)
        original_metadata = {str(p.relative_to(root / 'DEBIAN')): p.read_bytes() for p in (root / 'DEBIAN').rglob('*') if p.is_file()}
        expected_changes = []
        if package == 'kwin':
            if old_version != '6.6.0-26':
                raise RuntimeError('unexpected KWin base version')
            set_field(root, 'Version', KWIN_VERSION)
            depends = read_field(root, 'Depends')
            deps = depends.split(', ')
            if deps.count('qt6-qtbase') != 1:
                raise RuntimeError('unexpected QtBase dependency in KWin')
            deps[deps.index('qt6-qtbase')] = 'qt6-qtbase (= ' + QT_VERSION + ')'
            deps += ['libandroid-stub (>= 30-1)']
            set_field(root, 'Depends', ', '.join(deps))
            files = KWIN_FILES
        elif package == 'qt6-qtbase':
            set_field(root, 'Version', QT_VERSION)
            files = [QT_FILE]
        elif package in ('qt6-qtbase-gtk-platformtheme', 'qt6-qtbase-cross-tools'):
            set_field(root, 'Version', QT_VERSION)
            depends = read_field(root, 'Depends')
            if 'qt6-qtbase (= 6.12.0)' not in depends:
                raise RuntimeError('unexpected QtBase subpackage dependency')
            set_field(root, 'Depends', depends.replace('qt6-qtbase (= 6.12.0)', 'qt6-qtbase (= ' + QT_VERSION + ')'))
            files = []
        else:
            raise RuntimeError('unexpected base package: ' + package)
        for relative in files:
            source = overlay / 'payload' / relative
            target = root / PREFIX / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            os.chmod(target, 0o755)
            if sha(target) != sha(source):
                raise RuntimeError('payload copy mismatch: ' + relative)
            expected_changes.append(str(PREFIX / relative))
        after = inventory(root)
        changed = sorted(key for key in before.keys() | after.keys() if before.get(key) != after.get(key))
        if changed != sorted(expected_changes):
            raise RuntimeError('unexpected payload diff: ' + package + ': ' + repr(changed))
        rebuild_metadata(root)
        for relative, contents in original_metadata.items():
            if relative not in ('control', 'md5sums') and (root / 'DEBIAN' / relative).read_bytes() != contents:
                raise RuntimeError('unexpected maintainer metadata change: ' + relative)
        control_sha = {str(p.relative_to(root / 'DEBIAN')): sha(p) for p in (root / 'DEBIAN').rglob('*') if p.is_file()}
        version = read_field(root, 'Version')
        target = args.output_dir / (package + '_' + version + '_' + architecture + '.deb')
        env = {**os.environ, 'SOURCE_DATE_EPOCH': str(args.source_epoch)}
        run('dpkg-deb', '--root-owner-group', '--uniform-compression', '-Zxz', '-z6', '--build', root, target, env=env)
        verify = work / ('verified-' + package)
        run('dpkg-deb', '--raw-extract', target, verify)
        if inventory(verify) != after:
            raise RuntimeError('rebuilt archive payload mismatch: ' + package)
        if {str(p.relative_to(verify / 'DEBIAN')): sha(p) for p in (verify / 'DEBIAN').rglob('*') if p.is_file()} != control_sha:
            raise RuntimeError('rebuilt archive metadata mismatch: ' + package)
        manifests[target.name] = {
            'package': package, 'version': version, 'architecture': architecture,
            'depends': read_field(root, 'Depends'), 'sha256': sha(target),
            'base_asset': name, 'base_sha256': BASE[name],
            'payload_file_count': len(after), 'unchanged_file_count': len(after) - len(changed),
            'payload_diff': [{'path': key, 'before': before.get(key), 'after': after.get(key)} for key in changed],
            'control_sha256': control_sha,
        }
    for name, expected in PASSTHROUGH.items():
        shutil.copyfile(args.inputs_dir / name, args.output_dir / name)
        if sha(args.output_dir / name) != expected:
            raise RuntimeError('dependency asset copy mismatch')
    (args.output_dir / 'PACKAGE-DIFF.json').write_text(json.dumps(manifests, indent=2) + '\n')
    assets = []
    for path in sorted(args.output_dir.glob('*.deb')):
        fields = subprocess.check_output(['dpkg-deb', '-f', str(path), 'Package', 'Version', 'Architecture'], text=True)
        values = dict(line.split(': ', 1) for line in fields.splitlines())
        depends = subprocess.check_output(['dpkg-deb', '-f', str(path), 'Depends'], text=True).strip()
        assets.append({'name': path.name, 'size': path.stat().st_size, 'sha256': sha(path), 'pkg': values['Package'], 'version': values['Version'], 'architecture': values['Architecture'], 'depends': depends})
    (args.output_dir / 'ASSETS.json').write_text(json.dumps(assets, indent=2) + '\n')
    (args.output_dir / 'SHA256SUMS').write_text(''.join(item['sha256'] + '  ' + item['name'] + '\n' for item in assets))
    print(json.dumps({'assets': assets, 'verified_overlay_sha256': OVERLAY_SHA, 'source_epoch': args.source_epoch}, indent=2))


if __name__ == '__main__':
    main()
