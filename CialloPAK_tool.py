import argparse
import concurrent.futures
import hashlib
import os
import struct
import sys
import time
import lzma
import zlib
from pathlib import Path

try:
    import zstandard as zstd
except Exception:
    zstd = None


MAGIC = b"CialloPAK"
VERSION = 4
MASTER_KEY = b"CialloPakCustomKey::v4"
HASH_SIZE = 16
NONCE_SIZE = 12
FLAG_DEDUP_REUSED = 0x02
MODE_RAW = 0
MODE_ZLIB = 1
MODE_ZSTD = 2
MODE_LZMA = 3

LZMA_LC = 8
LZMA_LP = 0
LZMA_PB = 4
LZMA_DICT_SIZE = 1 << 27
IO_CHUNK_SIZE = 4 * 1024 * 1024


def normalize_relpath(text: str) -> str:
    return text.replace("\\", "/").lower()


def custom_hash_bytes(text: str) -> bytes:
    rel = normalize_relpath(text).encode("utf-8")
    return hashlib.blake2b(rel, digest_size=HASH_SIZE, person=b"CialloHashV4").digest()


def custom_hash(text: str) -> str:
    return custom_hash_bytes(text).hex().upper()


def derive_key(scope: bytes, material: bytes, size: int) -> bytes:
    h = hashlib.blake2b(key=MASTER_KEY, digest_size=32, person=b"CialloKeyV4")
    h.update(scope)
    h.update(material)
    h.update(struct.pack("<Q", size))
    return h.digest()


def xor_crypt(data: bytes, key: bytes, nonce: bytes) -> bytes:
    out = bytearray(len(data))
    counter = 0
    pos = 0
    while pos < len(data):
        block = hashlib.blake2b(
            nonce + struct.pack("<Q", counter),
            digest_size=64,
            key=key,
            person=b"CialloXorV4",
        ).digest()
        take = min(len(data) - pos, len(block))
        for i in range(take):
            out[pos + i] = data[pos + i] ^ block[i]
        pos += take
        counter += 1
    return bytes(out)


def _compress_zstd(data: bytes, level: int = 10) -> bytes:
    if zstd is None:
        raise ValueError("当前环境未安装zstandard，无法使用zstd压缩")
    return zstd.ZstdCompressor(level=level).compress(data)


def _lzma_props_bytes(lc: int, lp: int, pb: int, dict_size: int) -> bytes:
    first = (pb * 5 + lp) * 9 + lc
    return bytes([first]) + struct.pack("<I", dict_size)


