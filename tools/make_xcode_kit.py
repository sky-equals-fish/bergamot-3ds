#!/usr/bin/env python3
"""Assemble a self-contained Xcode project for installing Bergamot 3DS on an iPhone or iPad.

The kit carries the prebuilt iPhoneOS core (azahar_libretro.dylib), the runtime and presenter
static libraries, MoltenVK, the app's source, art and licenses. Users open
Bergamot3DS.xcodeproj, choose their team, and press Run; no CMake or Azahar build is needed.

Prerequisites: tools/build_core.py ios, and the player built for iPhoneOS in build/player-ios
(see docs/PLAYER.md). Writes artifacts/xcode-kit/Bergamot3DS and a zip next to it.
"""
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer")
ART = ("control-a", "control-b", "control-x", "control-y", "control-l", "control-r", "control-zl", "control-zr",
       "control-home", "control-select", "control-start", "dpad", "circle", "cstick", "skin-bezel",
       "skin-phone-details", "background", "library-row", "field", "panel", "font", "icon-3ds", "segment")
FRAMEWORKS = ["UIKit", "GameController", "AVFoundation", "UniformTypeIdentifiers", "Metal", "QuartzCore",
              "IOSurface", "CoreGraphics", "Foundation"]


def oid(name):
    return hashlib.md5(name.encode()).hexdigest()[:24].upper()


def platform_of(binary):
    out = subprocess.check_output(["xcrun", "vtool", "-show-build", str(binary)], env=ENV, text=True)
    return next((l.split()[1] for l in out.splitlines() if l.split()[:1] == ["platform"]), None)


def copy_inputs(kit):
    src, lib, res = kit / "Sources", kit / "Libraries", kit / "Resources"
    for d in (src / "include/vulkan", src / "include/vk_video", src / "include/citra_libretro", lib, res / "PixelArt", res / "Licenses"):
        d.mkdir(parents=True, exist_ok=True)
    for f in ("player/ios_player.mm", "player/player_session.h", "player/key_import.h", "ios/pixel_controls.h", "ios/game_art.hpp", "ios/user_keys.h",
              "src/runtime.hpp", "src/vulkan_backend.hpp", "src/metal_presenter.h", "src/display_layout.hpp", "src/frame_pacer.hpp"):
        shutil.copy2(ROOT / f, src / Path(f).name)
    retro = ROOT / "vendor/azahar/externals/libretro-common/libretro-common/include"
    for f in ("libretro.h", "libretro_vulkan.h"):
        shutil.copy2(retro / f, src / "include" / f)
    vk = ROOT / "vendor/azahar/externals/vulkan-headers/include"
    for f in (vk / "vulkan").glob("*.h"):
        shutil.copy2(f, src / "include/vulkan" / f.name)
    for f in (vk / "vk_video").glob("*.h"):
        shutil.copy2(f, src / "include/vk_video" / f.name)
    shutil.copy2(ROOT / "vendor/azahar/src/citra_libretro/ctr_bridge.h", src / "include/citra_libretro/ctr_bridge.h")

    binaries = {
        "azahar_libretro.dylib": ROOT / "build/ios/bin/Release/azahar_libretro.dylib",
        "libctr-runtime.a": ROOT / "build/player-ios/bergamot/runtime/libctr-runtime.a",
        "libctr-metal-presenter.a": ROOT / "build/player-ios/bergamot/runtime/libctr-metal-presenter.a",
        "libMoltenVK.a": ROOT / "vendor/moltenvk-ios/MoltenVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a",
    }
    for name, path in binaries.items():
        if not path.is_file():
            raise SystemExit(f"Missing {path}; build the iPhoneOS core and player first")
        shutil.copy2(path, lib / name)
    if platform_of(lib / "azahar_libretro.dylib") != "IOS":
        raise SystemExit("azahar_libretro.dylib is not an iPhoneOS binary")
    # Re-signed by Xcode on embed; drop the local ad-hoc signature so nothing stale ships.
    subprocess.run(["codesign", "--remove-signature", str(lib / "azahar_libretro.dylib")], env=ENV, check=False)

    exports = ROOT / "ios/PixelArt/exports"
    for png in exports.glob("default-*.png"):
        stem = png.stem[len("default-"):].rsplit("-", 1)[0]
        if stem in ART and png.stem.rsplit("-", 1)[1].isdigit():
            shutil.copy2(png, res / "PixelArt" / png.name)
    for f in (ROOT / "player/Icons").glob("AppIcon*.png"):
        shutil.copy2(f, res / f.name)
    shutil.copy2(ROOT / "player/Icons/fuitlet.png", res / "fuitlet.png")
    for f in ("LICENSE", "THIRD_PARTY_NOTICES.txt"):
        shutil.copy2(ROOT / f, res / "Licenses" / f)
    shutil.copy2(ROOT / "docs/THREEDS_NOTICE.txt", res / "Licenses/NOTICE.txt")

    info = plistlib.loads((ROOT / "player/iOSInfo.plist").read_bytes())
    info["CFBundleIdentifier"] = "$(PRODUCT_BUNDLE_IDENTIFIER)"
    info["CFBundleExecutable"] = "$(EXECUTABLE_NAME)"
    info.pop("UIDeviceFamily", None)   # Xcode derives it from TARGETED_DEVICE_FAMILY
    (kit / "Info.plist").write_bytes(plistlib.dumps(info))


