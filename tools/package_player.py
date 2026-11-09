#!/usr/bin/env python3
"""Package the Bergamot 3DS player: Mac zip, unsigned iPhone IPA and corresponding source.

Builds are taken from build/player-macos and build/player-ios (run the player CMake first).
Nothing is uploaded. No games, keys, saves or signing material are read or packaged.
"""
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer")
PATCHES = ["0001-libretro-frame-synchronization.patch", "0007-azahar-no-jit-build.patch",
           "0008-azahar-libretro-async-shaders.patch", "0009-azahar-libretro-shader-preparation.patch",
           "0010-azahar-batch-cro-cache-invalidation.patch"]
DENIED = {".cia", ".3ds", ".cci", ".cxi", ".3dsx", ".sav", ".bin", ".p12", ".pem", ".mobileprovision"}

def run(*args):
    subprocess.run([str(a) for a in args], check=True, env=ENV)

def platform_of(binary):
    out = subprocess.check_output(["xcrun", "vtool", "-show-build", str(binary)], env=ENV, text=True)
    return next((l.split()[1] for l in out.splitlines() if l.split()[:1] == ["platform"]), None)

def check_bundle(app):
    for path in app.rglob("*"):
        if path.is_file() and path.suffix.lower() in DENIED:
            raise SystemExit(f"Refusing to package {path.name}: looks like game, save or signing data")

def package_mac(version, out, identity):
    app = ROOT / "build/player-macos/Bergamot 3DS.app"
    if not app.is_dir(): raise SystemExit("Build the Mac player first (build/player-macos)")
    stage = out / "stage-mac"; shutil.rmtree(stage, ignore_errors=True); stage.mkdir(parents=True)
    staged = stage / app.name
    shutil.copytree(app, staged, symlinks=True)
    check_bundle(staged)
    for binary in (staged / "Contents/MacOS/Bergamot 3DS", staged / "Contents/Frameworks/azahar_libretro.dylib"):
        if platform_of(binary) != "MACOS": raise SystemExit(f"Not a macOS binary: {binary}")
    sign = ["--options", "runtime", "--timestamp"] if identity != "-" else []
    run("codesign", "--force", *sign, "--sign", identity, staged / "Contents/Frameworks/azahar_libretro.dylib")
    run("codesign", "--force", *sign, "--sign", identity, staged)
    run("codesign", "--verify", "--deep", "--strict", staged)
    target = out / f"Bergamot-3DS-{version}-mac.zip"
    target.unlink(missing_ok=True)
    run("ditto", "-c", "-k", "--keepParent", staged, target)
    shutil.rmtree(stage)
    return target

def package_ios(version, out):
    app = ROOT / "build/player-ios/Bergamot3DS.app"
    if not app.is_dir(): raise SystemExit("Build the iPhone player first (build/player-ios)")
    check_bundle(app)
    for binary in (app / "Bergamot3DS", app / "Frameworks/azahar_libretro.dylib"):
        if platform_of(binary) != "IOS": raise SystemExit(f"Not an iPhoneOS binary: {binary}")
    target = out / f"Bergamot-3DS-{version}-ios-unsigned.ipa"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as ipa:
        for path in sorted(app.rglob("*")):
            if path.is_file() and path.name != "_CodeSignature" and "_CodeSignature" not in path.parts:
                ipa.write(path, "Payload/Bergamot3DS.app/" + str(path.relative_to(app)))
    return target

def package_source(version, out):
    """Exact corresponding source: Bergamot's player, runtime and patches plus the pinned,
    patched Azahar tree with its submodules, as built."""
    target = out / f"Bergamot-3DS-{version}-source.tar.gz"
    prefix = f"bergamot-3ds-{version}-source"
    names = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "--cached", "--others", "--exclude-standard", "-z", "."]).split(b"\0")
    keep_top = {"CMakeLists.txt", "LICENSE", "NOTICE.txt", "THIRD_PARTY_NOTICES.txt", "dependencies.json", "requirements-build.txt"}
    tools = {"bootstrap.py", "build_core.py", "make_probe.py", "package_player.py"}
    art = ("default-control-", "default-dpad-", "default-circle-", "default-cstick-", "default-skin-bezel-", "default-skin-phone-details-", "default-background-")
    def wanted(rel):
        p = rel.parts
        if len(p) == 1: return rel.name in keep_top
        if p[0] == "src": return len(p) == 2
        if p[0] in ("player", "systems"): return p[0] == "player" or p[1] == "3ds" or p[1:3] == ("examples", "three-ds")
        if p[0] == "patches": return rel.name in PATCHES
        if p[0] == "tools": return rel.name in tools
        if p[0] == "tests": return len(p) == 2
        if p[0] == "ios": return rel.name == "pixel_controls.h" or (len(p) == 4 and p[2] == "exports" and rel.name.startswith(art))
        return False
    with tarfile.open(target, "w:gz") as tar:
        for raw in names:
            if not raw: continue
            rel = Path(raw.decode())
            if wanted(rel) and (ROOT / rel).is_file():
                if rel.suffix.lower() in DENIED: raise SystemExit(f"Refusing to package {rel}")
                tar.add(ROOT / rel, f"{prefix}/{rel}")
        azahar = ROOT / "vendor/azahar"
        def skip_git(info):
            parts = Path(info.name).parts
            return None if ".git" in parts or info.name.endswith("/.git") else info
        tar.add(azahar, f"{prefix}/vendor/azahar", filter=skip_git)
        note = (f"Bergamot 3DS {version} corresponding source.\n\n"
                "vendor/azahar is the pinned Azahar commit from dependencies.json with its submodules,\n"
                "with patches/0001 and 0007-0010 already applied, exactly as built.\n"
                "MoltenVK (Apache-2.0) is the unmodified release named in dependencies.json.\n").encode()
        info = tarfile.TarInfo(f"{prefix}/SOURCE.txt"); info.size = len(note)
        import io; tar.addfile(info, io.BytesIO(note))
    return target

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/player-release")
    parser.add_argument("--mac", action="store_true"); parser.add_argument("--ios", action="store_true")
    parser.add_argument("--source", action="store_true")
    parser.add_argument("--identity", default="-", help="codesign identity for the Mac app; '-' is ad hoc")
    args = parser.parse_args()
    version = plistlib.loads((ROOT / "player/MacInfo.plist").read_bytes())["CFBundleShortVersionString"]
    args.output.mkdir(parents=True, exist_ok=True)
    made = []
    if args.mac: made.append(package_mac(version, args.output, args.identity))
    if args.ios: made.append(package_ios(version, args.output))
    if args.source: made.append(package_source(version, args.output))
    sums = args.output / "SHA256SUMS"
    lines = {l.split("  ", 1)[1]: l for l in sums.read_text().splitlines()} if sums.exists() else {}
    for f in made:
        lines[f.name] = hashlib.sha256(f.read_bytes()).hexdigest() + "  " + f.name
        print(f"{f.name}  {f.stat().st_size / 1e6:.1f} MB")
    sums.write_text("\n".join(lines[k] for k in sorted(lines)) + "\n")

if __name__ == "__main__":
    main()
