import pytest

import edid_stereo as es

OTHER_OUI = 0x123456

# 1920x1080@60, 148.5 MHz; flags 0x1A = digital separate sync, no stereo.
DTD_1080P = bytes.fromhex("023a801871382d40582c4500c48e2100001a")


def descriptor(tag: int, text: bytes = b"") -> bytes:
    return bytes([0, 0, 0, tag, 0]) + (text + b"\n").ljust(13, b" ")[:13]


def base_block(extensions: int) -> bytes:
    body = (
        bytes.fromhex("00ffffffffffff00")
        + bytes.fromhex("6229")  # manufacturer "XYZ"
        + bytes.fromhex("3412")  # product code
        + bytes(4)  # no serial
        + bytes([1, 34, 1, 4])  # week 1 of 2024, EDID 1.4
        + bytes([0xA5, 60, 34, 0x78, 0x3B])
        + bytes(10)  # chromaticity
        + bytes(3)  # established timings
        + bytes([1, 1] * 8)  # standard timings unused
        + DTD_1080P
        + descriptor(0xFC, b"TEST MONITOR")
        + descriptor(0x10)
        + descriptor(0x10)
        + bytes([extensions])
    )
    assert len(body) == 127
    return es.with_checksum(body)


def vendor_block(oui: int, payload: bytes) -> bytes:
    data = bytes([oui & 0xFF, oui >> 8 & 0xFF, oui >> 16]) + payload
    return bytes([es.TAG_VENDOR << 5 | len(data)]) + data


def video_block(vics: list[int]) -> bytes:
    return bytes([es.TAG_VIDEO << 5 | len(vics)]) + bytes(vics)


def cta_block(blocks: list[bytes]) -> bytes:
    template = bytes([0x02, 0x03, 4, 0]) + bytes(124)
    return es.build_cta(template, blocks)


def synthetic_edid(blocks: list[bytes]) -> bytes:
    return base_block(1) + cta_block(blocks)


EDID = synthetic_edid([video_block([16, 4, 32]), vendor_block(OTHER_OUI, bytes(4))])
BASE_ONLY = base_block(0)


def all_checksums_ok(edid: bytes) -> bool:
    return all(sum(edid[i : i + 128]) % 256 == 0 for i in range(0, len(edid), 128))


def test_synthetic_edids_are_valid():
    assert len(EDID) == 256 and all_checksums_ok(EDID)
    assert len(BASE_ONLY) == 128 and all_checksums_ok(BASE_ONLY)


def test_cta_roundtrip_is_identity():
    ext = EDID[128:256]
    assert es.build_cta(ext, es.cta_data_blocks(ext)) == ext


def test_hdmi3d_variant():
    out = es.hdmi3d_variant(EDID)
    assert len(out) == 256 and all_checksums_ok(out)
    assert out[:128] == EDID[:128]
    blocks = es.cta_data_blocks(out[128:256])
    vendor = [b for b in blocks if es.block_tag(b) == es.TAG_VENDOR]
    assert [es.vendor_oui(b) for b in vendor] == [es.HDMI_OUI]
    assert es.hdmi_3d_present(vendor[0])
    # Non-vendor blocks are preserved.
    assert [b for b in blocks if es.block_tag(b) != es.TAG_VENDOR] == [video_block([16, 4, 32])]
    # Mandatory HDMI 1.4 3D VICs 4 (720p60) and 32 (1080p24) are advertised.
    assert {4, 32} <= es.video_vics(blocks)


def test_hdmi3d_variant_needs_cta_extension():
    with pytest.raises(es.EdidError):
        es.hdmi3d_variant(BASE_ONLY)


def test_dtd_stereo_variant():
    out = es.dtd_stereo_variant(EDID)
    assert len(out) == 256 and all_checksums_ok(out)
    flags = out[54 + 17]
    assert (flags >> 5) & 3 == 0b01 and flags & 1 == 0  # field sequential, right on sync high
    changed = [i for i in range(256) if out[i] != EDID[i]]
    assert changed == [54 + 17, 127]


def test_build_cta_rejects_overflow():
    full = [vendor_block(OTHER_OUI, bytes(27))] * 4  # 4 * 31 bytes
    with pytest.raises(es.EdidError):
        cta_block(full)


def test_reg_files_target_instance():
    instance = r"DISPLAY\ABC1234\1&2345678&0&UID256"
    apply, revert = es.registry_override(EDID, instance)
    key = rf"[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\{instance}\Device Parameters\EDID_OVERRIDE]"
    assert key in apply and '"0"=hex:00,ff,ff' in apply and '"1"=hex:02,03' in apply
    assert "[-" + key[1:] in revert


def test_video_vics_native_bit_only_for_129_to_192():
    block = bytes([es.TAG_VIDEO << 5 | 3, 0x84, 0xC0, 0xC1])
    assert es.video_vics([block]) == {4, 64, 193}
