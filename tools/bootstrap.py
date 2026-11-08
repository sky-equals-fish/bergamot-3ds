#!/usr/bin/env python3
"""Fetch pinned dependencies into this project; preserve existing checkouts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def run(*args):
    subprocess.run([str(a) for a in args], check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", action="store_true", help="Also fetch simulator Metal libraries")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "dependencies.json").read_text())
    (ROOT / "vendor").mkdir(exist_ok=True)
    (ROOT / "artifacts").mkdir(exist_ok=True)
    if not (ROOT / ".venv/bin/python3").exists():
        run(sys.executable, "-m", "venv", ROOT / ".venv")
    run(ROOT / ".venv/bin/python3", "-m", "pip", "install", "-r", ROOT / "requirements-build.txt")
    source = ROOT / "vendor/azahar"
    pin = manifest["azahar"]["commit"]
    if not source.exists():
        run("git", "init", source)
        run("git", "-C", source, "remote", "add", "origin", manifest["azahar"]["url"])
        run("git", "-C", source, "fetch", "--depth", "1", "origin", pin)
        run("git", "-C", source, "checkout", "--detach", pin)
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != pin:
        raise SystemExit("Existing Azahar checkout has a different HEAD; leaving it untouched")
    run("git", "-C", source, "remote", "set-url", "--push", "origin", "LOCAL_ONLY_PUSH_DISABLED")
    run("git", "-C", source, "submodule", "update", "--init", "--recursive", "--depth", "1", "--jobs", "6")
    run("git", "-C", source, "submodule", "foreach", "--recursive", "git remote set-url --push origin LOCAL_ONLY_PUSH_DISABLED")
    for relative in [manifest["azahar"]["patch"], *manifest["azahar"].get("additional_patches", [])]:
        patch = ROOT / relative
        reverse = subprocess.run(["git", "-C", source, "apply", "--reverse", "--check", str(patch)], capture_output=True)
        if reverse.returncode:
            run("git", "-C", source, "apply", "--check", patch)
            run("git", "-C", source, "apply", patch)
    for platform in ("macos", "ios") + (("ios-simulator",) if args.simulator else ()):
        archive_platform = "all" if platform == "ios-simulator" else platform
        archive = ROOT / "artifacts" / f"MoltenVK-{archive_platform}.tar"
        if not archive.exists():
            url = f'https://github.com/KhronosGroup/MoltenVK/releases/download/v{manifest["moltenvk"]["version"]}/{archive.name}'
            urllib.request.urlretrieve(url, archive)
        actual = hashlib.sha256(archive.read_bytes()).hexdigest()
        if actual != manifest["moltenvk"][f"{archive_platform}_sha256"]:
            raise SystemExit(f"Checksum mismatch: {archive}")
        destination = ROOT / "vendor" / f"moltenvk-{platform}"
        if not destination.exists():
            destination.mkdir()
            with tarfile.open(archive) as tar:
                for member in tar.getmembers():
                    resolved = (destination / member.name).resolve()
                    if destination.resolve() not in resolved.parents and resolved != destination.resolve():
                        raise SystemExit("Unsafe archive path")
                    if member.issym() or member.islnk():
                        base = resolved.parent if member.issym() else destination
                        link = (base / member.linkname).resolve()
                        if destination.resolve() not in link.parents:
                            raise SystemExit("Unsafe archive link")
                tar.extractall(destination)
    run(sys.executable, ROOT / "tools/make_probe.py")

if __name__ == "__main__":
    main()
