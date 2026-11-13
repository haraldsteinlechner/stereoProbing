"""Writes stereo-enabling variants of a monitor EDID for driver experiments.

Variants:
  hdmi3d      CTA extension: drop non-HDMI vendor blocks, add an HDMI 1.4 VSDB
              with 3D_present=1 (mandatory 3D formats, HDMI spec 8.3.2).
  dtd-stereo  Base block DTD 1: stereo bits = field sequential, right on sync high.

Usage:
  uv run --no-project edid_stereo.py IN.bin OUT_DIR [--instance "DISPLAY\\MODEL\\INSTANCE"]

With --instance, also writes <name>_<variant>_apply.reg / _revert.reg for the
Windows EDID_OVERRIDE registry key (Device Parameters\\EDID_OVERRIDE, values "0",
"1", ... per 128-byte block). The .bin files can be loaded into the AMD
driver's EDID emulation instead.
"""

import argparse
import sys
from pathlib import Path

TAG_VENDOR = 3
TAG_VIDEO = 2
HDMI_OUI = 0x000C03
BLOCK = 128


class EdidError(Exception):
    pass


def with_checksum(block: bytes) -> bytes:
    body = bytes(block[:127])
    return body + bytes([(-sum(body)) % 256])


def block_tag(block: bytes) -> int:
    return block[0] >> 5


def vendor_oui(block: bytes) -> int:
    return block[1] | block[2] << 8 | block[3] << 16


def cta_data_blocks(ext: bytes) -> list[bytes]:
    if ext[0] != 0x02:
        raise EdidError(f"not a CTA-861 extension (tag 0x{ext[0]:02x})")
    end = ext[2]
    blocks = []
    i = 4
    while i < end:
        length = ext[i] & 0x1F
        blocks.append(bytes(ext[i : i + 1 + length]))
        i += 1 + length
    return blocks


def cta_dtds(ext: bytes) -> list[bytes]:
    dtds = []
    i = ext[2]
    while i + 18 <= 127 and (ext[i] | ext[i + 1]) != 0:
        dtds.append(bytes(ext[i : i + 18]))
        i += 18
    return dtds


def build_cta(ext: bytes, blocks: list[bytes]) -> bytes:
    data = b"".join(blocks)
    dtds = b"".join(cta_dtds(ext))
    offset = 4 + len(data)
    if offset + len(dtds) > 127:
        raise EdidError(f"CTA extension overflow: {offset + len(dtds)} > 127 bytes")
    body = bytes([0x02, ext[1], offset, ext[3]]) + data + dtds
    return with_checksum(body + bytes(127 - len(body)))


def video_vics(blocks: list[bytes]) -> set[int]:
    vics = set()
    for b in blocks:
        if block_tag(b) == TAG_VIDEO:
            vics.update(v & 0x7F if 129 <= v <= 192 else v for v in b[1:])
    return vics


def hdmi_vsdb_3d() -> bytes:
    payload = bytes([
        0x03, 0x0C, 0x00,  # HDMI IEEE OUI, LSB first
        0x10, 0x00,        # CEC physical address 1.0.0.0
        0x00,              # no deep color / AI
        0x00,              # max TMDS clock not indicated
        0x20,              # HDMI_Video_present
        0x80,              # 3D_present, no 3D_Multi, no image size info
        0x00,              # HDMI_VIC_LEN=0, HDMI_3D_LEN=0
    ])
    return bytes([TAG_VENDOR << 5 | len(payload)]) + payload


def hdmi_3d_present(block: bytes) -> bool:
    if block_tag(block) != TAG_VENDOR or vendor_oui(block) != HDMI_OUI or len(block) < 9:
        return False
    flags = block[8]
    if not flags & 0x20:
        return False
    i = 9 + (2 if flags & 0x80 else 0) + (2 if flags & 0x40 else 0)
    return i < len(block) and bool(block[i] & 0x80)


def hdmi3d_variant(edid: bytes) -> bytes:
    if edid[126] == 0 or len(edid) < 2 * BLOCK:
        raise EdidError("EDID has no extension block")
    ext = edid[BLOCK : 2 * BLOCK]
    blocks = [b for b in cta_data_blocks(ext) if block_tag(b) != TAG_VENDOR]
    blocks.append(hdmi_vsdb_3d())
    return edid[:BLOCK] + build_cta(ext, blocks) + edid[2 * BLOCK :]


def dtd_stereo_variant(edid: bytes) -> bytes:
    base = bytearray(edid[:BLOCK])
    if base[54] | base[55] == 0:
        raise EdidError("first descriptor is not a detailed timing")
    flags = base[54 + 17]
    base[54 + 17] = (flags & ~0x61) | 0x20
    return with_checksum(bytes(base)) + edid[BLOCK:]


def registry_override(edid: bytes, instance: str) -> tuple[str, str]:
    key = rf"HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\{instance}\Device Parameters\EDID_OVERRIDE"
    values = []
    for n in range(len(edid) // BLOCK):
        block = edid[n * BLOCK : (n + 1) * BLOCK]
        values.append(f'"{n}"=hex:' + ",".join(f"{b:02x}" for b in block))
    header = "Windows Registry Editor Version 5.00\r\n\r\n"
    apply = header + f"[{key}]\r\n" + "\r\n".join(values) + "\r\n"
    revert = header + f"[-{key}]\r\n"
    return apply, revert


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--instance", help=r"monitor instance, e.g. DISPLAY\ABC1234\1&2345678&0&UID256")
    args = parser.parse_args()

    edid = args.input.read_bytes()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for name, make in (("hdmi3d", hdmi3d_variant), ("dtd-stereo", dtd_stereo_variant)):
        try:
            out = make(edid)
        except EdidError as e:
            print(f"{name}: skipped: {e}")
            continue
        path = args.out_dir / f"{args.input.stem}_{name}.bin"
        path.write_bytes(out)
        print(f"{name}: {path}")
        if args.instance:
            apply, revert = registry_override(out, args.instance)
            for suffix, text in (("apply", apply), ("revert", revert)):
                reg = args.out_dir / f"{args.input.stem}_{name}_{suffix}.reg"
                reg.write_bytes(text.encode("utf-16"))
                print(f"  {reg}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
