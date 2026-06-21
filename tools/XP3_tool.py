from __future__ import annotations

import argparse
import os
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"
HEADER_SIZE = len(MAGIC) + 8
INDEX_RAW = 0
INDEX_ZLIB = 1
INDEX_ENCODE_MASK = 0x07
INDEX_CONTINUE = 0x80
SEG_RAW = 0
SEG_ZLIB = 1
CHUNK_FILE = b"File"
CHUNK_INFO = b"info"
CHUNK_SEGM = b"segm"


@dataclass
class Xp3Segment:
    flags: int
    offset: int
    original_size: int
    stored_size: int


@dataclass
class Xp3Entry:
    name: str
    flags: int
    original_size: int
    stored_size: int
    segments: list[Xp3Segment]


def emit(message: str) -> None:
    print(message, flush=True)


def format_size(size: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    value = float(size)
    for unit in units:
        if value < 1024 or unit == units[-1]:
            return f"{value:.2f}{unit}" if unit != "B" else f"{size}B"
        value /= 1024
    return f"{size}B"


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def chunk(kind: bytes, payload: bytes) -> bytes:
    return kind + struct.pack("<Q", len(payload)) + payload


def normalize_name(name: str) -> str:
    normalized = name.replace("\\", "/").strip("/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    parts: list[str] = []
    for part in normalized.split("/"):
        if not part or part == ".":
            continue
        if part == "..":
            raise ValueError(f"XP3 路径不允许包含 ..: {name}")
        parts.append(part)
    if not parts:
        raise ValueError("XP3 路径不能为空")
    return "/".join(parts)


def normalized_key(name: str) -> str:
    return normalize_name(name).replace("/", "\\").lower()


def safe_output_path(output_dir: Path, name: str) -> Path:
    relative = normalize_name(name)
    target = output_dir.joinpath(*relative.split("/"))
    root = output_dir.resolve()
    parent = target.parent
    parent.mkdir(parents=True, exist_ok=True)
    resolved_parent = parent.resolve()
    if root != resolved_parent and root not in resolved_parent.parents:
        raise ValueError(f"XP3 输出路径越界: {name}")
    return target


def iter_files(input_dir: Path) -> list[Path]:
    files = [path for path in input_dir.rglob("*") if path.is_file()]
    return sorted(files, key=lambda path: path.relative_to(input_dir).as_posix().lower())


def compress_payload(data: bytes, compression: str) -> tuple[int, bytes]:
    mode = compression.lower()
    if mode == "raw":
        return SEG_RAW, data
    if mode == "zlib":
        return SEG_ZLIB, zlib.compress(data, 9)
    if mode == "auto":
        packed = zlib.compress(data, 9)
        if len(packed) < len(data):
            return SEG_ZLIB, packed
        return SEG_RAW, data
    raise ValueError("XP3 仅支持 raw / zlib / auto 压缩")


def build_file_chunk(name: str, original_size: int, stored_size: int, segments: list[Xp3Segment]) -> bytes:
    name_utf16 = name.replace("/", "\\").encode("utf-16le")
    name_len = len(name_utf16) // 2
    if name_len > 0xFFFF:
        raise ValueError(f"文件名太长: {name}")
    info_payload = (
        struct.pack("<IQQH", 0, original_size, stored_size, name_len)
        + name_utf16
    )
    seg_payload = b"".join(
        struct.pack("<IQQQ", segment.flags, segment.offset, segment.original_size, segment.stored_size)
        for segment in segments
    )
    return chunk(CHUNK_FILE, chunk(CHUNK_INFO, info_payload) + chunk(CHUNK_SEGM, seg_payload))


def encode_index(index_data: bytes, mode: str) -> bytes:
    mode = mode.lower()
    if mode == "raw":
        return bytes([INDEX_RAW]) + struct.pack("<Q", len(index_data)) + index_data
    if mode in {"zlib", "auto"}:
        packed = zlib.compress(index_data, 9)
        if mode == "zlib" or len(packed) + 8 < len(index_data):
            return bytes([INDEX_ZLIB]) + struct.pack("<Q", len(packed)) + struct.pack("<Q", len(index_data)) + packed
        return bytes([INDEX_RAW]) + struct.pack("<Q", len(index_data)) + index_data
    raise ValueError("XP3 索引仅支持 raw / zlib / auto")


def clear_dir(path: Path) -> None:
    if not path.exists():
        return
    for child in sorted(path.rglob("*"), reverse=True):
        if child.is_file() or child.is_symlink():
            child.unlink()
        elif child.is_dir():
            child.rmdir()


def pack(input_dir: Path, pak_path: Path, compression: str = "auto", index_compression: str = "auto") -> None:
    if not input_dir.is_dir():
        raise ValueError(f"输入目录不存在: {input_dir}")
    files = iter_files(input_dir)
    if not files:
        raise ValueError("输入目录没有可封包文件")

    pak_path.parent.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    index_chunks: list[bytes] = []
    logical_size = 0

    with pak_path.open("wb") as fp:
        fp.write(MAGIC)
        fp.write(b"\0" * 8)
        for index, file_path in enumerate(files, 1):
            rel_name = normalize_name(file_path.relative_to(input_dir).as_posix())
            data = file_path.read_bytes()
            flags, stored = compress_payload(data, compression)
            offset = fp.tell()
            fp.write(stored)
            segment = Xp3Segment(flags, offset, len(data), len(stored))
            index_chunks.append(build_file_chunk(rel_name, len(data), len(stored), [segment]))
            logical_size += len(data)
            emit(f"[{index}/{len(files)}] 写入 {rel_name} | mode={'zlib' if flags else 'raw'} | {format_size(len(stored))}/{format_size(len(data))}")

        index_offset = fp.tell()
        index_data = b"".join(index_chunks)
        fp.write(encode_index(index_data, index_compression))
        fp.seek(len(MAGIC))
        fp.write(struct.pack("<Q", index_offset))

    elapsed = time.perf_counter() - start
    emit(
        f"XP3 封包汇总: 文件数={len(files)} | 原始={format_size(logical_size)} | "
        f"封包={format_size(pak_path.stat().st_size)} | 耗时={elapsed:.3f}s"
    )


def parse_file_chunk(payload: bytes) -> Xp3Entry:
    pos = 0
    name = ""
    flags = 0
    original_size = 0
    stored_size = 0
    segments: list[Xp3Segment] = []
    while pos + 12 <= len(payload):
        kind = payload[pos:pos + 4]
        size = read_u64(payload, pos + 4)
        pos += 12
        if size > len(payload) - pos:
            raise ValueError("XP3 File 子块尺寸越界")
        body = payload[pos:pos + size]
        pos += size
        if kind == CHUNK_INFO:
            if len(body) < 22:
                raise ValueError("XP3 info 子块太小")
            flags = read_u32(body, 0) & 0xFF
            original_size = read_u64(body, 4)
            stored_size = read_u64(body, 12)
            name_len = read_u16(body, 20)
            name_bytes = name_len * 2
            if len(body) < 22 + name_bytes:
                raise ValueError("XP3 info 文件名越界")
            name = body[22:22 + name_bytes].decode("utf-16le", errors="replace").replace("\\", "/")
            name = normalize_name(name)
        elif kind == CHUNK_SEGM:
            if len(body) % 28 != 0:
                raise ValueError("XP3 segm 子块尺寸非法")
            for seg_pos in range(0, len(body), 28):
                segments.append(
                    Xp3Segment(
                        read_u32(body, seg_pos),
                        read_u64(body, seg_pos + 4),
                        read_u64(body, seg_pos + 12),
                        read_u64(body, seg_pos + 20),
                    )
                )
    if pos != len(payload) or not name or not segments:
        raise ValueError("XP3 File 块不完整")
    if original_size == 0:
        original_size = sum(segment.original_size for segment in segments)
    if stored_size == 0:
        stored_size = sum(segment.stored_size for segment in segments)
    return Xp3Entry(name, flags, original_size, stored_size, segments)


def read_index_at(fp, index_offset: int) -> bytes:
    while True:
        fp.seek(index_offset)
        header = fp.read(9)
        if len(header) != 9:
            raise ValueError("XP3 索引头读取失败")
        flags = header[0]
        index_size = read_u64(header, 1)
        if flags & INDEX_CONTINUE:
            if (flags & INDEX_ENCODE_MASK) != INDEX_RAW or index_size != 0:
                raise ValueError(f"不支持的 XP3 continue 索引 flags=0x{flags:02X}")
            next_offset = fp.read(8)
            if len(next_offset) != 8:
                raise ValueError("XP3 continue 索引缺少下一段偏移")
            index_offset = read_u64(next_offset, 0)
            continue

        encode = flags & INDEX_ENCODE_MASK
        if encode == INDEX_RAW:
            data = fp.read(index_size)
            if len(data) != index_size:
                raise ValueError("XP3 raw 索引读取失败")
            return data
        if encode == INDEX_ZLIB:
            raw_size_bytes = fp.read(8)
            if len(raw_size_bytes) != 8:
                raise ValueError("XP3 zlib 索引缺少解压后尺寸")
            raw_size = read_u64(raw_size_bytes, 0)
            packed = fp.read(index_size)
            if len(packed) != index_size:
                raise ValueError("XP3 zlib 索引读取失败")
            data = zlib.decompress(packed)
            if len(data) != raw_size:
                raise ValueError(f"XP3 索引解压尺寸不匹配: {len(data)} != {raw_size}")
            return data
        raise ValueError(f"不支持的 XP3 索引编码 flags=0x{flags:02X}")


def load_index(pak_path: Path) -> list[Xp3Entry]:
    with pak_path.open("rb") as fp:
        header = fp.read(HEADER_SIZE)
        if len(header) < HEADER_SIZE or not header.startswith(MAGIC):
            raise ValueError("不是支持的 XP3 文件")
        index_offset = read_u64(header, len(MAGIC))
        index_data = read_index_at(fp, index_offset)

    entries: list[Xp3Entry] = []
    pos = 0
    seen: set[str] = set()
    while pos + 12 <= len(index_data):
        kind = index_data[pos:pos + 4]
        size = read_u64(index_data, pos + 4)
        pos += 12
        if size > len(index_data) - pos:
            raise ValueError("XP3 顶层块尺寸越界")
        body = index_data[pos:pos + size]
        pos += size
        if kind == CHUNK_FILE:
            entry = parse_file_chunk(body)
            key = normalized_key(entry.name)
            if key in seen:
                raise ValueError(f"XP3 中存在重复文件名: {entry.name}")
            seen.add(key)
            entries.append(entry)
    if pos != len(index_data):
        raise ValueError("XP3 索引尾部数据非法")
    if not entries:
        raise ValueError("XP3 索引中没有文件")
    return entries


def read_entry(fp, entry: Xp3Entry) -> bytes:
    output = bytearray()
    for segment in entry.segments:
        fp.seek(segment.offset)
        stored = fp.read(segment.stored_size)
        if len(stored) != segment.stored_size:
            raise ValueError(f"读取 XP3 分段失败: {entry.name}")
        if segment.flags & INDEX_ENCODE_MASK:
            decoded = zlib.decompress(stored)
            if len(decoded) != segment.original_size:
                raise ValueError(f"XP3 分段解压尺寸不匹配: {entry.name}")
        else:
            decoded = stored
        output.extend(decoded)
    if len(output) != entry.original_size:
        raise ValueError(f"XP3 文件尺寸不匹配: {entry.name}")
    return bytes(output)


def unpack(pak_path: Path, output_dir: Path) -> None:
    if not pak_path.is_file():
        raise ValueError(f"XP3 文件不存在: {pak_path}")
    entries = load_index(pak_path)
    clear_dir(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    logical_size = 0
    with pak_path.open("rb") as fp:
        for index, entry in enumerate(entries, 1):
            data = read_entry(fp, entry)
            out_path = safe_output_path(output_dir, entry.name)
            out_path.write_bytes(data)
            logical_size += len(data)
            emit(f"[{index}/{len(entries)}] 解包 {entry.name} | {format_size(len(data))}")
    elapsed = time.perf_counter() - start
    emit(f"XP3 解包汇总: 文件数={len(entries)} | 输出={format_size(logical_size)} | 耗时={elapsed:.3f}s")


def info(pak_path: Path) -> None:
    entries = load_index(pak_path)
    total_original = sum(entry.original_size for entry in entries)
    total_stored = sum(entry.stored_size for entry in entries)
    emit(f"XP3: {pak_path}")
    emit(f"文件数: {len(entries)}")
    emit(f"原始大小: {format_size(total_original)}")
    emit(f"存储大小: {format_size(total_stored)}")
    for entry in entries:
        emit(f"{entry.name}\t{entry.original_size}\t{entry.stored_size}\tsegments={len(entry.segments)}")


def read_file(pak_path: Path, name: str, output: Path) -> None:
    entries = load_index(pak_path)
    target_key = normalized_key(name)
    match = next((entry for entry in entries if normalized_key(entry.name) == target_key), None)
    if match is None:
        raise ValueError(f"XP3 中找不到文件: {name}")
    with pak_path.open("rb") as fp:
        data = read_entry(fp, match)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    emit(f"已导出: {output} ({format_size(len(data))})")


def auto_drag_mode(paths: list[str]) -> bool:
    if len(paths) != 1:
        return False
    path = Path(paths[0])
    if path.is_dir():
        pak_path = path.with_suffix(".xp3")
        pack(path, pak_path)
        emit(f"已封包: {pak_path}")
        return True
    if path.is_file() and path.suffix.lower() == ".xp3":
        output = path.with_name(f"{path.stem}_unpacked")
        unpack(path, output)
        emit(f"已解包到: {output}")
        return True
    return False


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] not in {"pack", "unpack", "info", "read", "-h", "--help"}:
        return 0 if auto_drag_mode(sys.argv[1:]) else 2

    parser = argparse.ArgumentParser(description="普通 XP3 封包/解包工具（无加密/无 extraction filter）")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_pack = sub.add_parser("pack", help="封包")
    p_pack.add_argument("--input", required=True)
    p_pack.add_argument("--pak", required=True)
    p_pack.add_argument("--compression", default="auto", choices=["raw", "zlib", "auto"])
    p_pack.add_argument("--index-compression", default="auto", choices=["raw", "zlib", "auto"])

    p_unpack = sub.add_parser("unpack", help="解包")
    p_unpack.add_argument("--pak", required=True)
    p_unpack.add_argument("--output", required=True)

    p_info = sub.add_parser("info", help="查看信息")
    p_info.add_argument("--pak", required=True)

    p_read = sub.add_parser("read", help="提取单个文件")
    p_read.add_argument("--pak", required=True)
    p_read.add_argument("--name", required=True)
    p_read.add_argument("--output", required=True)

    args = parser.parse_args()
    try:
        if args.cmd == "pack":
            pack(Path(args.input), Path(args.pak), args.compression, args.index_compression)
        elif args.cmd == "unpack":
            unpack(Path(args.pak), Path(args.output))
        elif args.cmd == "info":
            info(Path(args.pak))
        elif args.cmd == "read":
            read_file(Path(args.pak), args.name, Path(args.output))
        else:
            parser.print_help()
            return 2
    except Exception as exc:
        print(f"错误: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
