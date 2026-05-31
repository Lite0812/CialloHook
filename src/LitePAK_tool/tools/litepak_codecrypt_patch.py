#!/usr/bin/env python3
import argparse
import struct
import sys
from pathlib import Path

BEGIN_MARKER = b"LPKCBEGINv1" + b"\0" * 5
ENCRYPTED_MARKER = b"LPKCENCEDv1" + b"\0" * 5
END_MARKER = b"LPKCENDv1" + b"\0" * 7
DESC_MARKER = b"LPKCRANGEv1" + b"\0" * 5
PUBLIC_SECTION_NAME = b".text\0\0\0"

IMAGE_DOS_SIGNATURE = 0x5A4D
IMAGE_NT_SIGNATURE = 0x00004550
IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_FILE_MACHINE_AMD64 = 0x8664

U32_MASK = 0xFFFFFFFF


def u32(v: int) -> int:
    return v & U32_MASK


def rotl32(v: int, r: int) -> int:
    return u32((v << r) | (v >> (32 - r)))


def mix32(x: int) -> int:
    x = u32(x)
    x = u32(x ^ (x >> 16))
    x = u32(x * 0x7FEB352D)
    x = u32(x ^ (x >> 15))
    x = u32(x * 0x846CA68B)
    x = u32(x ^ (x >> 16))
    return x


def code_seed(rva: int, size: int, machine: int) -> int:
    arch = 0xC0DEC064 if machine == IMAGE_FILE_MACHINE_AMD64 else 0xC0DEC032
    return mix32(0x4C504B43 ^ rva ^ rotl32(size, 7) ^ arch)


def stream_byte(state: int, index: int) -> tuple[int, int]:
    x = u32(state + 0x9E3779B9 + index)
    x = u32(x ^ (x >> 15))
    x = u32(x * 0x2C1B3C6D)
    x = u32(x ^ (x >> 12))
    x = u32(x * 0x297A2D39)
    x = u32(x ^ (x >> 15))
    state = mix32(state ^ x ^ index)
    return state, (x >> ((index & 3) * 8)) & 0xFF


def protect_range(buf: bytearray, offset: int, size: int, rva: int, machine: int) -> None:
    state = code_seed(rva, size, machine)
    for i in range(size):
        state, sb = stream_byte(state, i)
        buf[offset + i] ^= sb