def _lzma_filters(lc: int, lp: int, pb: int, dict_size: int):
    return [{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": dict_size}]


def _sanitize_lzma_params():
    lc = max(0, min(8, int(LZMA_LC)))
    lp = max(0, min(4, int(LZMA_LP)))
    pb = max(0, min(4, int(LZMA_PB)))
    if lc + lp > 4:
        lc = max(0, 4 - lp)
    dict_size = max(1 << 16, min(1 << 30, int(LZMA_DICT_SIZE)))
    return lc, lp, pb, dict_size


def _compress_lzma(data: bytes) -> bytes:
    lc, lp, pb, dict_size = _sanitize_lzma_params()
    props = _lzma_props_bytes(lc, lp, pb, dict_size)
    payload = lzma.compress(data, format=lzma.FORMAT_RAW, filters=_lzma_filters(lc, lp, pb, dict_size))
    return props + payload


def _decompress_lzma(payload: bytes) -> bytes:
    if len(payload) < 5:
        raise ValueError("LZMA数据损坏")
    prop0 = payload[0]
    dict_size = struct.unpack("<I", payload[1:5])[0]
    if prop0 >= 9 * 5 * 5:
        raise ValueError("LZMA属性非法")
    lc = prop0 % 9
    rem = prop0 // 9
    lp = rem % 5
    pb = rem // 5
    return lzma.decompress(payload[5:], format=lzma.FORMAT_RAW, filters=_lzma_filters(lc, lp, pb, dict_size))


def mode_name(mode: int) -> str:
    if mode == MODE_RAW:
        return "raw"
    if mode == MODE_ZLIB:
        return "zlib"
    if mode == MODE_ZSTD:
        return "zstd"
    if mode == MODE_LZMA:
        return "lzma"
    return f"unknown({mode})"


def custom_compress(data: bytes, compression: str = "auto", zstd_level: int = 22) -> bytes:
    c = compression.lower()
    if c == "raw":
        return bytes([MODE_RAW]) + data
    if c == "zlib":
        z1 = zlib.compress(data, 9)
        if len(z1) + 1 < len(data):
            return bytes([MODE_ZLIB]) + z1
        return bytes([MODE_RAW]) + data
    if c == "zstd":
        z2 = _compress_zstd(data, zstd_level)
        if len(z2) + 1 < len(data):
            return bytes([MODE_ZSTD]) + z2
        return bytes([MODE_RAW]) + data
    if c == "lzma":
        z3 = _compress_lzma(data)
        if len(z3) + 1 < len(data):
            return bytes([MODE_LZMA]) + z3
        return bytes([MODE_RAW]) + data
    if c == "auto":
        best_mode = MODE_RAW
        best_payload = data
        z1 = zlib.compress(data, 9)
        if len(z1) < len(best_payload):
            best_mode = MODE_ZLIB
            best_payload = z1
        if zstd is not None:
            z2 = _compress_zstd(data, zstd_level)
            if len(z2) < len(best_payload):
                best_mode = MODE_ZSTD
                best_payload = z2
        z3 = _compress_lzma(data)
        if len(z3) < len(best_payload):
            best_mode = MODE_LZMA
            best_payload = z3
        return bytes([best_mode]) + best_payload
    raise ValueError(f"不支持的压缩参数: {compression}")


def custom_decompress(data: bytes) -> bytes:
    if not data:
        raise ValueError("压缩数据为空")
    mode = data[0]
    payload = data[1:]
    if mode == MODE_ZLIB:
        return zlib.decompress(payload)
    if mode == MODE_ZSTD:
        if zstd is None:
            raise ValueError("当前环境未安装zstandard，无法解压zstd封包")
        return zstd.ZstdDecompressor().decompress(payload)
    if mode == MODE_LZMA:
        return _decompress_lzma(payload)
    if mode == MODE_RAW:
        return payload
    raise ValueError("未知压缩模式")


def resolve_worker_count(workers: int, task_count: int) -> int:
    if workers <= 0:
        workers = os.cpu_count() or 1
    workers = max(1, int(workers))
    return max(1, min(workers, max(1, task_count)))


def _read_bytes_with_progress(file_path: Path, progress_prefix: str = "") -> bytes:
    total = file_path.stat().st_size
    if total <= 0:
        if progress_prefix:
            print_progress(progress_prefix, 1, 1)
        return b""
    chunks = []
    done = 0
    with file_path.open("rb") as fp:
        while True:
            chunk = fp.read(IO_CHUNK_SIZE)
            if not chunk:
                break
            chunks.append(chunk)
            done += len(chunk)
            if progress_prefix:
                print_progress(progress_prefix, done, total)
    return b"".join(chunks)


def _write_bytes_to_fp(fp, data: bytes, progress_prefix: str = ""):
    total = len(data)
    if total <= 0:
        if progress_prefix:
            print_progress(progress_prefix, 1, 1)
        return
    done = 0
    mv = memoryview(data)
    while done < total:
        nxt = min(done + IO_CHUNK_SIZE, total)
        fp.write(mv[done:nxt])
        done = nxt
        if progress_prefix:
            print_progress(progress_prefix, done, total)


def _write_file_with_progress(file_path: Path, data: bytes, progress_prefix: str = ""):
    file_path.parent.mkdir(parents=True, exist_ok=True)
    with file_path.open("wb") as fp:
        _write_bytes_to_fp(fp, data, progress_prefix=progress_prefix)


def _prepare_pack_item(task):
    if len(task) == 4:
        file_path_str, rel, compression, detail_progress = task
    else:
        file_path_str, rel, compression = task
        detail_progress = False
    file_path = Path(file_path_str)
    if detail_progress:
        raw = _read_bytes_with_progress(file_path, f"单文件读取 {rel}")
        emit_log_line(f"[单文件] 压缩中 {rel}")
    else:
        raw = file_path.read_bytes()
    hash_bytes = custom_hash_bytes(rel)
    content_id = hashlib.blake2b(raw, digest_size=32, person=b"CialloDedupV4").digest()
    packed = custom_compress(raw, compression=compression)
    if detail_progress:
        emit_log_line(f"[单文件] 压缩完成 {rel}")
    return {
        "rel": rel,
        "hash_bytes": hash_bytes,
        "content_id": content_id,
        "raw_size": len(raw),
        "mode": packed[0],
        "packed": packed,
    }


def _decode_entry_task(task):
    pak_path_str, offset, stored_size, orig_size, nonce = task
    pak_path = Path(pak_path_str)
    with pak_path.open("rb") as fp:
        entry = {
            "offset": offset,
            "stored_size": stored_size,
            "orig_size": orig_size,
            "nonce": nonce,
        }
        return read_entry_data(fp, entry)


def collect_files(input_dir: Path):
    paths = []
    for file_path in input_dir.rglob("*"):
        if file_path.is_file():
            rel = file_path.relative_to(input_dir).as_posix()
            paths.append((file_path, rel))
    return sorted(paths, key=lambda x: x[1])


def write_manifest(manifest_path: Path, rel_paths):
    lines = []
    for rel in rel_paths:
        h = custom_hash(rel)
        lines.append(f"{h}\t{h}\t{rel}")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text("\n".join(lines), encoding="utf-8")


def load_manifest(manifest_path: Path):
    mapping = []
    for line in manifest_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) == 2:
            hash_hex, rel = parts
            alias = hash_hex
        elif len(parts) == 3:
            hash_hex, alias, rel = parts
        else:
            raise ValueError(f"清单格式错误：{line}")
        mapping.append((hash_hex.upper(), alias, rel))
    return mapping


_progress_line = ""
_progress_last_render_ts = 0.0


def emit_log_line(message: str):
    global _progress_line
    if _progress_line:
        sys.stdout.write("\r\x1b[2K")
        sys.stdout.write(message + "\n")
        sys.stdout.write("\r" + _progress_line)
        sys.stdout.flush()
        return
    print(message, flush=True)


def wait_for_any_key():
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        return
    if os.name == "nt":
        try:
            import msvcrt
            print("按任意键退出...", end="", flush=True)
            msvcrt.getwch()
            print()
            return
        except Exception:
            pass
    input("按回车退出...")


def print_progress(prefix: str, current: int, total: int):
    global _progress_line, _progress_last_render_ts
    total = max(total, 1)
    current = max(0, min(current, total))
    ratio = current / total
    bar_width = 28
    filled = int(bar_width * ratio)
    bar = "█" * filled + "░" * (bar_width - filled)
    percent = int(ratio * 100)
    next_line = f"{prefix} [{bar}] {percent:3d}% ({current}/{total})"
    now = time.perf_counter()
    should_render = current >= total or (now - _progress_last_render_ts) >= 0.05
    _progress_line = next_line
    if not should_render:
        return
    sys.stdout.write("\r" + _progress_line)
    sys.stdout.flush()
    _progress_last_render_ts = now
    if current >= total:
        print()
        _progress_line = ""
        _progress_last_render_ts = 0.0


def clear_dir(path: Path):
    if path.exists():
        for p in sorted(path.rglob("*"), reverse=True):
            if p.is_file():
                p.unlink()
            elif p.is_dir():
                p.rmdir()


def dir_total_size(path: Path) -> int:
    total = 0
    for p in path.rglob("*"):
        if p.is_file():
            total += p.stat().st_size
    return total


def format_size(num_bytes: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    value = float(num_bytes)
    unit = 0
    while value >= 1024 and unit < len(units) - 1:
        value /= 1024
        unit += 1
    return f"{value:.2f}{units[unit]}"


def _pack_header(file_count: int, index_offset: int, index_size: int, index_nonce: bytes) -> bytes:
    return struct.pack(
        f"<9s H I Q Q {NONCE_SIZE}s",
        MAGIC,
        VERSION,
        file_count,
        index_offset,
        index_size,
        index_nonce,
    )


def _read_header(fp):
    header_fmt = f"<9s H I Q Q {NONCE_SIZE}s"
    header_size = struct.calcsize(header_fmt)
    raw = fp.read(header_size)
    if len(raw) != header_size:
        raise ValueError("封包太小")
    magic, version, file_count, index_offset, index_size, index_nonce = struct.unpack(header_fmt, raw)
    if magic != MAGIC:
        raise ValueError("魔术头不匹配")
    if version != VERSION:
        raise ValueError(f"版本不支持：{version}")
    return header_size, file_count, index_offset, index_size, index_nonce


def _build_index(entries):
    entries_sorted = sorted(entries, key=lambda x: x["hash_bytes"])
    index_plain = bytearray()
    index_plain.extend(struct.pack("<I", len(entries_sorted)))
    for entry in entries_sorted:
        index_plain.extend(entry["hash_bytes"])
        index_plain.extend(
            struct.pack(
                f"<B Q Q Q {NONCE_SIZE}s",
                entry["flags"],
                entry["orig_size"],
                entry["stored_size"],
                entry["offset"],
                entry["nonce"],
            )
        )
    return bytes(index_plain)


def _parse_index(index_plain: bytes):
    pos = 0
    if len(index_plain) < 4:
        raise ValueError("索引损坏")
    count = struct.unpack_from("<I", index_plain, pos)[0]
    pos += 4
    entry_size = struct.calcsize(f"<{HASH_SIZE}s B Q Q Q {NONCE_SIZE}s")
    entries = []
    for _ in range(count):
        if pos + entry_size > len(index_plain):
            raise ValueError("索引损坏：记录不足")
        hash_bytes, flags, orig_size, stored_size, offset, nonce = struct.unpack_from(
            f"<{HASH_SIZE}s B Q Q Q {NONCE_SIZE}s", index_plain, pos
        )
        pos += entry_size
        entries.append(
            {
                "hash_bytes": hash_bytes,
                "hash_hex": hash_bytes.hex().upper(),
                "flags": flags,
                "orig_size": orig_size,
                "stored_size": stored_size,
                "offset": offset,
                "nonce": nonce,
            }
        )
    return entries


def load_index(pak_path: Path):
    with pak_path.open("rb") as fp:
        _, _, index_offset, index_size, index_nonce = _read_header(fp)
        fp.seek(index_offset)
        index_enc = fp.read(index_size)
    index_key = derive_key(b"INDEX", b"__index__", index_size)
    index_plain = xor_crypt(index_enc, index_key, index_nonce)
    entries = _parse_index(index_plain)
    return entries


def read_entry_data(fp, entry, detail_progress: str = ""):
    fp.seek(entry["offset"])
    if detail_progress:
        total = entry["stored_size"]
        done = 0
        chunks = []
        while done < total:
            chunk = fp.read(min(IO_CHUNK_SIZE, total - done))
            if not chunk:
                break
            chunks.append(chunk)
            done += len(chunk)
            print_progress(f"{detail_progress} 读取封包", done, total)
        enc = b"".join(chunks)
    else:
        enc = fp.read(entry["stored_size"])
    if len(enc) != entry["stored_size"]:
        raise ValueError("数据区越界")
    key = derive_key(b"DATA", entry["nonce"], entry["orig_size"])
    if detail_progress:
        emit_log_line(f"[单文件] 解密解压中 {detail_progress}")
    packed = xor_crypt(enc, key, entry["nonce"])
    raw = custom_decompress(packed)
    if len(raw) != entry["orig_size"]:
        raise ValueError("尺寸校验失败")
    if detail_progress:
        emit_log_line(f"[单文件] 解密解压完成 {detail_progress}")
    return raw


def pack(
    input_dir: Path,
    pak_path: Path,
    manifest_path: Path,
    dedup_mode: bool = False,
    show_progress: bool = True,
    compression: str = "auto",
    workers: int = 0,
    parallel: str = "thread",
):
    files = collect_files(input_dir)
    if not files:
        raise ValueError("输入目录没有可封包文件")

    rel_paths = [rel for _, rel in files]
    write_manifest(manifest_path, rel_paths)
    hash_seen = {}
    dedup_map = {}
    entries = []
    dedup_reused = 0
    total_files = len(files)
    header_size = struct.calcsize(f"<9s H I Q Q {NONCE_SIZE}s")

    pak_path.parent.mkdir(parents=True, exist_ok=True)
    worker_count = resolve_worker_count(workers, total_files)
    single_file_detail = show_progress and total_files == 1
    pack_tasks = [(str(file_path), rel, compression, single_file_detail) for file_path, rel in files]
    if worker_count > 1:
        if parallel == "process":
            executor_cls = concurrent.futures.ProcessPoolExecutor
        else:
            executor_cls = concurrent.futures.ThreadPoolExecutor
        executor = executor_cls(max_workers=worker_count)
        prepared_iter = executor.map(_prepare_pack_item, pack_tasks)
    else:
        executor = None
        prepared_iter = map(_prepare_pack_item, pack_tasks)

    try:
        with pak_path.open("wb") as fp:
            fp.write(b"\x00" * header_size)
            for idx, prepared in enumerate(prepared_iter, 1):
                rel = prepared["rel"]
                hash_bytes = prepared["hash_bytes"]
                hash_hex = hash_bytes.hex().upper()
                if hash_hex in hash_seen and hash_seen[hash_hex] != rel:
                    if executor is not None:
                        executor.shutdown(wait=False, cancel_futures=True)
                    raise ValueError(f"hash碰撞：{hash_seen[hash_hex]} 与 {rel}")
                hash_seen[hash_hex] = rel
                content_id = prepared["content_id"]
                reused = dedup_mode and content_id in dedup_map
                if reused:
                    offset, stored_size, orig_size, nonce, mode = dedup_map[content_id]
                    dedup_reused += 1
                else:
                    packed = prepared["packed"]
                    mode = prepared["mode"]
                    orig_size = prepared["raw_size"]
                    nonce = hashlib.blake2b(content_id, digest_size=NONCE_SIZE, person=b"CialloNonceV4").digest()
                    key = derive_key(b"DATA", nonce, orig_size)
                    enc = xor_crypt(packed, key, nonce)
                    offset = fp.tell()
                    if single_file_detail:
                        _write_bytes_to_fp(fp, enc, progress_prefix=f"单文件写入 {rel}")
                    else:
                        fp.write(enc)
                    stored_size = len(enc)
                    if dedup_mode:
                        dedup_map[content_id] = (offset, stored_size, orig_size, nonce, mode)
                flags = FLAG_DEDUP_REUSED if reused else 0
                entries.append(
                    {
                        "hash_bytes": hash_bytes,
                        "flags": flags,
                        "orig_size": orig_size,
                        "stored_size": stored_size,
                        "offset": offset,
                        "nonce": nonce,
                    }
                )
                ratio = (stored_size / orig_size) if orig_size > 0 else 0.0
                action = "复用" if reused else "写入"
                emit_log_line(f"[{idx}/{total_files}] {action} {rel} | mode={mode_name(mode)} | {stored_size}/{orig_size} ({ratio:.2%})")
                if show_progress:
                    print_progress("封包处理中", idx, total_files)

            index_offset = fp.tell()
            index_plain = _build_index(entries)
            index_nonce = hashlib.blake2b(
                struct.pack("<I", len(entries)) + struct.pack("<Q", index_offset),
                digest_size=NONCE_SIZE,
                person=b"CialloIdxV4",
            ).digest()
            index_key = derive_key(b"INDEX", b"__index__", len(index_plain))
            index_enc = xor_crypt(index_plain, index_key, index_nonce)
            fp.write(index_enc)
            index_size = len(index_enc)
            fp.seek(0)
            fp.write(_pack_header(len(entries), index_offset, index_size, index_nonce))
    finally:
        if executor is not None:
            executor.shutdown(wait=True)

    if dedup_mode:
        print(f"去重复用文件数: {dedup_reused}")


def unpack(
    pak_path: Path,
    manifest_path,
    output_dir: Path,
    show_progress: bool = True,
    workers: int = 0,
    parallel: str = "thread",
):
    entries = load_index(pak_path)
    manifest_map = {}
    if manifest_path is not None:
        manifest_items = load_manifest(manifest_path)
        for hash_hex, _, rel in manifest_items:
            manifest_map[hash_hex] = rel

    output_dir.mkdir(parents=True, exist_ok=True)
    hash_name_count = {}
    total_files = len(entries)
    grouped = {}
    for entry in entries:
        hash_hex = entry["hash_hex"]
        if manifest_map:
            if hash_hex not in manifest_map:
                raise ValueError(f"清单缺少映射：{hash_hex}")
            rel = manifest_map[hash_hex]
        else:
            cnt = hash_name_count.get(hash_hex, 0)
            hash_name_count[hash_hex] = cnt + 1
            rel = f"{hash_hex}.bin" if cnt == 0 else f"{hash_hex}_{cnt}.bin"
        cache_key = (entry["offset"], entry["stored_size"], entry["orig_size"], entry["nonce"])
        if cache_key not in grouped:
            grouped[cache_key] = {"entry": entry, "rels": []}
        grouped[cache_key]["rels"].append(rel)

    unique_groups = list(grouped.values())
    decode_tasks = [
        (str(pak_path), g["entry"]["offset"], g["entry"]["stored_size"], g["entry"]["orig_size"], g["entry"]["nonce"])
        for g in unique_groups
    ]
    worker_count = resolve_worker_count(workers, len(decode_tasks))
    if worker_count > 1:
        if parallel == "process":
            executor_cls = concurrent.futures.ProcessPoolExecutor
        else:
            executor_cls = concurrent.futures.ThreadPoolExecutor
        with executor_cls(max_workers=worker_count) as executor:
            decode_iter = executor.map(_decode_entry_task, decode_tasks)
            done = 0
            for g, raw in zip(unique_groups, decode_iter):
                for rel in g["rels"]:
                    out_path = output_dir / rel
                    if show_progress and total_files == 1:
                        _write_file_with_progress(out_path, raw, progress_prefix=f"单文件写出 {rel}")
                    else:
                        out_path.parent.mkdir(parents=True, exist_ok=True)
                        out_path.write_bytes(raw)
                    done += 1
                    if show_progress:
                        print_progress("解包处理中", done, total_files)
    else:
        done = 0
        detail_single = show_progress and total_files == 1
        with pak_path.open("rb") as fp:
            for g, task in zip(unique_groups, decode_tasks):
                if detail_single:
                    raw = read_entry_data(fp, g["entry"], detail_progress=g["rels"][0])
                else:
                    raw = _decode_entry_task(task)
                for rel in g["rels"]:
                    out_path = output_dir / rel
                    if detail_single:
                        _write_file_with_progress(out_path, raw, progress_prefix=f"单文件写出 {rel}")
                    else:
                        out_path.parent.mkdir(parents=True, exist_ok=True)
                        out_path.write_bytes(raw)
                    done += 1
                    if show_progress:
                        print_progress("解包处理中", done, total_files)


def extract_by_name(pak_path: Path, rel_name: str) -> bytes:
    target_hash = custom_hash_bytes(rel_name)
    entries = load_index(pak_path)
    entry_map = {e["hash_bytes"]: e for e in entries}
    if target_hash not in entry_map:
        raise ValueError("未找到目标hash")
    with pak_path.open("rb") as fp:
        return read_entry_data(fp, entry_map[target_hash])


def build_test_files(base_dir: Path):
    clear_dir(base_dir)
    base_dir.mkdir(parents=True, exist_ok=True)
    (base_dir / "script").mkdir(parents=True, exist_ok=True)
    (base_dir / "assets" / "bgm").mkdir(parents=True, exist_ok=True)
    (base_dir / "assets" / "image").mkdir(parents=True, exist_ok=True)
    (base_dir / "assets" / "dup").mkdir(parents=True, exist_ok=True)
    (base_dir / "script" / "start.ks").write_text("Ciallo~(∠・ω< )⌒★\njump label_01\n", encoding="utf-8")
    (base_dir / "assets" / "bgm" / "theme.txt").write_text("BGM metadata\nloop=true\n" * 64, encoding="utf-8")
    blob = ((bytes([1, 2, 3, 4, 5, 6, 7, 8]) * 1024) + (bytes([9]) * 4096))
    (base_dir / "assets" / "image" / "hero.dat").write_bytes(blob)
    (base_dir / "assets" / "dup" / "hero_copy.dat").write_bytes(blob)


def compare_dirs(a: Path, b: Path):
    a_files = sorted([p.relative_to(a).as_posix() for p in a.rglob("*") if p.is_file()])
    b_files = sorted([p.relative_to(b).as_posix() for p in b.rglob("*") if p.is_file()])
    if a_files != b_files:
        return False, f"文件列表不一致\nA={a_files}\nB={b_files}"
    for rel in a_files:
        if (a / rel).read_bytes() != (b / rel).read_bytes():
            return False, f"文件内容不一致：{rel}"
    return True, "OK"


def selftest(work_dir: Path):
    input_dir = work_dir / "test_input"
    output_dir = work_dir / "test_output"
    unpack_dir = work_dir / "test_unpacked"
    pak_path = output_dir / "demo.cpk"
    manifest_path = output_dir / "demo_manifest.txt"
    for d in [output_dir, unpack_dir]:
        clear_dir(d)
        d.mkdir(parents=True, exist_ok=True)
    build_test_files(input_dir)
    pack(input_dir, pak_path, manifest_path, dedup_mode=True, show_progress=False, compression="zlib")
    unpack(pak_path, manifest_path, unpack_dir, show_progress=False)
    ok, msg = compare_dirs(input_dir, unpack_dir)
    sample_hash = custom_hash("script/start.ks")
    print(f"封包文件: {pak_path}")
    print(f"清单文件: {manifest_path}")
    print(f"示例长hash: {sample_hash} (长度={len(sample_hash)})")
    print(f"解包目录: {unpack_dir}")
    print(f"校验结果: {msg}")
    if not ok:
        raise SystemExit(1)
    if zstd is not None:
        pak_zstd = output_dir / "demo_zstd.cpk"
        unpack_zstd = work_dir / "test_unpacked_zstd"
        clear_dir(unpack_zstd)
        unpack_zstd.mkdir(parents=True, exist_ok=True)
        pack(input_dir, pak_zstd, manifest_path, dedup_mode=True, show_progress=False, compression="zstd")
        unpack(pak_zstd, manifest_path, unpack_zstd, show_progress=False)
        ok2, msg2 = compare_dirs(input_dir, unpack_zstd)
        print(f"Zstd校验结果: {msg2}")
        if not ok2:
            raise SystemExit(1)
    pak_lzma = output_dir / "demo_lzma.cpk"
    unpack_lzma = work_dir / "test_unpacked_lzma"
    clear_dir(unpack_lzma)
    unpack_lzma.mkdir(parents=True, exist_ok=True)
    pack(input_dir, pak_lzma, manifest_path, dedup_mode=True, show_progress=False, compression="lzma")
    unpack(pak_lzma, manifest_path, unpack_lzma, show_progress=False)
    ok3, msg3 = compare_dirs(input_dir, unpack_lzma)
    print(f"Lzma校验结果: {msg3}")
    if not ok3:
        raise SystemExit(1)


def build_speed_test_files(base_dir: Path, file_count: int = 200, unit_size: int = 262144):
    clear_dir(base_dir)
    base_dir.mkdir(parents=True, exist_ok=True)
    (base_dir / "grp").mkdir(parents=True, exist_ok=True)
    unique_count = file_count // 2
    blobs = []
    for i in range(unique_count):
        head = f"ASSET_{i:04d}\n".encode("utf-8")
        pattern = bytes([(i * 17 + j) & 0xFF for j in range(128)])
        body = (pattern * (unit_size // len(pattern) + 1))[:unit_size - len(head)]
        blob = head + body
        blobs.append(blob)
        (base_dir / "grp" / f"u_{i:04d}.bin").write_bytes(blob)
    for i in range(file_count - unique_count):
        (base_dir / "grp" / f"d_{i:04d}.bin").write_bytes(blobs[i % unique_count])


def speedtest(work_dir: Path, rounds: int, compression: str):
    speed_input = work_dir / "speed_input"
    speed_pack_dir = work_dir / "speed_output"
    clear_dir(speed_pack_dir)
    speed_pack_dir.mkdir(parents=True, exist_ok=True)
    pak_path = speed_pack_dir / "speed.cpk"
    manifest_path = speed_pack_dir / "speed_manifest.txt"
    build_speed_test_files(speed_input)

    t_pack0 = time.perf_counter()
    pack(speed_input, pak_path, manifest_path, dedup_mode=True, show_progress=False, compression=compression)
    t_pack1 = time.perf_counter()
    pack_cost = t_pack1 - t_pack0

    costs = []
    logical_size = dir_total_size(speed_input)
    for i in range(rounds):
        out_dir = speed_pack_dir / f"unpack_round_{i}"
        clear_dir(out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        t0 = time.perf_counter()
        unpack(pak_path, manifest_path, out_dir, show_progress=False)
        t1 = time.perf_counter()
        costs.append(t1 - t0)
        clear_dir(out_dir)

    avg_unpack = sum(costs) / len(costs)
    pack_throughput = (logical_size / (1024 * 1024)) / max(pack_cost, 1e-9)
    unpack_throughput = (logical_size / (1024 * 1024)) / max(avg_unpack, 1e-9)
    print(f"速度测试轮数: {rounds}")
    print(f"逻辑数据量: {format_size(logical_size)}")
    print(f"封包耗时: {pack_cost:.4f}s")
    print(f"封包吞吐: {pack_throughput:.2f} MB/s")
    print(f"平均解包耗时: {avg_unpack:.4f}s")
    print(f"平均解包吞吐: {unpack_throughput:.2f} MB/s")
    print(f"封包文件大小: {format_size(pak_path.stat().st_size)}")


def auto_drag_mode(paths):
    if len(paths) == 1:
        p = Path(paths[0])
        if p.is_dir():
            pak_path = p.parent / f"{p.name}.cpk"
            manifest_path = p.parent / f"{p.name}_manifest.txt"
            pack(p, pak_path, manifest_path, dedup_mode=True, show_progress=True)
            print(f"已封包: {pak_path}")
            print(f"已生成清单: {manifest_path}")
            wait_for_any_key()
            return
        if p.suffix.lower() == ".cpk" and p.is_file():
            output_dir = p.parent / f"{p.stem}_unpacked"
            clear_dir(output_dir)
            output_dir.mkdir(parents=True, exist_ok=True)
            unpack(p, None, output_dir, show_progress=True)
            print(f"已按hash解包到: {output_dir}")
            wait_for_any_key()
            return
    if len(paths) == 2:
        p1 = Path(paths[0])
        p2 = Path(paths[1])
        items = {p1.suffix.lower(): p1, p2.suffix.lower(): p2}
        if ".cpk" in items and ".txt" in items:
            pak_path = items[".cpk"]
            manifest_path = items[".txt"]
            output_dir = pak_path.parent / f"{pak_path.stem}_unpacked"
            clear_dir(output_dir)
            output_dir.mkdir(parents=True, exist_ok=True)
            unpack(pak_path, manifest_path, output_dir, show_progress=True)
            print(f"已解包到: {output_dir}")
            wait_for_any_key()
            return
    raise SystemExit("拖拽模式支持：1个文件夹封包，或1个cpk按hash解包，或1个cpk+1个txt按清单解包")


def main():
    if len(sys.argv) > 1 and sys.argv[1] not in {"pack", "unpack", "selftest", "speedtest", "read", "-h", "--help"}:
        auto_drag_mode(sys.argv[1:])
        return

    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_pack = sub.add_parser("pack")
    p_pack.add_argument("--input", required=True)
    p_pack.add_argument("--pak", required=True)
    p_pack.add_argument("--manifest", required=True)
    p_pack.add_argument("--dedup", action="store_true")
    p_pack.add_argument("--compression", required=False, choices=["zlib", "zstd", "lzma", "auto", "raw"], default="auto")
    p_pack.add_argument("--workers", required=False, type=int, default=0)
    p_pack.add_argument("--parallel", required=False, choices=["thread", "process"], default="thread")

    p_unpack = sub.add_parser("unpack")
    p_unpack.add_argument("--pak", required=True)
    p_unpack.add_argument("--manifest", required=False)
    p_unpack.add_argument("--output", required=True)
    p_unpack.add_argument("--workers", required=False, type=int, default=0)
    p_unpack.add_argument("--parallel", required=False, choices=["thread", "process"], default="thread")

    p_read = sub.add_parser("read")
    p_read.add_argument("--pak", required=True)
    p_read.add_argument("--name", required=True)
    p_read.add_argument("--output", required=True)

    p_self = sub.add_parser("selftest")
    p_self.add_argument("--workdir", required=False, default=str(Path(__file__).resolve().parent))

    p_speed = sub.add_parser("speedtest")
    p_speed.add_argument("--workdir", required=False, default=str(Path(__file__).resolve().parent))
    p_speed.add_argument("--rounds", required=False, type=int, default=5)
    p_speed.add_argument("--compression", required=False, choices=["zlib", "zstd", "lzma", "auto", "raw"], default="auto")

    args = parser.parse_args()
    if args.cmd == "pack":
        pack(
            Path(args.input),
            Path(args.pak),
            Path(args.manifest),
            dedup_mode=args.dedup,
            show_progress=True,
            compression=args.compression,
            workers=args.workers,
            parallel=args.parallel,
        )
        wait_for_any_key()
        return
    if args.cmd == "unpack":
        out = Path(args.output)
        clear_dir(out)
        out.mkdir(parents=True, exist_ok=True)
        manifest = Path(args.manifest) if args.manifest else None
        unpack(Path(args.pak), manifest, out, show_progress=True, workers=args.workers, parallel=args.parallel)
        wait_for_any_key()
        return
    if args.cmd == "read":
        data = extract_by_name(Path(args.pak), args.name)
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        print(f"已导出: {out}")
        return
    if args.cmd == "selftest":
        selftest(Path(args.workdir))
        return
    if args.cmd == "speedtest":
        speedtest(Path(args.workdir), max(1, args.rounds), args.compression)
        return
    raise SystemExit(2)


if __name__ == "__main__":
    main()