def pbxproj():
    o = []  # (section, id, body)

    def add(section, name, body):
        o.append((section, oid(name), body))
        return oid(name)

    def fileref(name, path, ftype, group="<group>"):
        return add("PBXFileReference", "ref:" + path,
                   f'{{isa = PBXFileReference; lastKnownFileType = {ftype}; name = "{name}"; path = "{path}"; sourceTree = "{group}"; }}')

    src = fileref("ios_player.mm", "Sources/ios_player.mm", "sourcecode.cpp.objcpp")
    plist = fileref("Info.plist", "Info.plist", "text.plist.xml")
    libs = {n: fileref(n, "Libraries/" + n, "compiled.mach-o.dylib" if n.endswith(".dylib") else "archive.ar")
            for n in ("azahar_libretro.dylib", "libctr-runtime.a", "libctr-metal-presenter.a", "libMoltenVK.a")}
    res_items = [("PixelArt", "Resources/PixelArt", "folder"), ("Licenses", "Resources/Licenses", "folder"),
                 ("fuitlet.png", "Resources/fuitlet.png", "image.png")]
    res_items += [(n, "Resources/" + n, "image.png") for n in
                  ("AppIcon60x60@2x.png", "AppIcon60x60@3x.png", "AppIcon76x76@2x~ipad.png", "AppIcon83.5x83.5@2x~ipad.png")]
    res = {n: fileref(n, p, t) for n, p, t in res_items}
    product = add("PBXFileReference", "product",
                  '{isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = Bergamot3DS.app; sourceTree = BUILT_PRODUCTS_DIR; }')

    bf_src = add("PBXBuildFile", "bf:src", f"{{isa = PBXBuildFile; fileRef = {src}; }}")
    bf_libs = [add("PBXBuildFile", "bf:lib:" + n, f"{{isa = PBXBuildFile; fileRef = {r}; }}") for n, r in libs.items()]
    bf_embed = add("PBXBuildFile", "bf:embed",
                   f"{{isa = PBXBuildFile; fileRef = {libs['azahar_libretro.dylib']}; settings = {{ATTRIBUTES = (CodeSignOnCopy, ); }}; }}")
    bf_res = [add("PBXBuildFile", "bf:res:" + n, f"{{isa = PBXBuildFile; fileRef = {r}; }}") for n, r in res.items()]

    ph_src = add("PBXSourcesBuildPhase", "phase:src",
                 f"{{isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = ({bf_src}, ); runOnlyForDeploymentPostprocessing = 0; }}")
    ph_fw = add("PBXFrameworksBuildPhase", "phase:fw",
                f"{{isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = ({', '.join(bf_libs)}, ); runOnlyForDeploymentPostprocessing = 0; }}")
    ph_res = add("PBXResourcesBuildPhase", "phase:res",
                 f"{{isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = ({', '.join(bf_res)}, ); runOnlyForDeploymentPostprocessing = 0; }}")
    ph_embed = add("PBXCopyFilesBuildPhase", "phase:embed",
                   f'{{isa = PBXCopyFilesBuildPhase; buildActionMask = 2147483647; dstPath = ""; dstSubfolderSpec = 10; files = ({bf_embed}, ); name = "Embed Core"; runOnlyForDeploymentPostprocessing = 0; }}')

    g_src = add("PBXGroup", "g:src", f'{{isa = PBXGroup; children = ({src}, {plist}, ); name = Sources; sourceTree = "<group>"; }}')
    g_lib = add("PBXGroup", "g:lib", f'{{isa = PBXGroup; children = ({", ".join(libs.values())}, ); name = Libraries; sourceTree = "<group>"; }}')
    g_res = add("PBXGroup", "g:res", f'{{isa = PBXGroup; children = ({", ".join(res.values())}, ); name = Resources; sourceTree = "<group>"; }}')
    g_prod = add("PBXGroup", "g:prod", f'{{isa = PBXGroup; children = ({product}, ); name = Products; sourceTree = "<group>"; }}')
    g_main = add("PBXGroup", "g:main", f'{{isa = PBXGroup; children = ({g_src}, {g_lib}, {g_res}, {g_prod}, ); sourceTree = "<group>"; }}')

    ldflags = " ".join(f'"-framework", {f},' for f in FRAMEWORKS)
    common_target = (
        'ALWAYS_SEARCH_USER_PATHS = NO; ARCHS = arm64; ASSETCATALOG_COMPILER_APPICON_NAME = ""; CLANG_CXX_LANGUAGE_STANDARD = "c++20"; '
        'CLANG_CXX_LIBRARY = "libc++"; CLANG_ENABLE_OBJC_ARC = YES; CODE_SIGN_STYLE = Automatic; '
        'CURRENT_PROJECT_VERSION = 1; DEVELOPMENT_TEAM = ""; ENABLE_BITCODE = NO; GENERATE_INFOPLIST_FILE = NO; '
        'HEADER_SEARCH_PATHS = ("$(SRCROOT)/Sources", "$(SRCROOT)/Sources/include", ); '
        'INFOPLIST_FILE = Info.plist; IPHONEOS_DEPLOYMENT_TARGET = 18.0; '
        'LD_RUNPATH_SEARCH_PATHS = ("$(inherited)", "@executable_path/Frameworks", ); '
        'LIBRARY_SEARCH_PATHS = ("$(SRCROOT)/Libraries", ); MARKETING_VERSION = 0.1.0; '
        f'OTHER_LDFLAGS = ("$(inherited)", {ldflags} ); '
        # Unique per team, so a free Apple ID can register it without colliding with anyone else's.
        'PRODUCT_BUNDLE_IDENTIFIER = "io.bergamot.threeds.$(DEVELOPMENT_TEAM:lower)"; PRODUCT_NAME = Bergamot3DS; '
        'SDKROOT = iphoneos; SUPPORTED_PLATFORMS = iphoneos; SUPPORTS_MACCATALYST = NO; TARGETED_DEVICE_FAMILY = "1,2"; '
        'WARNING_CFLAGS = ("-Wno-deprecated-declarations", ); '
    )
    t_dbg = add("XCBuildConfiguration", "cfg:t:dbg", f"{{isa = XCBuildConfiguration; buildSettings = {{{common_target}GCC_OPTIMIZATION_LEVEL = 0; }}; name = Debug; }}")
    t_rel = add("XCBuildConfiguration", "cfg:t:rel", f"{{isa = XCBuildConfiguration; buildSettings = {{{common_target}}}; name = Release; }}")
    p_dbg = add("XCBuildConfiguration", "cfg:p:dbg", '{isa = XCBuildConfiguration; buildSettings = {SDKROOT = iphoneos; ONLY_ACTIVE_ARCH = YES; }; name = Debug; }')
    p_rel = add("XCBuildConfiguration", "cfg:p:rel", '{isa = XCBuildConfiguration; buildSettings = {SDKROOT = iphoneos; VALIDATE_PRODUCT = YES; }; name = Release; }')
    t_list = add("XCConfigurationList", "cfgl:t", f"{{isa = XCConfigurationList; buildConfigurations = ({t_dbg}, {t_rel}, ); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }}")
    p_list = add("XCConfigurationList", "cfgl:p", f"{{isa = XCConfigurationList; buildConfigurations = ({p_dbg}, {p_rel}, ); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }}")

    target = add("PBXNativeTarget", "target",
                 f'{{isa = PBXNativeTarget; buildConfigurationList = {t_list}; buildPhases = ({ph_src}, {ph_fw}, {ph_res}, {ph_embed}, ); '
                 f'buildRules = ( ); dependencies = ( ); name = Bergamot3DS; productName = Bergamot3DS; productReference = {product}; '
                 'productType = "com.apple.product-type.application"; }')
    project = add("PBXProject", "project",
                  f'{{isa = PBXProject; attributes = {{BuildIndependentTargetsInParallel = 1; LastUpgradeCheck = 1600; '
                  f'TargetAttributes = {{{target} = {{CreatedOnToolsVersion = 16.0; }}; }}; }}; buildConfigurationList = {p_list}; '
                  f'compatibilityVersion = "Xcode 14.0"; developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en, Base, ); '
                  f'mainGroup = {g_main}; productRefGroup = {g_prod}; projectDirPath = ""; projectRoot = ""; targets = ({target}, ); }}')

    lines = ["// !$*UTF8*$!", "{", "\tarchiveVersion = 1;", "\tclasses = {", "\t};", "\tobjectVersion = 56;", "\tobjects = {"]
    for section in sorted({s for s, _, _ in o}):
        lines.append(f"\n/* Begin {section} section */")
        for s, i, body in o:
            if s == section:
                lines.append(f"\t\t{i} = {body};")
        lines.append(f"/* End {section} section */")
    lines += ["\t};", f"\trootObject = {project};", "}", ""]
    return "\n".join(lines), target