def read_u16(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", buf, offset)[0]


def read_u32(buf: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def write_u32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buf, offset, u32(value))


def parse_pe(buf: bytes | bytearray) -> tuple[int, int, int]:
    if len(buf) < 0x100:
        raise ValueError("PE too small")
    if read_u16(buf, 0) != IMAGE_DOS_SIGNATURE:
        raise ValueError("MZ missing")

    nt = read_u32(buf, 0x3C)
    if nt < 0 or nt + 0x18 >= len(buf):
        raise ValueError("bad e_lfanew")
    if read_u32(buf, nt) != IMAGE_NT_SIGNATURE:
        raise ValueError("PE signature missing")

    machine = read_u16(buf, nt + 4)
    section_count = read_u16(buf, nt + 6)
    optional_size = read_u16(buf, nt + 20)
    section_table = nt + 24 + optional_size
    return machine, section_count, section_table


def find_executable_lpksc(buf: bytes | bytearray) -> tuple[int, int, int, int, int]:
    machine, section_count, section_table = parse_pe(buf)
    for i in range(section_count):
        off = section_table + i * 40
        if off + 40 > len(buf):
            raise ValueError("bad section table")
        name = bytes(buf[off:off + 8]).split(b"\0", 1)[0].decode("ascii", "replace")
        if name != ".lpksc":
            continue
        virtual_size = read_u32(buf, off + 8)
        rva = read_u32(buf, off + 12)
        raw_size = read_u32(buf, off + 16)
        raw_ptr = read_u32(buf, off + 20)
        characteristics = read_u32(buf, off + 36)
        size = virtual_size or raw_size
        if raw_size and raw_size < size:
            size = raw_size
        if raw_ptr + size > len(buf):
            raise ValueError(".lpksc raw range outside file")
        if (characteristics & IMAGE_SCN_CNT_CODE) == 0 or size <= 64:
            continue
        return raw_ptr, size, rva, machine, off
    raise ValueError("missing executable .lpksc section")


def descriptor_candidates(buf: bytes | bytearray) -> list[tuple[int, int, int]]:
    out: list[tuple[int, int, int]] = []
    start = 0
    while True:
        pos = buf.find(DESC_MARKER, start)
        if pos < 0:
            return out
        if pos + 24 <= len(buf):
            out.append((pos, read_u32(buf, pos + 16), read_u32(buf, pos + 20)))
        start = pos + 1


def descriptor_values(buf: bytes | bytearray) -> tuple[int, int] | None:
    for _pos, rva, size in descriptor_candidates(buf):
        if rva and size and rva < 0x10000000 and 64 < size < 0x1000000:
            return rva, size
    return None


def patch_descriptor(buf: bytearray, rva: int, size: int) -> None:
    candidates = descriptor_candidates(buf)
    for pos, old_rva, old_size in candidates:
        if old_rva == 0 and old_size == 0:
            write_u32(buf, pos + 16, rva)
            write_u32(buf, pos + 20, size)
            return
    raise RuntimeError("writable zeroed range descriptor marker not found")


def replace_all(buf: bytearray, old: bytes, new: bytes) -> int:
    if len(old) != len(new):
        raise ValueError("replacement sizes differ")
    count = 0
    start = 0
    while True:
        pos = buf.find(old, start)
        if pos < 0:
            return count
        buf[pos:pos + len(old)] = new
        count += 1
        start = pos + len(new)


def sanitize_private_section_strings(buf: bytearray) -> int:
    count = 0
    count += replace_all(buf, b".lpksc\0\0", PUBLIC_SECTION_NAME)
    count += replace_all(buf, b".lpksc$m", b".text$mn")
    return count


def patch(path: Path) -> str:
    if not path.exists():
        raise FileNotFoundError(f"missing PE: {path}")
    buf = bytearray(path.read_bytes())

    begin_plain = buf.find(BEGIN_MARKER)
    begin_encrypted = buf.find(ENCRYPTED_MARKER)
    end_plain = buf.find(END_MARKER)
    desc = descriptor_values(buf)

    if begin_encrypted >= 0 and end_plain >= 0 and desc and desc[0] and desc[1]:
        sanitized = sanitize_private_section_strings(buf)
        if sanitized:
            path.write_bytes(buf)
            return f"[CodeCrypt] Already encrypted; sanitized private section strings in {path}"
        return f"[CodeCrypt] Already encrypted protected code range: {path}"

    if begin_plain < 0 or end_plain < 0:
        raise RuntimeError("marker not found before encryption state probe")

    raw_ptr, size, rva, machine, section_header = find_executable_lpksc(buf)
    protect_range(buf, raw_ptr, size, rva, machine)
    patch_descriptor(buf, rva, size)

    replaced = replace_all(buf, BEGIN_MARKER, ENCRYPTED_MARKER)
    if replaced <= 0 or buf.find(BEGIN_MARKER) >= 0:
        raise RuntimeError("begin marker still plaintext")

    buf[section_header:section_header + 8] = PUBLIC_SECTION_NAME
    sanitize_private_section_strings(buf)

    path.write_bytes(buf)
    return f"[CodeCrypt] Encrypted protected code range and renamed section in {path}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Encrypt LitePAK protected PE code range")
    parser.add_argument("path", help="PE file to patch")
    args = parser.parse_args()
    path = Path(args.path).resolve()
    try:
        print(patch(path))
        return 0
    except Exception as exc:
        print(f"CodeCrypt patch failed: {exc}: {path}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
