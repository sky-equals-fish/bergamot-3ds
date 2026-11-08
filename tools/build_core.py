#!/usr/bin/env python3
"""Build the pinned, shared Azahar core; never consumes or embeds game files."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = "6b4e7f2e694514fdf2f72dc20368609b7cd4912e"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("platform", choices=["macos", "ios", "ios-simulator"])
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    source = ROOT / "vendor/azahar"
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != PIN:
        raise SystemExit(f"Source pin mismatch: expected {PIN}, got {actual}")
    env = dict(os.environ, DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer")
    env.pop("SDKROOT", None)
    env["PATH"] = str(ROOT / ".venv/bin") + os.pathsep + env["PATH"]
    build = ROOT / "build" / args.platform
    flags = ["-G", "Ninja", "-DENABLE_LIBRETRO=ON", "-DENABLE_TESTS=OFF",
             "-DBERGAMOT_NO_JIT=ON", "-DENABLE_BUILTIN_KEYBLOB=OFF", "-DENABLE_LTO=OFF",
             "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64",
             "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]
    if args.platform in ("ios", "ios-simulator"):
        sdk = subprocess.check_output(["xcrun", "--sdk", "iphonesimulator" if args.platform == "ios-simulator" else "iphoneos", "--show-sdk-path"], env=env, text=True).strip()
        flags += ["-DIOS=ON", "-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0",
                  "-DCMAKE_OSX_SYSROOT=" + sdk,
                  "-DCMAKE_C_FLAGS=-DIOS", "-DCMAKE_CXX_FLAGS=-DIOS",
                  "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCITRA_USE_PRECOMPILED_HEADERS=OFF",
                  "-DENABLE_OPT=OFF"]
    subprocess.run(["cmake", "--fresh", "-S", str(source), "-B", str(build), *flags], env=env, check=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "azahar_libretro", "-j", str(args.jobs)], env=env, check=True)
    artifact = build / "bin/Release/azahar_libretro.dylib"
    if not artifact.is_file():
        raise SystemExit("Build returned success without the expected core")
    platform_info = subprocess.check_output(["xcrun", "vtool", "-show-build", str(artifact)], env=env, text=True)
    expected_platform = {"ios": "IOS", "ios-simulator": "IOSSIMULATOR", "macos": "MACOS"}[args.platform]
    if not any(line.split() == ["platform", expected_platform] for line in platform_info.splitlines()):
        raise SystemExit("Wrong Mach-O target platform:\n" + platform_info)
    report = {"source": "https://github.com/azahar-emu/azahar", "commit": actual,
              "platform": args.platform, "artifact": str(artifact),
              "builtin_keyblob": False, "native_jit_components": False, "gameplay_verified": False}
    (build / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(artifact)

if __name__ == "__main__":
    main()
