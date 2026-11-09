#!/usr/bin/env python3
"""Export reviewable source-only previews; never publish or package private runtime data."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
COMMON_FILES = {'CMakeLists.txt', 'LICENSE', 'NOTICE.txt', 'THIRD_PARTY_NOTICES.txt',
                'dependencies.json', 'requirements-build.txt', '.gitignore'}
COMMON_TOOLS = {'bootstrap.py', 'build_core.py', 'make_probe.py', 'prepare_source_release.py', 'package_player.py', 'make_xcode_kit.py'}
APP_TOOLS = {'build_app.py', 'build_device.py', 'build_simulator.py', 'build_handheld_cores.py',
             'make_handheld_probes.py', 'test_handheld_runtimes.py'}
DOCS = {'RELEASE_READINESS.md', 'USER_FILES.md', 'THREEDS_SOURCE_PACKAGE.md', 'RUNTIME_AUDIT.md', 'PRESENTATION_SYNC_EXPERIMENT.md',
        'PLAYER.md', 'AZAHAR_PATCHES.md', 'STATUS.md'}
APP_ONLY_DOCS = {'RELEASE_READINESS.md', 'USER_FILES.md', 'RUNTIME_AUDIT.md'}
DENIED = {'.pem', '.p12', '.mobileprovision', '.key', '.keys', '.cia', '.3ds', '.3dsx', '.nds',
          '.gba', '.sav', '.srm', '.dylib', '.a', '.ipa', '.bin'}
AZAHAR_PATCHES = {'0001-libretro-frame-synchronization.patch', '0007-azahar-no-jit-build.patch',
                  '0008-azahar-libretro-async-shaders.patch', '0009-azahar-libretro-shader-preparation.patch',
                  '0010-azahar-batch-cro-cache-invalidation.patch'}
PLAYER_ART = re.compile(r'^default-(control-(a|b|x|y|l|r|zl|zr|home|select|start)|dpad|circle|cstick|skin-bezel|skin-phone-details|background|library-row|field|panel|font|icon-3ds|segment)-[0-9]+\.png$')
RETIRED = re.compile(r'-(icon-(gc|wii|wiiu|switch|ps1|ps2|psp)|control-(plus|minus|cross|circle|square|triangle|z|l1|l2|l3|r1|r2|r3))-')

def selected(relative, component):
    parts = relative.parts
    if any(p in {'vendor', 'build', 'artifacts', 'runtime-data', '.git', '.venv', '__pycache__', 'research', 'Skins'} for p in parts):
        return False
    if len(parts) == 1: return relative.name in COMMON_FILES or (component == 'app' and relative.name == 'handheld-dependencies.json')
    if parts[0] == 'src': return len(parts) == 2 or (component == 'app' and parts[1] == 'handheld')
    if parts[0] == 'tools': return relative.name in COMMON_TOOLS | (APP_TOOLS if component == 'app' else set())
    if parts[0] == 'patches': return relative.name in AZAHAR_PATCHES
    if parts[0] == 'player': return True   # Bergamot 3DS app (Mac + iOS) around the core
    if component == '3ds' and parts[0] == 'ios':
        # Only what the player reuses: the 3DS touch controls and their default-palette art.
        return relative.as_posix() in ('ios/pixel_controls.h', 'ios/game_art.hpp', 'ios/user_keys.h') or (len(parts) == 4 and parts[2] == 'exports' and PLAYER_ART.match(relative.name) is not None)
    if parts[0] == 'docs':
        if len(parts) == 3 and parts[1] == 'screenshots':
            return component == 'app' and relative.name in {'README.md', 'home.png', '3ds-controls.png', 'ds-controls.png', 'gba-controls.png'}
        if len(parts) == 3 and parts[1] == 'art':
            return relative.name == 'fuitlet.png'
        if component == '3ds' and relative.name in APP_ONLY_DOCS: return False   # Bergamot front end, incl. DS/GBA
        return len(parts) == 2 and relative.name in DOCS
    if parts[0] == 'systems': return parts[1] == '3ds' or (len(parts) > 2 and parts[1:3] == ('examples', 'three-ds'))
    if parts[0] == 'tests': return len(parts) == 2 or (component == 'app' and parts[1] in {'handheld', 'ios'})
    if component == 'app' and parts[0] == 'cmake': return relative.name == 'BergamontHandheld.cmake'
    if component == 'app' and parts[0] == 'ios':
        if len(parts) == 2: return True
        return parts[1] == 'PixelArt' and not RETIRED.search(relative.name) and '-sheet.' not in relative.name
    return False

def export(component, output):
    # Start from source-controlled files plus explicit new source files, not a recursive disk archive.
    names = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '.']).split(b'\0')
    files = {}
    for raw in names:
        if not raw: continue
        relative = Path(raw.decode())
        if not selected(relative, component): continue
        path = ROOT / relative
        if not path.is_file() or path.is_symlink(): raise RuntimeError('Non-regular source: ' + str(relative))
        if relative.suffix.lower() in DENIED: raise RuntimeError('Forbidden release extension: ' + str(relative))
        data = path.read_bytes()
        if re.search(rb'(?m)^-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\r?$', data):
            raise RuntimeError('Private signing key marker in ' + str(relative))
        files[relative.as_posix()] = data
    required = COMMON_FILES - {'.gitignore'}
    if not required.issubset(files): raise RuntimeError('Missing source files: ' + repr(required - files.keys()))
    if component == 'app':
        files['README.md'] = (ROOT / 'README.md').read_bytes()
    else:
        files['README.md'] = (ROOT / 'docs/THREEDS_REPO_README.md').read_bytes()
        files['NOTICE.txt'] = (ROOT / 'docs/THREEDS_NOTICE.txt').read_bytes()   # 3DS-only credits
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    manifest = {'component': component, 'format': 'source-only-preview', 'base_revision': revision,
                'working_tree_snapshot': True, 'binary_release_approved': False,
                'dependencies_bundled': False,
                'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}}
    files['SOURCE_MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / ('bergamot-' + component + '-source-preview.tar.gz')
    with tarfile.open(archive, 'w:gz') as tar:
        for name, data in sorted(files.items()):
            info = tarfile.TarInfo(name); info.size = len(data); info.mtime = 0; info.mode = 0o644
            tar.addfile(info, io.BytesIO(data))
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            if member.name == 'SOURCE_MANIFEST.json': continue
            assert hashlib.sha256(tar.extractfile(member).read()).hexdigest() == manifest['files'][member.name]
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (archive.with_suffix(archive.suffix + '.sha256')).write_text(digest + '  ' + archive.name + '\n')
    print(json.dumps({'archive': str(archive), 'files': len(files), 'bytes': archive.stat().st_size,
                      'sha256': digest, 'binary_release_approved': False}))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--component', choices=['app', '3ds'], required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/release-preview')
    args = parser.parse_args(); export(args.component, args.output)