def scheme(target_id):
    ref = (f'<BuildableReference BuildableIdentifier = "primary" BlueprintIdentifier = "{target_id}" '
           'BuildableName = "Bergamot3DS.app" BlueprintName = "Bergamot3DS" ReferencedContainer = "container:Bergamot3DS.xcodeproj"></BuildableReference>')
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion = "1600" version = "1.7">
   <BuildAction parallelizeBuildables = "YES" buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting = "NO" buildForRunning = "YES" buildForProfiling = "YES" buildForArchiving = "YES" buildForAnalyzing = "YES">
            {ref}
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <LaunchAction buildConfiguration = "Release" selectedDebuggerIdentifier = "" selectedLauncherIdentifier = "Xcode.IDEFoundation.Launcher.PosixSpawn" launchStyle = "0" useCustomWorkingDirectory = "NO" ignoresPersistentStateOnLaunch = "NO" debugDocumentVersioning = "YES" debugServiceExtension = "internal" allowLocationSimulation = "NO">
      <BuildableProductRunnable runnableDebuggingMode = "0">
         {ref}
      </BuildableProductRunnable>
   </LaunchAction>
   <ArchiveAction buildConfiguration = "Release" revealArchiveInOrganizer = "YES"></ArchiveAction>
</Scheme>
'''


README = """Bergamot 3DS for iPhone and iPad: Xcode project

1. Open Bergamot3DS.xcodeproj in Xcode (free from the Mac App Store).
2. Select the Bergamot3DS target, open Signing & Capabilities, and pick your team.
   A free Apple ID works; its installs expire after 7 days, so re-run to refresh.
3. Connect your iPhone or iPad (iOS 18 or later), enable Developer Mode, select it
   as the run destination, and press Run.

The emulator core and libraries are prebuilt. The Simulator is not supported for
3DS games. Source for everything here: https://github.com/sky-equals-fish/bergamot-3ds
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/xcode-kit")
    parser.add_argument("--version", default="0.1.0")
    args = parser.parse_args()
    kit = args.output / "Bergamot3DS"
    shutil.rmtree(kit, ignore_errors=True)
    kit.mkdir(parents=True)
    copy_inputs(kit)
    proj = kit / "Bergamot3DS.xcodeproj"
    (proj / "xcshareddata/xcschemes").mkdir(parents=True)
    text, target = pbxproj()
    (proj / "project.pbxproj").write_text(text)
    (proj / "xcshareddata/xcschemes/Bergamot3DS.xcscheme").write_text(scheme(target))
    (kit / "README.txt").write_text(README)
    archive = args.output / f"Bergamot-3DS-{args.version}-ios-xcode.zip"
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(kit.rglob("*")):
            if path.is_file():
                z.write(path, "Bergamot3DS/" + str(path.relative_to(kit)))
    print(f"{archive}  {archive.stat().st_size / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
