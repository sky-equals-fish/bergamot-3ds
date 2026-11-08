#!/usr/bin/env python3
"""Generate original ARM homebrew: debug marker through SVC, then sleep forever.

No SDK or copyrighted game payload. The marker proves guest ARM instructions
reached the emulated kernel; this probe does not exercise PICA rendering.
"""
from pathlib import Path
import struct
import subprocess
import os

root = Path(__file__).resolve().parents[1]
(root / "artifacts").mkdir(exist_ok=True)
obj = root / "artifacts/arm11-probe.o"
env = dict(os.environ, DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer")
env.pop("SDKROOT", None)
subprocess.run(["xcrun", "clang", "-target", "armv6k-none-eabi", "-c",
                str(root / "tests/arm11_probe.S"), "-o", str(obj)], env=env, check=True)
elf = obj.read_bytes()
if elf[:7] != b"\x7fELF\x01\x01\x01":
    raise SystemExit("Expected a little-endian ELF32 object")
section_offset = struct.unpack_from("<I", elf, 32)[0]
section_size, section_count, names_index = struct.unpack_from("<HHH", elf, 46)
sections = [struct.unpack_from("<10I", elf, section_offset + i * section_size) for i in range(section_count)]
names_section = sections[names_index]
names = elf[names_section[4]:names_section[4] + names_section[5]]
code = None
for section in sections:
    name = names[section[0]:].split(b"\0", 1)[0]
    if name == b".text": code = elf[section[4]:section[4] + section[5]]
    if name in (b".rel.text", b".rela.text") and section[5]:
        raise SystemExit("Probe contains unresolved relocations")
if not code: raise SystemExit("Object has no .text")
code += bytes((-len(code)) % 4)
# Extended 0x2c header and two relocation counts per segment, all zero.
header = struct.pack("<IHH9I", 0x58534433, 0x2C, 8, 0, 0, len(code), 4, 4, 0, 0, 0, 0)
out = root / "artifacts/arm11-probe.3dsx"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(header + bytes(24) + code + bytes(8))
print(out)
