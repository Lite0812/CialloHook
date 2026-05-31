import argparse
import concurrent.futures
import hashlib
import io
import mmap
import os
import secrets
import struct
import sys
import threading
import time
import lzma
import zlib
from collections import OrderedDict
from pathlib import Path

try:
    import zstandard as zstd
except Exception:
    zstd = None

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.asymmetric import ed25519 as crypto_ed25519
    from cryptography.exceptions import InvalidSignature
    _HAS_AESGCM = True
    _HAS_CRYPTO_ED25519 = True
except Exception:
    _HAS_AESGCM = False
    _HAS_CRYPTO_ED25519 = False

try:
    from nacl.signing import SigningKey
    from nacl.exceptions import BadSignatureError
    _HAS_NACL_ED25519 = True
except Exception:
    _HAS_NACL_ED25519 = False

_HAS_ED25519 = _HAS_NACL_ED25519 or _HAS_CRYPTO_ED25519


# ============================================================================
# Constants
# ============================================================================

MAGIC = b"LitePAK"
VERSION = 6
HEADER_SIZE = 96
TRAILER_SIZE = 192          # v5: 128 + 64 (Ed25519 signature)
PATH_HASH_SIZE = 16
FILE_ID_SIZE = 16
CHUNK_HASH_SIZE = 32
NONCE_SIZE = 12
GCM_TAG_SIZE = 16           # AES-GCM authentication tag
FRAGMENT_SIZE = 16
IO_CHUNK_SIZE = 4 * 1024 * 1024
LARGE_FILE_THRESHOLD = 64 * 1024 * 1024
WHOLE_FILE_THRESHOLD = 1 * 1024 * 1024
CDC_DEFAULT_AVG_SIZE = 128 * 1024
SEGMENT_BOUNDARY = 4096     # 分段密钥派生边界
SEGMENT_DEPEND_LEN = 128    # 前段明文依赖长度

# CDC chunking (FastCDC-like normalized cut)
def make_cdc_params(avg_size: int) -> dict:
    avg_size = max(32 * 1024, int(avg_size))
    min_size = max(8 * 1024, avg_size // 4)
    max_size = max(avg_size + 1, avg_size * 4)
    avg_bits = avg_size.bit_length() - 1
    return {
        "min_size": min_size,
        "avg_size": avg_size,
        "max_size": max_size,
        "mask_early": (1 << (avg_bits + 1)) - 1,
        "mask_late": (1 << max(1, avg_bits - 1)) - 1,
    }


CDC_PARAMS = make_cdc_params(CDC_DEFAULT_AVG_SIZE)

# Stronger index hashes
INDEX_HASH_SIZE = 16  # BLAKE2b-128

# Runtime plaintext chunk cache
CHUNK_CACHE_MAX_ENTRIES = 1024
CHUNK_CACHE_MAX_BYTES = 128 * 1024 * 1024

# Mode values
MODE_RAW = 0xC0
MODE_ZLIB = 0xC1
MODE_ZSTD = 0xC2
MODE_LZMA = 0xC3

# Header flags
FLAG_HAS_TRAILER = 0x1000
FLAG_INDEX_COMPRESSED = 0x2000
FLAG_CHUNK_INDEX = 0x4000
FLAG_FULL_VERIFY = 0x8000
FLAG_AES_GCM = 0x0100       # v5: AES-256-GCM encryption
FLAG_ED25519_SIGNED = 0x0200 # v5: Ed25519 signed trailer
FLAG_INDEX_OBFUSCATED = 0x0400  # v5: index block shuffle + substitution
FLAG_WB_STRONG = 0x0800      # v5: nonlinear white-box S-box

V6_FEATURE_KEY_SIG = 0x00000001
V6_FEATURE_KEYED_INDEX = 0x00000002
V6_FEATURE_FILE_ID_MASK = 0x00000004
V6_FEATURE_ARX_LAYER = 0x00000008
V6_FEATURE_FEISTEL_HEAD = 0x00000010
V6_FEATURES = (V6_FEATURE_KEY_SIG | V6_FEATURE_KEYED_INDEX |
               V6_FEATURE_FILE_ID_MASK | V6_FEATURE_ARX_LAYER |
               V6_FEATURE_FEISTEL_HEAD)

CHUNK_TRANSFORM_NONE = 0x00
CHUNK_TRANSFORM_ARX = 0x01
CHUNK_TRANSFORM_FEISTEL = 0x02

# Entry flags
ENTRY_FILE = 0xA0
ENTRY_DIRECTORY = 0xA1
ENTRY_KEY_PAYLOAD = 0xA2

# Chunk kinds
CHUNK_KIND_FILE = 0x31
CHUNK_KIND_K9 = 0x39

LZMA_LC = 8
LZMA_LP = 0
LZMA_PB = 4
LZMA_DICT_SIZE = 1 << 27

# DLL fixed root material (must match runtime DLL)
DLL_ROOT_SEED_A = bytes.fromhex(
    "9e3b8f0a73c44152d7e9810f2c6a4b95"
    "a61fd8c2be77439184c1e06f55ab2d17"
)
DLL_ROOT_SEED_B = bytes.fromhex(
    "3c5f17aa92d14be88f0d6b31c4a2795e"
    "db1027f6a4ce48109d3f75b2e861cc43"
)

# White-box obfuscation tables (simulates white-box key derivation)
_WB_STRONG_SBOX = bytes([
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
])


def _select_wb_sbox(strong_wb: bool) -> bytes:
    if strong_wb:
        return _WB_STRONG_SBOX
    return _WB_STRONG_SBOX

# Ed25519 default signing seed (deterministic for tool; production would use file-based key)
_ED25519_SEED = bytes.fromhex(
    "7a1f3e8c5b0d49a2c6e8f17d3b9a4c50"
    "e2d6a8f1c3b5074e9d2f6a8b1c4e7053"
)

_CRC32C_TABLE = None
_progress_line = ""
_progress_last_render_ts = 0.0
GEAR_TABLE = [
    struct.unpack("<Q", hashlib.blake2b(bytes([i]), digest_size=8, person=b"LiteCDC6").digest())[0]
    for i in range(256)
]


# ============================================================================
# Hash / CRC helpers
# ============================================================================

def _make_crc32c_table():
    global _CRC32C_TABLE
    if _CRC32C_TABLE is not None:
        return
    _CRC32C_TABLE = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = (crc >> 1) ^ 0x1EDC6F41 if crc & 1 else crc >> 1
        _CRC32C_TABLE.append(crc)


def crc32c(data: bytes, seed: int = 0) -> int:
    _make_crc32c_table()
    crc = seed ^ 0xFFFFFFFF
    for b in data:
        crc = _CRC32C_TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


def crc8(data: bytes) -> int:
    crc = 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) if crc & 0x80 else (crc << 1)
        crc &= 0xFF
    return crc


def hash16_personal(data: bytes, person: bytes) -> bytes:
    return hashlib.blake2b(data, digest_size=INDEX_HASH_SIZE, person=person).digest()


def normalize_relpath(text: str) -> str:
    return text.replace("\\", "/").lower()


def lite_hash_bytes(text: str) -> bytes:
    return hashlib.blake2b(
        normalize_relpath(text).encode("utf-8"),
        digest_size=PATH_HASH_SIZE,
        person=b"LitePathV6",
    ).digest()


def lite_hash(text: str) -> str:
    return lite_hash_bytes(text).hex().upper()


def chunk_hash_bytes(data: bytes) -> bytes:
    return hashlib.blake2b(data, digest_size=CHUNK_HASH_SIZE, person=b"LiteChkV6").digest()


# ============================================================================
# Console / progress
# ============================================================================

def _safe_console_text(text: str) -> str:
    encoding = sys.stdout.encoding or "utf-8"
    try:
        text.encode(encoding)
        return text
    except UnicodeEncodeError:
        return text.encode(encoding, errors="replace").decode(encoding, errors="replace")


def emit_log_line(message: str):
    global _progress_line
    message = _safe_console_text(message)
    if _progress_line:
        sys.stdout.write("\r\x1b[2K")
        sys.stdout.write(message + "\n")
        sys.stdout.write("\r" + _progress_line)
        sys.stdout.flush()
        return
    print(message, flush=True)


def print_progress(prefix: str, current: int, total: int):
    global _progress_line, _progress_last_render_ts
    total = max(total, 1)
    current = max(0, min(current, total))
    ratio = current / total
    bar_width = 28
    filled = int(bar_width * ratio)
    encoding = (sys.stdout.encoding or "utf-8").lower()
    unicode_bar = not encoding.startswith("gbk") and not encoding.startswith("cp936")
    full_ch = "█" if unicode_bar else "#"
    empty_ch = "░" if unicode_bar else "-"
    bar = full_ch * filled + empty_ch * (bar_width - filled)
    percent = int(ratio * 100)
    line = _safe_console_text(f"{prefix} [{bar}] {percent:3d}% ({current}/{total})")
    now = time.perf_counter()
    should_render = current >= total or (now - _progress_last_render_ts) >= 0.05
    _progress_line = line
    if not should_render:
        return
    sys.stdout.write("\r" + _progress_line)
    sys.stdout.flush()
    _progress_last_render_ts = now
    if current >= total:
        print()
        _progress_line = ""
        _progress_last_render_ts = 0.0


def format_size(n: int) -> str:
    f = float(n)
    for u in ["B", "KB", "MB", "GB"]:
        if f < 1024:
            return f"{f:.2f}{u}"
        f /= 1024
    return f"{f:.2f}TB"


def format_duration(seconds: float) -> str:
    if seconds < 60:
        return f"{seconds:.3f}s"
    m, s = divmod(seconds, 60)
    if m < 60:
        return f"{int(m)}m {s:.3f}s"
    h, m = divmod(int(m), 60)
    return f"{h}h {m}m {s:.3f}s"


def resolve_worker_count(workers: int, tasks: int) -> int:
    if workers <= 0:
        workers = os.cpu_count() or 1
    return max(1, min(int(workers), max(1, tasks)))


def clear_dir(path: Path):
    if path.exists():
        for p in sorted(path.rglob("*"), reverse=True):
            if p.is_file():
                p.unlink()
            elif p.is_dir():
                p.rmdir()


def wait_for_any_key():
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        return
    if os.name == "nt":
        try:
            import msvcrt
            print("\n按任意键退出...", end="", flush=True)
            msvcrt.getwch()
            print()
        except Exception:
            input("\n按回车退出...")
    else:
        input("\n按回车退出...")


# ============================================================================
# Custom PRNG (MT19937 variant with modified init and output transform)
# ============================================================================

class LitePRNG:
    """Modified MT19937 with custom initialization and output transform."""
    _N = 624
    _M = 397
    _MATRIX_A = 0x9908B0DF
    _UPPER_MASK = 0x80000000
    _LOWER_MASK = 0x7FFFFFFF
    # Custom twist constants (different from standard MT)
    _INIT_MULT = 0x6C078965 ^ 0x4A7E3B19
    _TEMPER_A = 0x9D2C5681 ^ 0x1F3A7C05
    _TEMPER_B = 0xEFC60000 ^ 0x3B8E1A47

    def __init__(self, seed: int):
        self.mt = [0] * self._N
        self.mti = self._N + 1
        self._init_genrand(seed)

    def _init_genrand(self, s: int):
        self.mt[0] = s & 0xFFFFFFFF
        for i in range(1, self._N):
            self.mt[i] = (self._INIT_MULT * (self.mt[i - 1] ^ (self.mt[i - 1] >> 30)) + i) & 0xFFFFFFFF
        self.mti = self._N

    def _generate(self):
        mag01 = [0, self._MATRIX_A]
        for kk in range(self._N - self._M):
            y = (self.mt[kk] & self._UPPER_MASK) | (self.mt[kk + 1] & self._LOWER_MASK)
            self.mt[kk] = self.mt[kk + self._M] ^ (y >> 1) ^ mag01[y & 1]
        for kk in range(self._N - self._M, self._N - 1):
            y = (self.mt[kk] & self._UPPER_MASK) | (self.mt[kk + 1] & self._LOWER_MASK)
            self.mt[kk] = self.mt[kk + (self._M - self._N)] ^ (y >> 1) ^ mag01[y & 1]
        y = (self.mt[self._N - 1] & self._UPPER_MASK) | (self.mt[0] & self._LOWER_MASK)
        self.mt[self._N - 1] = self.mt[self._M - 1] ^ (y >> 1) ^ mag01[y & 1]
        self.mti = 0

    def next_u32(self) -> int:
        if self.mti >= self._N:
            self._generate()
        y = self.mt[self.mti]
        self.mti += 1
        # Custom output tempering (different from standard MT)
        y ^= (y >> 11)
        y ^= (y << 7) & self._TEMPER_A
        y ^= (y << 15) & self._TEMPER_B
        y ^= (y >> 18)
        y ^= (y >> 5)  # extra transform
        return y & 0xFFFFFFFF

    def permutation(self, n: int) -> list:
        """Generate a random permutation of 0..n-1 (Fisher-Yates)."""
        perm = list(range(n))
        for i in range(n - 1, 0, -1):
            j = self.next_u32() % (i + 1)
            perm[i], perm[j] = perm[j], perm[i]
        return perm

    def inverse_permutation(self, perm: list) -> list:
        inv = [0] * len(perm)
        for i, p in enumerate(perm):
            inv[p] = i
        return inv


# ============================================================================
# Index obfuscation (Block Shuffle + Byte Substitution)
# ============================================================================

_FIXED_SUB_CONSTANT = bytes.fromhex(
    "e7a3c1d5f28b0946"
    "1d7c3f9a5e6b8042"
    "b4f0d82e6c19a573"
    "90284d7bce1fa6e3"
)

def _generate_substitution_table(seed: int) -> bytes:
    """Generate a bijective 256-byte substitution table from seed."""
    rng = LitePRNG(seed ^ 0xA5A5C3C3)
    table = list(range(256))
    for i in range(255, 0, -1):
        j = rng.next_u32() % (i + 1)
        table[i], table[j] = table[j], table[i]
    return bytes(table)


def _inverse_substitution_table(table: bytes) -> bytes:
    inv = bytearray(256)
    for i in range(256):
        inv[table[i]] = i
    return bytes(inv)


def _keyed_round_seed(seed: int, key: bytes, round_index: int) -> int:
    base = (round_index * 7) & 31
    k0 = (key[base] |
          (key[(base + 1) & 31] << 8) |
          (key[(base + 2) & 31] << 16) |
          (key[(base + 3) & 31] << 24))
    return (((seed & 0xFFFF) * 0x45D9F3B) ^ k0 ^ ((round_index * 0x9E3779B9) & 0xFFFFFFFF)) & 0xFFFFFFFF


def _xor_index_stream(data: bytearray, seed: int, key: bytes, round_index: int):
    pos = 0
    counter = 0
    while pos < len(data):
        block = (key[:32] + struct.pack("<H", seed & 0xFFFF) + bytes([round_index & 0xFF, 0, 0, 0]) +
                 struct.pack("<Q", counter))
        stream = hashlib.blake2b(block, digest_size=32, person=b"LiteIdxObV6").digest()
        for b in stream:
            if pos >= len(data):
                break
            data[pos] ^= b
            pos += 1
        counter += 1


def _obfuscate_index_round(data: bytes, round_seed: int) -> bytes:
    n_blocks = 256
    block_size = (len(data) + n_blocks - 1) // n_blocks
    rng = LitePRNG(round_seed ^ 0x5A5A9E9E)
    perm = rng.permutation(n_blocks)
    shuffled = bytearray()
    for idx in perm:
        start = idx * block_size
        end = min(start + block_size, len(data))
        if start < len(data):
            shuffled.extend(data[start:end])
    sub_table = _generate_substitution_table(round_seed)
    return bytes(sub_table[b] for b in shuffled)


def _deobfuscate_index_round(data: bytes, round_seed: int) -> bytes:
    sub_table = _generate_substitution_table(round_seed)
    inv_table = _inverse_substitution_table(sub_table)
    unsubbed = bytes(inv_table[b] for b in data)
    n_blocks = 256
    block_size = (len(data) + n_blocks - 1) // n_blocks
    rng = LitePRNG(round_seed ^ 0x5A5A9E9E)
    perm = rng.permutation(n_blocks)
    original_sizes = []
    for i in range(n_blocks):
        start = i * block_size
        end = min(start + block_size, len(data))
        original_sizes.append((end - start) if start < len(data) else 0)
    shuffled_sizes = [original_sizes[perm[i]] for i in range(n_blocks)]
    out = bytearray(len(data))
    pos = 0
    for i, sz in enumerate(shuffled_sizes):
        orig_idx = perm[i]
        orig_start = orig_idx * block_size
        if sz > 0 and orig_start < len(data):
            out[orig_start:orig_start + sz] = unsubbed[pos:pos + sz]
        pos += sz
    return bytes(out)


def obfuscate_index(data: bytes, seed: int, pre_master_key: bytes) -> bytes:
    if len(data) == 0:
        return data
    rounds = 3 + (seed % 5)
    current = bytes(data)
    for r in range(rounds):
        current = _obfuscate_index_round(current, _keyed_round_seed(seed, pre_master_key, r))
        tmp = bytearray(current)
        _xor_index_stream(tmp, seed, pre_master_key, r)
        current = bytes(tmp)
    return current


def deobfuscate_index(data: bytes, seed: int, pre_master_key: bytes) -> bytes:
    if len(data) == 0:
        return data
    rounds = 3 + (seed % 5)
    current = bytes(data)
    for r in range(rounds - 1, -1, -1):
        tmp = bytearray(current)
        _xor_index_stream(tmp, seed, pre_master_key, r)
        current = _deobfuscate_index_round(bytes(tmp), _keyed_round_seed(seed, pre_master_key, r))
    return current


# ============================================================================
# Entry field obfuscation
# ============================================================================

def _entry_obfuscation_mask(path_hash: bytes, file_id: bytes, seed: int, v6_features: int) -> bytes:
    return hashlib.blake2b(
        path_hash + file_id + struct.pack("<H", seed) + struct.pack("<I", v6_features),
        digest_size=20,
        person=b"LiteEOBv6",
    ).digest()


def obfuscate_entry_fields(entry: dict, seed: int, v6_features: int = V6_FEATURES) -> dict:
    mask = _entry_obfuscation_mask(entry["hash_bytes"], entry.get("file_id", b"\x00" * FILE_ID_SIZE), seed, v6_features)
    ref_start_mask = struct.unpack_from("<I", mask, 0)[0]
    count_mask = struct.unpack_from("<I", mask, 4)[0]
    size_mask = struct.unpack_from("<Q", mask, 8)[0]
    crc_mask = struct.unpack_from("<I", mask, 16)[0]
    e = dict(entry)
    e["chunk_ref_start"] = entry["chunk_ref_start"] ^ ref_start_mask
    e["chunk_count"] = entry["chunk_count"] ^ count_mask
    e["original_size"] = entry["original_size"] ^ size_mask
    e["file_crc32c"] = entry["file_crc32c"] ^ crc_mask
    return e


def deobfuscate_entry_fields(entry: dict, seed: int, v6_features: int = V6_FEATURES) -> dict:
    return obfuscate_entry_fields(entry, seed, v6_features)


# ============================================================================
# Weighted LRU chunk cache (bytes × frequency × cost)
# ============================================================================

class ChunkPlaintextCache:
    """Weighted eviction cache: score = freq * cost / size.
    Uses a min-heap for O(log n) eviction instead of O(n) scan."""

    def __init__(self, max_entries: int = CHUNK_CACHE_MAX_ENTRIES,
                 max_bytes: int = CHUNK_CACHE_MAX_BYTES):
        self.max_entries = max_entries
        self.max_bytes = max_bytes
        self._lock = threading.Lock()
        self._map: dict[tuple, bytes] = {}
        self._freq: dict[tuple, int] = {}
        self._cost: dict[tuple, float] = {}
        self._current_bytes = 0
        self._heap: list[tuple[float, int, tuple]] = []  # (neg_score, tiebreak, key)
        self._heap_seq = 0

    def get(self, key: tuple):
        with self._lock:
            value = self._map.get(key)
            if value is None:
                return None
            self._freq[key] = self._freq.get(key, 0) + 1
            return value

    def put(self, key: tuple, value: bytes, decode_cost_ms: float = 1.0):
        import heapq
        size = len(value)
        with self._lock:
            old = self._map.get(key)
            if old is not None:
                self._current_bytes -= len(old)
            self._map[key] = value
            self._freq[key] = self._freq.get(key, 0) + 1
            self._cost[key] = max(0.001, decode_cost_ms)
            self._current_bytes += size
            # Push to heap: score = freq * cost / size (lower = more evictable)
            freq = self._freq[key]
            cost = self._cost[key]
            score = (freq * cost) / max(1, size)
            self._heap_seq += 1
            heapq.heappush(self._heap, (score, self._heap_seq, key))
            while self._map and (len(self._map) > self.max_entries or self._current_bytes > self.max_bytes):
                self._evict_one()

    def _evict_one(self):
        import heapq
        while self._heap:
            _score, _seq, key = heapq.heappop(self._heap)
            if key in self._map:
                evicted = self._map.pop(key)
                self._current_bytes -= len(evicted)
                self._freq.pop(key, None)
                self._cost.pop(key, None)
                return
        # Fallback: evict arbitrary entry
        if self._map:
            key = next(iter(self._map))
            evicted = self._map.pop(key)
            self._current_bytes -= len(evicted)
            self._freq.pop(key, None)
            self._cost.pop(key, None)


# ============================================================================
# Crypto – AES-256-GCM (primary) + legacy XOR fallback
# ============================================================================

def _ensure_aesgcm():
    if not _HAS_AESGCM:
        raise RuntimeError("需要安装 cryptography 库: pip install cryptography")


def aes_gcm_encrypt(data: bytes, key: bytes, nonce: bytes) -> bytes:
    """Encrypt with AES-256-GCM. Returns ciphertext+tag (16 bytes appended)."""
    _ensure_aesgcm()
    aesgcm = AESGCM(key[:32])
    return aesgcm.encrypt(nonce[:12], data, None)


def aes_gcm_decrypt(data: bytes, key: bytes, nonce: bytes) -> bytes:
    """Decrypt AES-256-GCM. Input is ciphertext+tag."""
    _ensure_aesgcm()
    aesgcm = AESGCM(key[:32])
    return aesgcm.decrypt(nonce[:12], data, None)



# ============================================================================
# White-box key derivation (simulated with S-box network)
# ============================================================================

def _rotl32(v: int, r: int) -> int:
    return ((v << r) | (v >> (32 - r))) & 0xFFFFFFFF


def _whitebox_transform(seed_a: bytes, seed_b: bytes, extra: bytes, strong_wb: bool = True) -> bytes:
    sbox = _select_wb_sbox(strong_wb)
    state = bytearray(64)
    state[:32] = hashlib.blake2b(seed_a + extra, digest_size=32, person=b"LiteWB_A").digest()
    state[32:] = hashlib.blake2b(seed_b + extra, digest_size=32, person=b"LiteWB_B").digest()

    tables = []
    for t in range(4):
        seed = bytes([t]) + seed_a + seed_b
        digest = hashlib.blake2b(seed + extra, digest_size=32, person=b"LiteTblV6").digest()
        table = bytearray(256)
        for i in range(256):
            table[i] = (sbox[i ^ digest[i & 31]] + digest[(i + t * 7) & 31] + i * (t * 2 + 1)) & 0xFF
        tables.append(bytes(table))

    for r in range(10):
        for i in range(64):
            state[i] = tables[(i + r) & 3][state[i]] ^ tables[(i + r + 1) & 3][(state[(i + 17) & 63] + r + i) & 0xFF]
        left = bytearray(state[:32])
        right = bytearray(state[32:])
        f_out = hashlib.blake2b(bytes(right), digest_size=32,
                                key=struct.pack("<I", r).ljust(8, b"\x00"),
                                person=b"LiteWBRd").digest()
        for i in range(32):
            left[i] ^= f_out[i] ^ tables[(r + i) & 3][right[(i * 5 + r) & 31]]
        state[:32], state[32:] = right, left
    return hashlib.blake2b(bytes(state), digest_size=32, person=b"LiteWBOt").digest()


def derive_pre_master_key(k2: bytes, k8: bytes, strong_wb: bool = True) -> bytes:
    """White-box protected pre-master key derivation."""
    raw = _whitebox_transform(
        DLL_ROOT_SEED_A, DLL_ROOT_SEED_B,
        MAGIC + bytes([VERSION]) + k2 + k8,
        strong_wb,
    )
    # Final BLAKE2b to match expected format
    h = hashlib.blake2b(raw, digest_size=32, person=b"LitePreV6")
    h.update(k2)
    h.update(k8)
    return h.digest()


def derive_full_master_key(k2: bytes, k6: bytes, k8: bytes, k9: bytes, k10: bytes,
                           strong_wb: bool = True) -> bytes:
    """White-box protected full master key derivation."""
    raw = _whitebox_transform(
        DLL_ROOT_SEED_B, DLL_ROOT_SEED_A,
        MAGIC + bytes([VERSION]) + k2 + k6 + k8 + k9 + k10,
        strong_wb,
    )
    h = hashlib.blake2b(raw, digest_size=32, person=b"LiteMKV6")
    h.update(k2)
    h.update(k6)
    h.update(k8)
    h.update(k9)
    h.update(k10)
    return h.digest()


def derive_file_id(path_hash: bytes, content_id: bytes, original_size: int) -> bytes:
    return hashlib.blake2b(path_hash + content_id + struct.pack("<Q", original_size),
                           digest_size=16, person=b"LiteFileV6").digest()


def compute_key_material_signature(k2: bytes, k6: bytes, k8: bytes, k9: bytes, k10: bytes,
                                   v6_features: int, pre_master_key: bytes,
                                   full_master_key: bytes) -> bytes:
    pre_hash = hashlib.blake2b(pre_master_key, digest_size=32, person=b"LiteKPrV6").digest()
    full_hash = hashlib.blake2b(full_master_key, digest_size=32, person=b"LiteKFuV6").digest()
    material = k2 + k6 + k8 + k9 + k10 + struct.pack("<I", v6_features) + pre_hash + full_hash
    return hashlib.blake2b(material, digest_size=32, person=b"LiteKMSV6").digest()


def derive_index_key(pre_master_key: bytes, plain_sz: int) -> bytes:
    h = hashlib.blake2b(key=pre_master_key, digest_size=32, person=b"LiteIdKV6")
    h.update(b"INDEX")
    h.update(b"__cdc_idx__")
    h.update(struct.pack("<Q", plain_sz))
    return h.digest()


def derive_k9_chunk_key(pre_master_key: bytes, nonce: bytes, plain_sz: int, chunk_kind: int) -> bytes:
    h = hashlib.blake2b(key=pre_master_key, digest_size=32, person=b"LiteK9V6")
    h.update(b"K9CH")
    h.update(nonce)
    h.update(struct.pack("<Q", plain_sz))
    h.update(bytes([chunk_kind]))
    return h.digest()


def derive_file_chunk_key(full_master_key: bytes, nonce: bytes, plain_sz: int, chunk_kind: int) -> bytes:
    h = hashlib.blake2b(key=full_master_key, digest_size=32, person=b"LiteDtV6")
    h.update(b"CHNK")
    h.update(nonce)
    h.update(struct.pack("<Q", plain_sz))
    h.update(bytes([chunk_kind]))
    return h.digest()


# ============================================================================
# Segmented key derivation (§3.8)
# ============================================================================

def derive_segment_open_key(master_key: bytes, nonce: bytes, chunk_kind: int) -> bytes:
    """Derive key for the first SEGMENT_BOUNDARY bytes of a chunk."""
    h = hashlib.blake2b(key=master_key, digest_size=32, person=b"LiteOpV6")
    h.update(b"OPEN")
    h.update(nonce)
    h.update(struct.pack("<I", SEGMENT_BOUNDARY))
    h.update(bytes([chunk_kind]))
    return h.digest()


def derive_segment_read_key(master_key: bytes, nonce: bytes, remaining_len: int,
                             open_key: bytes, first_plain_bytes: bytes, chunk_kind: int) -> bytes:
    """Derive key for remaining bytes, dependent on first SEGMENT_DEPEND_LEN plaintext bytes."""
    depend = first_plain_bytes[:SEGMENT_DEPEND_LEN].ljust(SEGMENT_DEPEND_LEN, b"\x00")
    h = hashlib.blake2b(key=master_key, digest_size=32, person=b"LiteRdV6")
    h.update(b"READ")
    h.update(nonce)
    h.update(struct.pack("<Q", remaining_len))
    h.update(open_key)
    h.update(depend)
    h.update(bytes([chunk_kind]))
    return h.digest()


def segmented_encrypt(plain_packed: bytes, master_key: bytes, nonce: bytes,
                       chunk_kind: int, original_size: int,
                       is_k9: bool = False) -> bytes:
    """Encrypt a packed chunk with segmented key derivation.
    original_size: uncompressed chunk size, used for key derivation consistency."""
    if is_k9:
        key = derive_k9_chunk_key(master_key, nonce, original_size, chunk_kind)
        return aes_gcm_encrypt(plain_packed, key, nonce)

    if len(plain_packed) <= SEGMENT_BOUNDARY:
        # Small chunk: single segment with open key
        open_key = derive_segment_open_key(master_key, nonce, chunk_kind)
        return aes_gcm_encrypt(plain_packed, open_key, nonce)

    # Large chunk: two segments
    seg1 = plain_packed[:SEGMENT_BOUNDARY]
    seg2 = plain_packed[SEGMENT_BOUNDARY:]
    open_key = derive_segment_open_key(master_key, nonce, chunk_kind)
    enc1 = aes_gcm_encrypt(seg1, open_key, nonce)
    # nonce2 is derived from nonce to avoid reuse
    nonce2 = hashlib.blake2b(nonce + b"SEG2", digest_size=NONCE_SIZE, person=b"LiteN2V6").digest()
    read_key = derive_segment_read_key(master_key, nonce, len(seg2), open_key, seg1, chunk_kind)
    enc2 = aes_gcm_encrypt(seg2, read_key, nonce2)
    # Store: [4-byte seg1_enc_len][enc1][enc2]
    return struct.pack("<I", len(enc1)) + enc1 + enc2


def segmented_decrypt(enc_data: bytes, master_key: bytes, nonce: bytes,
                       original_size: int, chunk_kind: int, is_k9: bool = False) -> bytes:
    """Decrypt with segmented key derivation.
    original_size: uncompressed chunk size (same value used during encrypt)."""
    if is_k9:
        key = derive_k9_chunk_key(master_key, nonce, original_size, chunk_kind)
        return aes_gcm_decrypt(enc_data, key, nonce)

    # Detect segmentation: if encrypted data starts with a 4-byte length header
    # and total size suggests two segments, it's segmented.
    # Simple heuristic: single-segment encrypted data = GCM(packed) where
    # packed <= SEGMENT_BOUNDARY, so enc size <= SEGMENT_BOUNDARY + GCM_TAG_SIZE.
    # Segmented data = 4 + enc1 + enc2 which is always > SEGMENT_BOUNDARY + GCM_TAG_SIZE + 4.
    single_max = SEGMENT_BOUNDARY + GCM_TAG_SIZE
    if len(enc_data) <= single_max:
        open_key = derive_segment_open_key(master_key, nonce, chunk_kind)
        return aes_gcm_decrypt(enc_data, open_key, nonce)

    seg1_enc_len = struct.unpack_from("<I", enc_data, 0)[0]
    enc1 = enc_data[4:4 + seg1_enc_len]
    enc2 = enc_data[4 + seg1_enc_len:]
    open_key = derive_segment_open_key(master_key, nonce, chunk_kind)
    seg1 = aes_gcm_decrypt(enc1, open_key, nonce)
    nonce2 = hashlib.blake2b(nonce + b"SEG2", digest_size=NONCE_SIZE, person=b"LiteN2V6").digest()
    read_key = derive_segment_read_key(master_key, nonce, len(enc2) - GCM_TAG_SIZE,
                                        open_key, seg1, chunk_kind)
    seg2 = aes_gcm_decrypt(enc2, read_key, nonce2)
    return seg1 + seg2


# ============================================================================
# Ed25519 signing / verification
# ============================================================================

def _get_signing_seed(key_path: str | None = None) -> bytes:
    if key_path and os.path.exists(key_path):
        return Path(key_path).read_bytes()[:32]
    return _ED25519_SEED[:32]


def sign_trailer_data(trailer_data: bytes, key_path: str | None = None) -> bytes:
    """Sign trailer data with Ed25519, return 64-byte signature."""
    seed = _get_signing_seed(key_path)
    if _HAS_NACL_ED25519:
        return SigningKey(seed).sign(trailer_data).signature
    if _HAS_CRYPTO_ED25519:
        return crypto_ed25519.Ed25519PrivateKey.from_private_bytes(seed).sign(trailer_data)
    raise RuntimeError("需要安装 PyNaCl 或 cryptography 才能创建 Ed25519 签名")


def verify_trailer_signature(trailer_data: bytes, signature: bytes, key_path: str | None = None) -> bool:
    """Verify Ed25519 signature on trailer data."""
    seed = _get_signing_seed(key_path)
    if _HAS_NACL_ED25519:
        try:
            SigningKey(seed).verify_key.verify(trailer_data, signature)
            return True
        except BadSignatureError:
            return False
    if _HAS_CRYPTO_ED25519:
        try:
            crypto_ed25519.Ed25519PrivateKey.from_private_bytes(seed).public_key().verify(signature, trailer_data)
            return True
        except InvalidSignature:
            return False
    return False


# ============================================================================
# Compression
# ============================================================================

def _lzma_props_bytes(lc: int, lp: int, pb: int, dict_size: int) -> bytes:
    return bytes([(pb * 5 + lp) * 9 + lc]) + struct.pack("<I", dict_size)


def _lzma_filters(lc: int, lp: int, pb: int, dict_size: int):
    return [{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": dict_size}]


def _sanitize_lzma_params():
    lc = max(0, min(8, int(LZMA_LC)))
    lp = max(0, min(4, int(LZMA_LP)))
    pb = max(0, min(4, int(LZMA_PB)))
    if lc + lp > 4:
        lc = max(0, 4 - lp)
    return lc, lp, pb, max(1 << 16, min(1 << 30, int(LZMA_DICT_SIZE)))


def compress_chunk(data: bytes, method: str = "auto", whole_file_mode: bool = False) -> bytes:
    c = method.lower()
    if c == "raw":
        return bytes([MODE_RAW]) + data
    if c == "zlib":
        z = zlib.compress(data, 9)
        return bytes([MODE_ZLIB]) + z if len(z) + 1 < len(data) else bytes([MODE_RAW]) + data
    if c == "zstd":
        if zstd is None:
            raise ValueError("zstandard 未安装")
        z = zstd.ZstdCompressor(level=18 if whole_file_mode else 12).compress(data)
        return bytes([MODE_ZSTD]) + z if len(z) + 1 < len(data) else bytes([MODE_RAW]) + data
    if c == "lzma":
        lc, lp, pb, ds = _sanitize_lzma_params()
        props = _lzma_props_bytes(lc, lp, pb, ds)
        z = lzma.compress(data, format=lzma.FORMAT_RAW, filters=_lzma_filters(lc, lp, pb, ds))
        z = props + z
        return bytes([MODE_LZMA]) + z if len(z) + 1 < len(data) else bytes([MODE_RAW]) + data
    if c == "auto":
        size = len(data)
        if whole_file_mode:
            candidates = ["zstd", "zlib", "lzma"]
        elif size <= 8 * 1024:
            candidates = ["zlib"]
        elif size <= 64 * 1024:
            candidates = ["zstd", "zlib"]
        else:
            candidates = ["zstd", "lzma"] if zstd is not None else ["lzma", "zlib"]
        best = bytes([MODE_RAW]) + data
        for m in candidates:
            try:
                candidate = compress_chunk(data, m, whole_file_mode=whole_file_mode)
                if len(candidate) < len(best):
                    best = candidate
            except Exception:
                continue
        return best
    raise ValueError(f"不支持的压缩参数: {method}")


def decompress_chunk(data: bytes) -> bytes:
    if not data:
        raise ValueError("压缩数据为空")
    mode = data[0]
    payload = data[1:]
    if mode == MODE_RAW:
        return payload
    if mode == MODE_ZLIB:
        return zlib.decompress(payload)
    if mode == MODE_ZSTD:
        if zstd is None:
            raise ValueError("zstandard 未安装")
        return zstd.ZstdDecompressor().decompress(payload)
    if mode == MODE_LZMA:
        if len(payload) < 5:
            raise ValueError("LZMA 数据损坏")
        prop0 = payload[0]
        ds = struct.unpack("<I", payload[1:5])[0]
        if prop0 >= 9 * 5 * 5:
            raise ValueError("LZMA 属性非法")
        return lzma.decompress(
            payload[5:],
            format=lzma.FORMAT_RAW,
            filters=_lzma_filters(prop0 % 9, (prop0 // 9) % 5, (prop0 // 9) // 5, ds),
        )
    raise ValueError(f"未知压缩模式: {mode}")


def mode_name(mode: int) -> str:
    return {
        MODE_RAW: "raw",
        MODE_ZLIB: "zlib",
        MODE_ZSTD: "zstd",
        MODE_LZMA: "lzma",
    }.get(mode, f"unknown({mode})")


# ============================================================================
# Streaming CDC chunking
# ============================================================================

def split_chunks_cdc(data: bytes, cdc_params: dict | None = None) -> list[bytes]:
    """Non-streaming CDC for compatibility."""
    params = cdc_params or CDC_PARAMS
    min_size = params["min_size"]
    avg_size = params["avg_size"]
    max_size = params["max_size"]
    mask_early = params["mask_early"]
    mask_late = params["mask_late"]

    if not data:
        return [b""]
    if len(data) <= min_size:
        return [data]

    chunks = []
    pos = 0
    size = len(data)
    normal_cut = avg_size

    while pos < size:
        max_end = min(pos + max_size, size)
        min_end = min(pos + min_size, size)
        if max_end <= min_end:
            chunks.append(data[pos:max_end])
            break

        h = 0
        cut = None
        i = min_end
        while i < max_end:
            h = ((h << 1) + GEAR_TABLE[data[i]]) & 0xFFFFFFFFFFFFFFFF
            rel = i - pos
            mask = mask_early if rel < normal_cut else mask_late
            if (h & mask) == 0:
                cut = i + 1
                break
            i += 1

        if cut is None:
            cut = max_end
        chunks.append(data[pos:cut])
        pos = cut
    return chunks


class StreamingCDCSplitter:
    """Streaming CDC splitter: feed data incrementally, emit chunks without
    holding the entire file in memory."""

    def __init__(self, cdc_params: dict | None = None):
        self._params = cdc_params or CDC_PARAMS
        self._buf = bytearray()
        self._chunks: list[bytes] = []
        self._finalized = False

    def feed(self, data: bytes):
        """Feed a block of data into the splitter."""
        self._buf.extend(data)
        self._try_split()

    def finalize(self) -> list[bytes]:
        """Finalize and return all remaining chunks (respecting max_size)."""
        if self._buf:
            # Remaining buffer may exceed max_size; re-split with standard CDC
            remaining = bytes(self._buf)
            self._buf.clear()
            if len(remaining) <= self._params["max_size"]:
                self._chunks.append(remaining)
            else:
                self._chunks.extend(split_chunks_cdc(remaining, self._params))
        self._finalized = True
        result = self._chunks
        self._chunks = []
        return result

    def drain_ready(self) -> list[bytes]:
        """Drain chunks that are ready (buffer may still hold partial data)."""
        ready = self._chunks
        self._chunks = []
        return ready

    def _try_split(self):
        params = self._params
        min_size = params["min_size"]
        avg_size = params["avg_size"]
        max_size = params["max_size"]
        mask_early = params["mask_early"]
        mask_late = params["mask_late"]

        buf = self._buf
        while len(buf) > max_size:
            # Enough data for at least one cut attempt
            h = 0
            cut = None
            i = min_size
            limit = min(max_size, len(buf))
            while i < limit:
                h = ((h << 1) + GEAR_TABLE[buf[i]]) & 0xFFFFFFFFFFFFFFFF
                rel = i
                mask = mask_early if rel < avg_size else mask_late
                if (h & mask) == 0:
                    cut = i + 1
                    break
                i += 1
            if cut is None:
                cut = max_size
            self._chunks.append(bytes(buf[:cut]))
            del buf[:cut]


# ============================================================================
# Header / index / trailer IO
# ============================================================================

def read_header(fp) -> dict:
    raw = fp.read(HEADER_SIZE)
    if len(raw) != HEADER_SIZE:
        raise ValueError(f"封包太小: 期望 {HEADER_SIZE}B 头部, 实际 {len(raw)}B")
    magic, ver, flags, file_count, idx_off, idx_enc_sz, idx_plain_sz, idx_nonce, hdr_crc, k2, k8, seed, v6_features, hdr_sz = struct.unpack(
        f"<7s B H I Q Q Q {NONCE_SIZE}s I 16s 16s H I I", raw
    )
    if magic != MAGIC:
        raise ValueError(f"魔术头不匹配: {magic!r}")
    if ver != VERSION:
        raise ValueError(f"版本不支持: {ver}")
    if hdr_sz != HEADER_SIZE:
        raise ValueError(f"header_size 不匹配: {hdr_sz}")
    if (v6_features & V6_FEATURES) != V6_FEATURES:
        raise ValueError(f"v6 feature profile 不完整: 0x{v6_features:08X}")
    if crc32c(raw[:50]) != hdr_crc:
        raise ValueError("Header CRC32C 校验失败")
    required_flags = (FLAG_HAS_TRAILER | FLAG_CHUNK_INDEX | FLAG_FULL_VERIFY |
                      FLAG_AES_GCM | FLAG_ED25519_SIGNED |
                      FLAG_INDEX_OBFUSCATED | FLAG_WB_STRONG)
    if (flags & required_flags) != required_flags:
        raise ValueError(f"缺少必需安全 flags: 0x{flags:04X}")
    return {
        "version": ver,
        "flags": flags,
        "file_count": file_count,
        "index_offset": idx_off,
        "index_encrypted_sz": idx_enc_sz,
        "index_plain_sz": idx_plain_sz,
        "index_nonce": idx_nonce,
        "k2": k2,
        "k8": k8,
        "seed": seed,
        "v6_features": v6_features,
        "has_trailer": bool(flags & FLAG_HAS_TRAILER),
        "index_compressed": bool(flags & FLAG_INDEX_COMPRESSED),
        "chunk_index": bool(flags & FLAG_CHUNK_INDEX),
        "aes_gcm": bool(flags & FLAG_AES_GCM),
        "ed25519_signed": bool(flags & FLAG_ED25519_SIGNED),
        "index_obfuscated": bool(flags & FLAG_INDEX_OBFUSCATED),
        "strong_wb": bool(flags & FLAG_WB_STRONG),
    }


def _build_header(file_count: int, idx_offset: int, idx_enc_sz: int, idx_plain_sz: int,
                  idx_nonce: bytes, flags: int, k2: bytes, k8: bytes, seed: int = 0,
                  v6_features: int = V6_FEATURES) -> bytes:
    raw = bytearray(HEADER_SIZE)
    struct.pack_into(
        f"<7s B H I Q Q Q {NONCE_SIZE}s",
        raw, 0, MAGIC, VERSION, flags, file_count,
        idx_offset, idx_enc_sz, idx_plain_sz, idx_nonce,
    )
    raw[54:70] = k2
    raw[70:86] = k8
    struct.pack_into("<H", raw, 86, seed)
    struct.pack_into("<I", raw, 88, v6_features)
    struct.pack_into("<I", raw, 92, HEADER_SIZE)
    struct.pack_into("<I", raw, 50, crc32c(bytes(raw[:50])))
    return bytes(raw)


def decrypt_index(fp, header: dict, pre_master_key: bytes) -> bytes:
    fp.seek(header["index_offset"])
    enc = fp.read(header["index_encrypted_sz"])
    if len(enc) != header["index_encrypted_sz"]:
        raise ValueError("索引区读取不足")
    idx_key = derive_index_key(pre_master_key, header["index_plain_sz"])

    payload = aes_gcm_decrypt(enc, idx_key, header["index_nonce"])

    if header["index_compressed"]:
        if zstd is None:
            raise RuntimeError("索引已压缩但 zstandard 未安装")
        plain = zstd.ZstdDecompressor().decompress(payload)
    else:
        plain = payload
    if len(plain) != header["index_plain_sz"]:
        raise ValueError(f"索引明文大小不匹配: {len(plain)} != {header['index_plain_sz']}")

    # Deobfuscate index if flag set
    if header.get("index_obfuscated"):
        plain = deobfuscate_index(plain, header["seed"], pre_master_key)

    return plain


def _build_index(chunk_records: list, chunk_refs: list, entries: list, k6: bytes, k10: bytes,
                 cdc_params: dict, whole_file_threshold: int, seed: int = 0,
                 v6_features: int = V6_FEATURES, obfuscate_entries: bool = True) -> bytes:
    chunk_fmt = f"<{CHUNK_HASH_SIZE}s Q Q Q {NONCE_SIZE}s I B B B {FILE_ID_SIZE}s"
    entry_fmt = f"<{PATH_HASH_SIZE}s {FILE_ID_SIZE}s B Q I I I"

    chunk_table = bytearray()
    for rec in chunk_records:
        chunk_table.extend(struct.pack(
            chunk_fmt,
            rec["chunk_hash"], rec["original_size"], rec["stored_size"],
            rec["data_offset"], rec["data_nonce"], rec["chunk_crc32c"], rec["chunk_kind"],
            rec.get("mode", MODE_RAW), rec.get("transform_flags", CHUNK_TRANSFORM_NONE),
            rec.get("file_id", b"\x00" * FILE_ID_SIZE),
        ))

    refs_blob = bytearray()
    for ref in chunk_refs:
        refs_blob.extend(struct.pack("<I", ref))

    entries_blob = bytearray()
    for ent in entries:
        if obfuscate_entries:
            e = obfuscate_entry_fields(ent, seed, v6_features)
        else:
            e = ent
        entries_blob.extend(struct.pack(
            entry_fmt,
            e["hash_bytes"], e.get("file_id", b"\x00" * FILE_ID_SIZE), e["flags"], e["original_size"],
            e["file_crc32c"], e["chunk_ref_start"], e["chunk_count"],
        ))

    chunk_table_hash = hash16_personal(bytes(chunk_table), b"LiteCTH6")
    chunk_refs_hash = hash16_personal(bytes(refs_blob), b"LiteCRH6")
    entries_hash = hash16_personal(bytes(entries_blob), b"LiteENH6")

    buf = bytearray()
    buf.extend(struct.pack("<I", len(entries)))
    buf.extend(struct.pack("<I", len(chunk_records)))
    buf.extend(struct.pack("<I", len(chunk_refs)))
    body_hash_pos = len(buf)
    buf.extend(b"\x00" * INDEX_HASH_SIZE)
    buf.extend(chunk_table_hash)
    buf.extend(chunk_refs_hash)
    buf.extend(entries_hash)
    buf.extend(struct.pack("<I", len(k6)))
    buf.extend(k6)
    buf.append(crc8(k6))
    buf.extend(struct.pack("<I", len(k10)))
    buf.extend(k10)
    buf.append(crc8(k10))
    buf.extend(struct.pack("<I", whole_file_threshold))
    buf.extend(struct.pack("<I", cdc_params["min_size"]))
    buf.extend(struct.pack("<I", cdc_params["avg_size"]))
    buf.extend(struct.pack("<I", cdc_params["max_size"]))
    pad_size_pos = len(buf)
    buf.extend(struct.pack("<I", 0))
    align = (16 - (len(buf) % 16)) % 16
    struct.pack_into("<I", buf, pad_size_pos, align)
    if align:
        buf.extend(secrets.token_bytes(align))
    buf.extend(chunk_table)
    buf.extend(refs_blob)
    buf.extend(entries_blob)

    body_hash = hash16_personal(bytes(buf[body_hash_pos + INDEX_HASH_SIZE:]), b"LiteBDH6")
    buf[body_hash_pos:body_hash_pos + INDEX_HASH_SIZE] = body_hash
    return bytes(buf)


def parse_index(index_plain: bytes, strict: bool = False, seed: int = 0,
                v6_features: int = V6_FEATURES, deobfuscate_entries: bool = True):
    pos = 0
    entry_count = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    chunk_count = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    chunk_ref_count = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    body_hash = index_plain[pos:pos + INDEX_HASH_SIZE]; pos += INDEX_HASH_SIZE
    chunk_table_hash = index_plain[pos:pos + INDEX_HASH_SIZE]; pos += INDEX_HASH_SIZE
    chunk_refs_hash = index_plain[pos:pos + INDEX_HASH_SIZE]; pos += INDEX_HASH_SIZE
    entries_hash = index_plain[pos:pos + INDEX_HASH_SIZE]; pos += INDEX_HASH_SIZE

    k6_size = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    k6 = index_plain[pos:pos + k6_size]; pos += k6_size
    if crc8(k6) != index_plain[pos]:
        raise ValueError("K6 CRC8 校验失败")
    pos += 1

    k10_size = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    k10 = index_plain[pos:pos + k10_size]; pos += k10_size
    if crc8(k10) != index_plain[pos]:
        raise ValueError("K10 CRC8 校验失败")
    pos += 1

    whole_file_threshold = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    cdc_min = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    cdc_avg = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    cdc_max = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    pad_size = struct.unpack_from("<I", index_plain, pos)[0]; pos += 4
    pos += pad_size

    chunk_fmt = f"<{CHUNK_HASH_SIZE}s Q Q Q {NONCE_SIZE}s I B B B {FILE_ID_SIZE}s"
    chunk_sz = struct.calcsize(chunk_fmt)
    chunk_table_start = pos
    chunk_records = []
    for _ in range(chunk_count):
        if pos + chunk_sz > len(index_plain):
            raise ValueError("chunk table 越界")
        ch, osz, ssz, off, nonce, ccrc, kind, mode, transform_flags, file_id = struct.unpack_from(chunk_fmt, index_plain, pos)
        pos += chunk_sz
        chunk_records.append({
            "chunk_hash": ch,
            "original_size": osz,
            "stored_size": ssz,
            "data_offset": off,
            "data_nonce": nonce,
            "chunk_crc32c": ccrc,
            "chunk_kind": kind,
            "mode": mode,
            "transform_flags": transform_flags,
            "file_id": file_id,
        })
    chunk_table_blob = index_plain[chunk_table_start:pos]

    refs_start = pos
    chunk_refs = []
    for _ in range(chunk_ref_count):
        if pos + 4 > len(index_plain):
            raise ValueError("chunk refs 越界")
        chunk_refs.append(struct.unpack_from("<I", index_plain, pos)[0])
        pos += 4
    chunk_refs_blob = index_plain[refs_start:pos]

    entry_fmt = f"<{PATH_HASH_SIZE}s {FILE_ID_SIZE}s B Q I I I"
    entry_sz = struct.calcsize(entry_fmt)
    entries_start = pos
    entries = []
    k9_entry = None
    for _ in range(entry_count):
        if pos + entry_sz > len(index_plain):
            raise ValueError("entry table 越界")
        hb, file_id, flags, osz, fcrc, ref_start, ref_count = struct.unpack_from(entry_fmt, index_plain, pos)
        pos += entry_sz
        entry = {
            "hash_bytes": hb,
            "hash_hex": hb.hex().upper(),
            "file_id": file_id,
            "flags": flags,
            "original_size": osz,
            "file_crc32c": fcrc,
            "chunk_ref_start": ref_start,
            "chunk_count": ref_count,
        }
        # Deobfuscate entry fields if requested
        if deobfuscate_entries:
            entry = deobfuscate_entry_fields(entry, seed, v6_features)
        entries.append(entry)
        if flags == ENTRY_KEY_PAYLOAD:
            k9_entry = entry
    entries_blob = index_plain[entries_start:pos]

    if hash16_personal(chunk_table_blob, b"LiteCTH6") != chunk_table_hash:
        if strict:
            raise ValueError("chunk_table_hash 不匹配")
        emit_log_line("  chunk_table_hash 不匹配")
    if hash16_personal(chunk_refs_blob, b"LiteCRH6") != chunk_refs_hash:
        if strict:
            raise ValueError("chunk_refs_hash 不匹配")
        emit_log_line("  chunk_refs_hash 不匹配")
    if hash16_personal(entries_blob, b"LiteENH6") != entries_hash:
        if strict:
            raise ValueError("entries_hash 不匹配")
        emit_log_line("  entries_hash 不匹配")
    if hash16_personal(index_plain[12 + INDEX_HASH_SIZE:], b"LiteBDH6") != body_hash:
        if strict:
            raise ValueError("index_body_hash 不匹配")
        emit_log_line("  index_body_hash 不匹配")

    return {
        "entries": entries,
        "chunk_records": chunk_records,
        "chunk_refs": chunk_refs,
        "k6": k6,
        "k10": k10,
        "k9_entry": k9_entry,
        "whole_file_threshold": whole_file_threshold,
        "cdc_min_size": cdc_min,
        "cdc_avg_size": cdc_avg,
        "cdc_max_size": cdc_max,
    }


def _build_trailer(fp, index_plain: bytes, data_end: int, header_len: int,
                   key_material_signature: bytes,
                   sign: bool = True, sign_key_path: str | None = None) -> bytes:
    saved_pos = fp.tell()
    fp.seek(0)
    header_bytes = fp.read(header_len)
    index_plain_hash = hashlib.blake2b(index_plain, digest_size=32, person=b"LiteIHv6").digest()
    fp.seek(HEADER_SIZE)
    data_area = fp.read(max(0, data_end - HEADER_SIZE))
    data_area_hash = hashlib.blake2b(data_area, digest_size=32, person=b"LiteDHv6").digest()
    header_chain_hash = hashlib.blake2b(header_bytes + index_plain_hash, digest_size=32, person=b"LiteHCv6").digest()[:8]
    raw = bytearray(TRAILER_SIZE)
    struct.pack_into("<32s 32s 8s 8s Q", raw, 0,
                     index_plain_hash, data_area_hash, header_chain_hash,
                     b"LiteTRLR", TRAILER_SIZE)
    raw[88:120] = key_material_signature
    trailer_self_hash = hashlib.blake2b(bytes(raw[:120]), digest_size=32, person=b"LiteTRv6").digest()
    raw[120:128] = trailer_self_hash[:8]
    # Ed25519 signature over trailer[0..128)
    if sign:
        if not _HAS_ED25519:
            raise RuntimeError("需要安装 PyNaCl 或 cryptography 才能创建 Ed25519 签名")
        sig = sign_trailer_data(bytes(raw[:128]), sign_key_path)
        raw[128:192] = sig
    fp.seek(saved_pos)
    return bytes(raw)


def read_trailer(fp, index_plain: bytes, header: dict, sign_key_path: str | None = None,
                 expected_key_material_signature: bytes | None = None) -> bool:
    try:
        fp.seek(-TRAILER_SIZE, os.SEEK_END)
    except Exception:
        return False
    raw = fp.read(TRAILER_SIZE)
    if len(raw) != TRAILER_SIZE:
        return False
    index_plain_hash = raw[0:32]
    data_area_hash = raw[32:64]
    header_chain_hash = raw[64:72]
    tmag = raw[72:80]
    tsz = struct.unpack_from("<Q", raw, 80)[0]
    key_material_signature = raw[88:120]
    trailer_self_hash = raw[120:128]
    signature = raw[128:192]

    if tmag != b"LiteTRLR" or tsz != TRAILER_SIZE:
        return False
    if expected_key_material_signature is not None and key_material_signature != expected_key_material_signature:
        return False
    if hashlib.blake2b(raw[:120], digest_size=32, person=b"LiteTRv6").digest()[:8] != trailer_self_hash:
        return False
    if hashlib.blake2b(index_plain, digest_size=32, person=b"LiteIHv6").digest() != index_plain_hash:
        return False
    fp.seek(HEADER_SIZE)
    data_area = fp.read(max(0, header["index_offset"] - HEADER_SIZE))
    if hashlib.blake2b(data_area, digest_size=32, person=b"LiteDHv6").digest() != data_area_hash:
        return False
    fp.seek(0)
    header_bytes = fp.read(50)
    if hashlib.blake2b(header_bytes + index_plain_hash, digest_size=32, person=b"LiteHCv6").digest()[:8] != header_chain_hash:
        return False
    # Verify Ed25519 signature
    if header.get("ed25519_signed"):
        if not _HAS_ED25519 or signature == b"\x00" * 64:
            return False
        if not verify_trailer_signature(raw[:128], signature, sign_key_path):
            return False
    return True


# ============================================================================
# FULL_VERIFY — complete same-spec implementation
# ============================================================================

def full_verify_pack(pak_path: Path, sign_key_path: str | None = None) -> dict:
    """Complete verification of a .lpk file: header CRC, index hashes,
    every chunk GCM tag / CRC, every file CRC, trailer hashes, Ed25519 signature.
    Returns a dict with pass/fail status for each check."""
    results = {}
    with pak_path.open("rb") as fp:
        # 1) Header
        try:
            header = read_header(fp)
            results["header_crc"] = True
        except ValueError as e:
            results["header_crc"] = False
            results["error"] = str(e)
            return results

        pre_master_key = derive_pre_master_key(header["k2"], header["k8"], True)

        # 2) Index decrypt + hash verification
        try:
            index_plain = decrypt_index(fp, header, pre_master_key)
            results["index_decrypt"] = True
        except Exception as e:
            results["index_decrypt"] = False
            results["error"] = str(e)
            return results

        try:
            meta = parse_index(index_plain, strict=True, seed=header.get("seed", 0),
                               v6_features=header.get("v6_features", V6_FEATURES))
            results["index_hashes"] = True
        except ValueError as e:
            results["index_hashes"] = False
            results["error"] = str(e)
            return results

        # 3) K9 and full master key
        if meta["k9_entry"] is None:
            results["k9_found"] = False
            return results
        results["k9_found"] = True

        try:
            k9_bytes = _decode_entry_bytes(str(pak_path), meta["k9_entry"], meta["chunk_records"],
                                           meta["chunk_refs"], pre_master_key, b"", None)
            full_master_key = derive_full_master_key(header["k2"], meta["k6"], header["k8"],
                                                      k9_bytes[:FRAGMENT_SIZE], meta["k10"],
                                                      True)
            key_material_signature = compute_key_material_signature(header["k2"], meta["k6"], header["k8"],
                                                                    k9_bytes[:FRAGMENT_SIZE], meta["k10"],
                                                                    header.get("v6_features", V6_FEATURES),
                                                                    pre_master_key, full_master_key)
            results["k9_decrypt"] = True
        except Exception as e:
            results["k9_decrypt"] = False
            results["error"] = str(e)
            return results

        # 4) Verify every chunk
        chunk_ok = 0
        chunk_fail = 0
        chunk_errors = []
        for i, rec in enumerate(meta["chunk_records"]):
            try:
                _read_chunk_plain(str(pak_path), rec, pre_master_key, full_master_key, None)
                chunk_ok += 1
            except Exception as e:
                chunk_fail += 1
                chunk_errors.append(f"chunk[{i}]: {type(e).__name__}: {e}")
        results["chunks_ok"] = chunk_ok
        results["chunks_fail"] = chunk_fail
        if chunk_errors:
            results["chunk_errors"] = chunk_errors

        # 5) Verify every file
        file_entries = [e for e in meta["entries"] if e["flags"] == ENTRY_FILE]
        file_ok = 0
        file_fail = 0
        file_errors = []
        for e in file_entries:
            try:
                _decode_entry_bytes(str(pak_path), e, meta["chunk_records"], meta["chunk_refs"],
                                    pre_master_key, full_master_key, None)
                file_ok += 1
            except Exception as ex:
                file_fail += 1
                file_errors.append(f"entry[{e['hash_hex']}]: {type(ex).__name__}: {ex}")
        results["files_ok"] = file_ok
        results["files_fail"] = file_fail
        if file_errors:
            results["file_errors"] = file_errors

        # 6) Trailer
        if header["has_trailer"]:
            trailer_ok = read_trailer(fp, index_plain, header, sign_key_path,
                                      key_material_signature)
            results["trailer"] = trailer_ok
        else:
            results["trailer"] = None

    results["overall"] = (
        results.get("header_crc") and results.get("index_decrypt") and
        results.get("index_hashes") and results.get("k9_decrypt") and
        results.get("chunks_fail", 0) == 0 and results.get("files_fail", 0) == 0 and
        results.get("trailer", True) is not False
    )
    return results


# ============================================================================
# Chunk read / entry read
# ============================================================================

def _chunk_cache_key(chunk_record: dict) -> tuple:
    return (
        chunk_record["chunk_hash"],
        chunk_record["original_size"],
        chunk_record["chunk_crc32c"],
        chunk_record["chunk_kind"],
    )


def apply_arx_transform(data: bytes | bytearray, full_master_key: bytes, file_id: bytes, nonce: bytes,
                        original_size: int) -> bytes:
    buf = bytearray(data)
    key = hashlib.blake2b(file_id + nonce + struct.pack("<Q", original_size), key=full_master_key,
                          digest_size=32, person=b"LiteARXV6").digest()
    a, b, c, d = struct.unpack_from("<IIII", key, 0)
    for i in range(len(buf)):
        a = (a + (b ^ i)) & 0xFFFFFFFF
        b = _rotl32((b + c + a) & 0xFFFFFFFF, 7)
        c = (c ^ ((d + b) & 0xFFFFFFFF)) & 0xFFFFFFFF
        d = _rotl32((d ^ a ^ c) & 0xFFFFFFFF, 11)
        buf[i] ^= (a ^ (b >> 8) ^ (c >> 16) ^ (d >> 24)) & 0xFF
    return bytes(buf)


def apply_feistel_transform(data: bytes | bytearray, full_master_key: bytes, file_id: bytes, nonce: bytes,
                            original_size: int) -> bytes:
    buf = bytearray(data)
    key = hashlib.blake2b(file_id + nonce + struct.pack("<Q", original_size), key=full_master_key,
                          digest_size=32, person=b"LiteFstV6").digest()
    limit = min(len(buf), SEGMENT_BOUNDARY)
    blocks = limit // 256
    for i in range(0, blocks - 1, 2):
        round_byte = key[i & 31]
        a0 = i * 256
        b0 = (i + 1) * 256
        for j in range(256):
            mask = (key[(j + round_byte) & 31] + j) & 0xFF
            buf[a0 + j] ^= mask
            buf[b0 + j] ^= mask
        left = bytes(buf[a0:a0 + 256])
        buf[a0:a0 + 256] = buf[b0:b0 + 256]
        buf[b0:b0 + 256] = left
    return bytes(buf)


def select_transform_flags(rel_path: str, original_size: int, mode: int = MODE_RAW) -> int:
    ext = Path(rel_path).suffix.lower()
    if original_size <= WHOLE_FILE_THRESHOLD and ext in {".ini", ".json", ".txt", ".ks", ".lua", ".js", ".cfg", ".xml", ".yaml", ".yml"}:
        return CHUNK_TRANSFORM_ARX
    if original_size >= SEGMENT_BOUNDARY and mode == MODE_RAW and ext in {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".ogg", ".mp3", ".wav", ".mp4", ".avi", ".mkv", ".dat", ".bin"}:
        return CHUNK_TRANSFORM_FEISTEL
    return CHUNK_TRANSFORM_NONE


def _decode_chunk_payload_gcm(enc: bytes, master_key: bytes, nonce: bytes,
                               plain_sz: int, expected_crc: int, chunk_kind: int,
                               transform_flags: int = CHUNK_TRANSFORM_NONE,
                               file_id: bytes = b"\x00" * FILE_ID_SIZE,
                               is_k9: bool = False) -> bytes:
    packed = segmented_decrypt(enc, master_key, nonce, plain_sz, chunk_kind, is_k9=is_k9)
    plain = decompress_chunk(packed)
    if len(plain) != plain_sz:
        raise ValueError(f"chunk 解压大小不匹配: {len(plain)} != {plain_sz}")
    if transform_flags & CHUNK_TRANSFORM_FEISTEL:
        plain = apply_feistel_transform(plain, master_key, file_id, nonce, plain_sz)
    if transform_flags & CHUNK_TRANSFORM_ARX:
        plain = apply_arx_transform(plain, master_key, file_id, nonce, plain_sz)
    if crc32c(plain) != expected_crc:
        raise ValueError("chunk CRC32C 校验失败")
    return plain


def _read_chunk_plain(pak_path_str: str, chunk_record: dict,
                      pre_master_key: bytes, full_master_key: bytes,
                      cache: ChunkPlaintextCache | None = None) -> bytes:
    cache_key = _chunk_cache_key(chunk_record)
    if cache is not None:
        cached = cache.get(cache_key)
        if cached is not None:
            return cached

    t0 = time.perf_counter()

    with open(pak_path_str, "rb") as fp:
        fp.seek(chunk_record["data_offset"])
        enc = fp.read(chunk_record["stored_size"])
    if len(enc) != chunk_record["stored_size"]:
        raise ValueError("chunk 读取不足")

    is_k9 = chunk_record["chunk_kind"] == CHUNK_KIND_K9
    master = pre_master_key if is_k9 else full_master_key

    plain = _decode_chunk_payload_gcm(
        enc, master, chunk_record["data_nonce"],
        chunk_record["original_size"], chunk_record["chunk_crc32c"],
        chunk_record["chunk_kind"], chunk_record.get("transform_flags", CHUNK_TRANSFORM_NONE),
        chunk_record.get("file_id", b"\x00" * FILE_ID_SIZE), is_k9=is_k9,
    )

    decode_cost_ms = (time.perf_counter() - t0) * 1000.0
    if cache is not None:
        cache.put(cache_key, plain, decode_cost_ms)
    return plain


def _decode_entry_bytes(pak_path_str: str, entry: dict,
                        chunk_records: list, chunk_refs: list,
                        pre_master_key: bytes, full_master_key: bytes,
                        cache: ChunkPlaintextCache | None = None) -> bytes:
    out = bytearray()
    start = entry["chunk_ref_start"]
    count = entry["chunk_count"]
    end = start + count
    if start < 0 or count < 0 or end > len(chunk_refs):
        raise ValueError("chunk refs 越界")
    for ref_index in chunk_refs[start:end]:
        if ref_index < 0 or ref_index >= len(chunk_records):
            raise ValueError("chunk record 索引越界")
        chunk_record = chunk_records[ref_index]
        out.extend(_read_chunk_plain(pak_path_str, chunk_record, pre_master_key,
                                      full_master_key, cache))
    plain = bytes(out)
    if len(plain) != entry["original_size"]:
        raise ValueError(f"文件解包大小不匹配: {len(plain)} != {entry['original_size']}")
    if crc32c(plain) != entry["file_crc32c"]:
        raise ValueError("文件 CRC32C 校验失败")
    return plain


def _decode_entry_task(args):
    return _decode_entry_bytes(*args)


# ============================================================================
# Manifest
# ============================================================================

def write_manifest(manifest_path: Path, rel_paths: list):
    lines = [f"{lite_hash(rel)}\t{lite_hash(rel)}\t{rel}" for rel in rel_paths]
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text("\n".join(lines), encoding="utf-8")


def safe_output_path(output_dir: Path, rel: str) -> Path:
    rel_path = Path(rel)
    if rel_path.is_absolute() or ".." in rel_path.parts:
        raise ValueError(f"清单路径不安全: {rel}")
    root = output_dir.resolve()
    out_path = (output_dir / rel_path).resolve()
    try:
        out_path.relative_to(root)
    except ValueError:
        raise ValueError(f"清单路径越界: {rel}") from None
    return out_path


def load_manifest(manifest_path: Path) -> list:
    mapping = []
    for line in manifest_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) == 2:
            mapping.append((parts[0].upper(), parts[0], parts[1]))
        elif len(parts) == 3:
            mapping.append((parts[0].upper(), parts[1], parts[2]))
        else:
            raise ValueError(f"清单格式错误: {line}")
    return mapping


# ============================================================================
# Pack
# ============================================================================

def collect_files(input_dir: Path) -> list:
    files = []
    for fp in input_dir.rglob("*"):
        if fp.is_file():
            files.append((fp, fp.relative_to(input_dir).as_posix()))
    return sorted(files, key=lambda x: x[1])


def _prepare_pack_item_streaming(task):
    """Streaming version: for large files, use streaming CDC instead of reading
    entire file into memory at once."""
    src_str, rel, cdc_params, whole_threshold = task
    src = Path(src_str)
    file_size = src.stat().st_size

    if file_size <= whole_threshold:
        # Small file: read entirely (it's small anyway)
        raw = src.read_bytes()
        return {
            "rel": rel,
            "raw": raw,
            "chunks": None,  # will be treated as whole
            "raw_size": len(raw),
            "hash_bytes": lite_hash_bytes(rel),
            "file_crc32c": crc32c(raw),
            "content_id": hashlib.blake2b(raw, digest_size=32, person=b"LiteDedV6").digest(),
        }

    # Large file: streaming CDC — never holds full file in memory
    splitter = StreamingCDCSplitter(cdc_params)
    h = hashlib.blake2b(digest_size=32, person=b"LiteDedV6")
    file_crc = 0
    total_size = 0
    chunks = []
    with open(src_str, "rb") as f:
        while True:
            block = f.read(IO_CHUNK_SIZE)
            if not block:
                break
            h.update(block)
            file_crc = crc32c(block, file_crc)
            total_size += len(block)
            splitter.feed(block)
            chunks.extend(splitter.drain_ready())
    chunks.extend(splitter.finalize())

    return {
        "rel": rel,
        "raw": None,  # not stored in memory for large files
        "chunks": chunks,
        "raw_size": total_size,
        "hash_bytes": lite_hash_bytes(rel),
        "file_crc32c": file_crc,
        "content_id": h.digest(),
    }


def _prepare_pack_item(task):
    """Legacy non-streaming prepare (for backward compat with simple task tuples)."""
    src_str, rel = task
    raw = Path(src_str).read_bytes()
    return {
        "rel": rel,
        "raw": raw,
        "chunks": None,
        "raw_size": len(raw),
        "hash_bytes": lite_hash_bytes(rel),
        "file_crc32c": crc32c(raw),
        "content_id": hashlib.blake2b(raw, digest_size=32, person=b"LiteDedV6").digest(),
    }


def is_whole_file_mode(file_size: int, whole_file_threshold: int) -> bool:
    return file_size <= whole_file_threshold


def summarize_modes(chunk_records: list, chunk_indices: list) -> tuple[str, int]:
    counts = {MODE_RAW: 0, MODE_ZLIB: 0, MODE_ZSTD: 0, MODE_LZMA: 0}
    stored_total = 0
    for idx in chunk_indices:
        mode = chunk_records[idx].get("mode", MODE_RAW)
        counts[mode] = counts.get(mode, 0) + 1
        stored_total += chunk_records[idx]["stored_size"]
    parts = [f"{mode_name(k)}:{v}" for k, v in counts.items() if v]
    return (",".join(parts) if parts else "raw:0", stored_total)


def _add_chunk(fp, chunk_map: dict, chunk_records: list, chunk_payload: bytes,
               chunk_kind: int, pre_master_key: bytes, full_master_key: bytes,
               compression: str, rel_path: str = "", file_id: bytes = b"\x00" * FILE_ID_SIZE,
               allow_reuse: bool = True, whole_file_mode: bool = False,
               is_k9: bool = False) -> tuple[int, bool]:
    chunk_hash = chunk_hash_bytes(chunk_payload)
    chunk_crc = crc32c(chunk_payload)
    transform_flags = CHUNK_TRANSFORM_NONE if is_k9 else select_transform_flags(rel_path, len(chunk_payload), MODE_RAW)
    if transform_flags != CHUNK_TRANSFORM_NONE:
        allow_reuse = False
    key_id = (chunk_kind, chunk_hash, len(chunk_payload), chunk_crc)
    if allow_reuse and key_id in chunk_map:
        return chunk_map[key_id], True

    nonce = secrets.token_bytes(NONCE_SIZE)
    master = pre_master_key if is_k9 else full_master_key
    plain_for_pack = chunk_payload
    if transform_flags & CHUNK_TRANSFORM_ARX:
        plain_for_pack = apply_arx_transform(plain_for_pack, master, file_id, nonce, len(chunk_payload))
    if transform_flags & CHUNK_TRANSFORM_FEISTEL:
        plain_for_pack = apply_feistel_transform(plain_for_pack, master, file_id, nonce, len(chunk_payload))
    packed = compress_chunk(plain_for_pack, compression, whole_file_mode=whole_file_mode)
    mode = packed[0]

    enc = segmented_encrypt(packed, master, nonce, chunk_kind,
                            original_size=len(chunk_payload), is_k9=is_k9)

    data_offset = fp.tell()
    fp.write(enc)

    rec = {
        "chunk_hash": chunk_hash,
        "original_size": len(chunk_payload),
        "stored_size": len(enc),
        "data_offset": data_offset,
        "data_nonce": nonce,
        "chunk_crc32c": chunk_crc,
        "chunk_kind": chunk_kind,
        "mode": mode,
        "transform_flags": transform_flags,
        "file_id": file_id,
    }
    idx = len(chunk_records)
    chunk_records.append(rec)
    if allow_reuse:
        chunk_map[key_id] = idx
    return idx, False


def pack(input_dir: Path, pak_path: Path, manifest_path: Path,
         dedup_mode: bool = True, show_progress: bool = True,
         compression: str = "auto", workers: int = 0,
         cdc_avg_size: int = CDC_DEFAULT_AVG_SIZE,
         whole_file_threshold: int = WHOLE_FILE_THRESHOLD,
         sign_key_path: str | None = None):
    _ensure_aesgcm()
    files = collect_files(input_dir)
    if not files:
        raise ValueError("输入目录没有可封包文件")

    cdc_params = make_cdc_params(cdc_avg_size)
    rel_paths = [rel for _, rel in files]
    total_files = len(files)
    logical_size = sum(fp.stat().st_size for fp, _ in files)
    write_manifest(manifest_path, rel_paths)
    emit_log_line(f"收集到 {total_files} 个文件, 逻辑大小 {format_size(logical_size)}")
    emit_log_line(
        f"封包策略: 小文件整文件阈值={format_size(whole_file_threshold)} | "
        f"CDC(min/avg/max)={format_size(cdc_params['min_size'])}/"
        f"{format_size(cdc_params['avg_size'])}/{format_size(cdc_params['max_size'])}"
    )
    emit_log_line("v6 安全特性: AES-256-GCM + Ed25519签名 + keyed索引混淆 + file_id条目掩码 + ARX/Feistel二次保护")

    k2 = secrets.token_bytes(FRAGMENT_SIZE)
    k6 = secrets.token_bytes(FRAGMENT_SIZE)
    k8 = secrets.token_bytes(FRAGMENT_SIZE)
    k10 = secrets.token_bytes(FRAGMENT_SIZE)
    seed = struct.unpack("<H", secrets.token_bytes(2))[0]
    pre_master_key = derive_pre_master_key(k2, k8, True)
    k9_plain = secrets.token_bytes(FRAGMENT_SIZE)
    full_master_key = derive_full_master_key(k2, k6, k8, k9_plain, k10, True)

    chunk_map = {}
    file_dedup_map = {}
    chunk_records = []
    chunk_refs = []
    entries = []
    dedup_reused = 0
    exact_file_reused = 0
    hash_seen = {}
    layout_counts = {"whole": 0, "cdc": 0, "file_dedup": 0}
    mode_stats = {
        MODE_RAW: {"chunks": 0, "plain": 0, "stored": 0},
        MODE_ZLIB: {"chunks": 0, "plain": 0, "stored": 0},
        MODE_ZSTD: {"chunks": 0, "plain": 0, "stored": 0},
        MODE_LZMA: {"chunks": 0, "plain": 0, "stored": 0},
    }
    start_time = time.perf_counter()

    # Use streaming pack tasks for large files
    pack_tasks = [(str(src), rel, cdc_params, whole_file_threshold) for src, rel in files]
    worker_count = resolve_worker_count(workers, total_files)
    if any(fp.stat().st_size >= LARGE_FILE_THRESHOLD for fp, _ in files):
        worker_count = 1

    pak_path.parent.mkdir(parents=True, exist_ok=True)
    with pak_path.open("w+b") as fp:
        fp.write(b"\x00" * HEADER_SIZE)

        k9_ref_start = len(chunk_refs)
        k9_chunk_index, _ = _add_chunk(fp, chunk_map, chunk_records, k9_plain,
                                       CHUNK_KIND_K9, pre_master_key, full_master_key,
                                       "zlib", "", b"\x00" * FILE_ID_SIZE,
                                       allow_reuse=False, is_k9=True)
        chunk_refs.append(k9_chunk_index)
        entries.append({
            "hash_bytes": lite_hash_bytes(f"__lpk_internal__/k9_{secrets.token_hex(8)}"),
            "file_id": b"\x00" * FILE_ID_SIZE,
            "flags": ENTRY_KEY_PAYLOAD,
            "original_size": len(k9_plain),
            "file_crc32c": crc32c(k9_plain),
            "chunk_ref_start": k9_ref_start,
            "chunk_count": 1,
        })

        prepared_iter = None
        if worker_count > 1:
            executor = concurrent.futures.ThreadPoolExecutor(max_workers=worker_count)
            prepared_iter = executor.map(_prepare_pack_item_streaming, pack_tasks)
        else:
            executor = None
            prepared_iter = map(_prepare_pack_item_streaming, pack_tasks)

        try:
            for idx, prep in enumerate(prepared_iter, 1):
                rel = prep["rel"]
                hh = prep["hash_bytes"].hex().upper()
                if hh in hash_seen and hash_seen[hh] != rel:
                    raise ValueError(f"hash碰撞: {hash_seen[hh]} vs {rel}")
                hash_seen[hh] = rel

                file_id = derive_file_id(prep["hash_bytes"], prep["content_id"], prep["raw_size"])
                file_transform_flags = select_transform_flags(rel, prep["raw_size"], MODE_RAW)
                ref_start = len(chunk_refs)
                file_chunk_count = 0
                file_reused = 0
                layout = "whole" if is_whole_file_mode(prep["raw_size"], whole_file_threshold) else "cdc"
                mode_summary = ""
                logical_stored = 0
                detail_progress = show_progress and prep["raw_size"] >= LARGE_FILE_THRESHOLD
                processed_bytes = 0

                if dedup_mode and file_transform_flags == CHUNK_TRANSFORM_NONE and prep["content_id"] in file_dedup_map:
                    prev = file_dedup_map[prep["content_id"]]
                    refs_slice = chunk_refs[prev["chunk_ref_start"]:prev["chunk_ref_start"] + prev["chunk_count"]]
                    chunk_refs.extend(refs_slice)
                    file_chunk_count = prev["chunk_count"]
                    file_reused = prev["chunk_count"]
                    dedup_reused += file_reused
                    exact_file_reused += 1
                    layout = "file_dedup"
                    mode_summary = prev["mode_summary"]
                    logical_stored = prev["logical_stored"]
                    processed_bytes = prep["raw_size"]
                    if detail_progress:
                        print_progress(f"单文件处理中 {rel}", processed_bytes, prep["raw_size"])
                    layout_counts[layout] += 1
                else:
                    if file_transform_flags != CHUNK_TRANSFORM_NONE:
                        layout = "whole"
                    # Get chunks: either pre-split (streaming) or split now
                    if prep["chunks"] is not None and file_transform_flags == CHUNK_TRANSFORM_NONE:
                        payload_chunks = prep["chunks"]
                    elif prep["raw"] is not None:
                        payload_chunks = [prep["raw"]] if layout == "whole" else split_chunks_cdc(prep["raw"], cdc_params)
                    else:
                        with open(next(src for src, path_rel in files if path_rel == rel), "rb") as src_fp:
                            payload_chunks = [src_fp.read()] if layout == "whole" else prep["chunks"]

                    chunk_indices = []
                    for chunk_payload in payload_chunks:
                        ci, reused = _add_chunk(fp, chunk_map, chunk_records, chunk_payload,
                                                CHUNK_KIND_FILE, pre_master_key, full_master_key,
                                                compression, rel, file_id, allow_reuse=dedup_mode,
                                                whole_file_mode=(layout == "whole"))
                        chunk_refs.append(ci)
                        chunk_indices.append(ci)
                        file_chunk_count += 1
                        processed_bytes += len(chunk_payload)
                        if detail_progress:
                            print_progress(f"单文件处理中 {rel}", processed_bytes, prep["raw_size"])
                        if reused:
                            file_reused += 1
                        else:
                            mode = chunk_records[ci]["mode"]
                            mode_stats[mode]["chunks"] += 1
                            mode_stats[mode]["plain"] += chunk_records[ci]["original_size"]
                            mode_stats[mode]["stored"] += chunk_records[ci]["stored_size"]
                    dedup_reused += file_reused
                    mode_summary, logical_stored = summarize_modes(chunk_records, chunk_indices)
                    if dedup_mode and file_transform_flags == CHUNK_TRANSFORM_NONE:
                        file_dedup_map[prep["content_id"]] = {
                            "chunk_ref_start": ref_start,
                            "chunk_count": file_chunk_count,
                            "mode_summary": mode_summary,
                            "logical_stored": logical_stored,
                        }
                    layout_counts[layout] += 1

                entries.append({
                    "hash_bytes": prep["hash_bytes"],
                    "file_id": file_id,
                    "flags": ENTRY_FILE,
                    "original_size": prep["raw_size"],
                    "file_crc32c": prep["file_crc32c"],
                    "chunk_ref_start": ref_start,
                    "chunk_count": file_chunk_count,
                })

                ratio = (logical_stored / prep["raw_size"]) if prep["raw_size"] > 0 else 0.0
                emit_log_line(
                    f"[{idx}/{total_files}] 写入 {rel} | layout={layout} | mode={mode_summary} | chunk={file_chunk_count} | reused={file_reused} | {logical_stored}/{prep['raw_size']} ({ratio:.2%})"
                )
                if show_progress:
                    print_progress("封包处理中", idx, total_files)
        finally:
            if executor is not None:
                executor.shutdown(wait=True)

        data_end = fp.tell()
        index_plain = _build_index(chunk_records, chunk_refs, entries, k6, k10,
                                    cdc_params, whole_file_threshold, seed=seed,
                                    v6_features=V6_FEATURES)
        obfuscated_plain = obfuscate_index(index_plain, seed, pre_master_key)

        idx_nonce = secrets.token_bytes(NONCE_SIZE)
        if zstd is not None:
            idx_payload = zstd.ZstdCompressor(level=19).compress(obfuscated_plain)
            flags = (FLAG_HAS_TRAILER | FLAG_INDEX_COMPRESSED | FLAG_CHUNK_INDEX |
                     FLAG_FULL_VERIFY | FLAG_AES_GCM | FLAG_ED25519_SIGNED | FLAG_INDEX_OBFUSCATED | FLAG_WB_STRONG)
        else:
            idx_payload = obfuscated_plain
            flags = (FLAG_HAS_TRAILER | FLAG_CHUNK_INDEX | FLAG_FULL_VERIFY |
                     FLAG_AES_GCM | FLAG_ED25519_SIGNED | FLAG_INDEX_OBFUSCATED | FLAG_WB_STRONG)
        idx_key = derive_index_key(pre_master_key, len(index_plain))
        idx_enc = aes_gcm_encrypt(idx_payload, idx_key, idx_nonce)
        idx_offset = fp.tell()
        fp.write(idx_enc)
        end_of_index = fp.tell()

        fp.seek(0)
        fp.write(_build_header(total_files, idx_offset, len(idx_enc), len(index_plain),
                               idx_nonce, flags, k2, k8, seed, V6_FEATURES))
        fp.seek(end_of_index)
        key_material_signature = compute_key_material_signature(k2, k6, k8, k9_plain, k10,
                                                                V6_FEATURES, pre_master_key,
                                                                full_master_key)
        fp.write(_build_trailer(fp, index_plain, data_end, 50, key_material_signature,
                                sign=True, sign_key_path=sign_key_path))

    emit_log_line(
        f"封包汇总: 文件数={total_files} | 原始={format_size(logical_size)} | 封包={format_size(pak_path.stat().st_size)} | 总压缩率={pak_path.stat().st_size / logical_size:.2%} | 唯一chunk={len(chunk_records)} | chunk引用={len(chunk_refs)-1} | 去重复用块={dedup_reused} | 整文件复用={exact_file_reused} | whole={layout_counts['whole']} | cdc={layout_counts['cdc']} | file-dedup={layout_counts['file_dedup']} | 耗时={format_duration(time.perf_counter() - start_time)}"
    )
    emit_log_line(
        "压缩模式汇总: " + " | ".join(
            f"{mode_name(mode)} chunks={stat['chunks']} plain={format_size(stat['plain'])} stored={format_size(stat['stored'])}"
            for mode, stat in mode_stats.items() if stat['chunks']
        )
    )


# ============================================================================
# Unpack / Read
# ============================================================================

def unpack(pak_path: Path, output_dir: Path,
           manifest_path=None, show_progress: bool = True,
           workers: int = 0, parallel: str = "thread",
           verify_trailer: bool = True, sign_key_path: str | None = None):
    with pak_path.open("rb") as fp:
        header = read_header(fp)
    pre_master_key = derive_pre_master_key(header["k2"], header["k8"], True)
    emit_log_line(f"Header: v={header['version']} file_count={header['file_count']}, flags=0x{header['flags']:04X}, seed=0x{header.get('seed',0):04X}")

    with pak_path.open("rb") as fp:
        emit_log_line("正在解密索引...")
        index_plain = decrypt_index(fp, header, pre_master_key)
        strict = bool(header["flags"] & FLAG_FULL_VERIFY)
        meta = parse_index(index_plain, strict=strict, seed=header.get("seed", 0),
                           v6_features=header.get("v6_features", V6_FEATURES))
        if meta["k9_entry"] is None:
            raise ValueError("未找到 key_payload 条目 (K9)")
        k9_bytes = _decode_entry_bytes(str(pak_path), meta["k9_entry"], meta["chunk_records"],
                                       meta["chunk_refs"], pre_master_key, b"", None)
        full_master_key = derive_full_master_key(header["k2"], meta["k6"], header["k8"],
                                                  k9_bytes[:FRAGMENT_SIZE], meta["k10"],
                                                  True)
        key_material_signature = compute_key_material_signature(header["k2"], meta["k6"], header["k8"],
                                                                k9_bytes[:FRAGMENT_SIZE], meta["k10"],
                                                                header.get("v6_features", V6_FEATURES),
                                                                pre_master_key, full_master_key)
        if header["has_trailer"] and verify_trailer:
            trailer_ok = read_trailer(fp, index_plain, header, sign_key_path,
                                      key_material_signature)
            if strict and not trailer_ok:
                raise ValueError("Trailer 校验失败")
            sig_status = "Ed25519已验证" if header.get("ed25519_signed") else "无签名"
            emit_log_line(f"Trailer: {'校验通过' if trailer_ok else '校验失败'} ({sig_status})")

    manifest_map = {}
    if manifest_path is not None:
        for hh, _, rel in load_manifest(manifest_path):
            manifest_map[hh] = rel
        emit_log_line(f"清单: {len(manifest_map)} 条映射")

    file_entries = [e for e in meta["entries"] if e["flags"] == ENTRY_FILE]
    for e in file_entries:
        hh = e["hash_bytes"].hex().upper()
        e["rel"] = manifest_map.get(hh, f"{hh}.bin")

    total_files = len(file_entries)
    logical_size = sum(e["original_size"] for e in file_entries)
    emit_log_line(f"待解包: {total_files} 个文件, 逻辑大小 {format_size(logical_size)}")
    output_dir.mkdir(parents=True, exist_ok=True)

    any_large = any(e["original_size"] >= LARGE_FILE_THRESHOLD for e in file_entries)
    shared_cache = None if parallel == "process" else ChunkPlaintextCache()
    decode_tasks = [
        (str(pak_path), e, meta["chunk_records"], meta["chunk_refs"],
         pre_master_key, full_master_key, shared_cache)
        for e in file_entries
    ]
    start_time = time.perf_counter()

    if any_large:
        done = 0
        for e in file_entries:
            out_path = safe_output_path(output_dir, e["rel"])
            out_path.parent.mkdir(parents=True, exist_ok=True)
            plain = _decode_entry_bytes(str(pak_path), e, meta["chunk_records"], meta["chunk_refs"],
                                        pre_master_key, full_master_key, shared_cache)
            out_path.write_bytes(plain)
            done += 1
            if show_progress:
                print_progress("解包处理中", done, total_files)
    else:
        worker_count = resolve_worker_count(workers, total_files)
        if worker_count > 1:
            ex_cls = concurrent.futures.ProcessPoolExecutor if parallel == "process" else concurrent.futures.ThreadPoolExecutor
            with ex_cls(max_workers=worker_count) as ex:
                diter = ex.map(_decode_entry_task, decode_tasks)
                done = 0
                for e, plain in zip(file_entries, diter):
                    out_path = safe_output_path(output_dir, e["rel"])
                    out_path.parent.mkdir(parents=True, exist_ok=True)
                    out_path.write_bytes(plain)
                    done += 1
                    if show_progress:
                        print_progress("解包处理中", done, total_files)
        else:
            done = 0
            for e in file_entries:
                plain = _decode_entry_bytes(str(pak_path), e, meta["chunk_records"], meta["chunk_refs"],
                                            pre_master_key, full_master_key, shared_cache)
                out_path = safe_output_path(output_dir, e["rel"])
                out_path.parent.mkdir(parents=True, exist_ok=True)
                out_path.write_bytes(plain)
                done += 1
                if show_progress:
                    print_progress("解包处理中", done, total_files)

    emit_log_line(f"解包汇总: 文件数={total_files} | 输出={format_size(logical_size)} | 封包={format_size(pak_path.stat().st_size)} | 耗时={format_duration(time.perf_counter() - start_time)}")


def extract_by_name(pak_path: Path, rel_name: str) -> bytes:
    with pak_path.open("rb") as fp:
        header = read_header(fp)
    pre_master_key = derive_pre_master_key(header["k2"], header["k8"], True)
    target_hash = lite_hash_bytes(rel_name)
    cache = ChunkPlaintextCache()
    with pak_path.open("rb") as fp:
        header = read_header(fp)
        index_plain = decrypt_index(fp, header, pre_master_key)
        meta = parse_index(index_plain, seed=header.get("seed", 0),
                           v6_features=header.get("v6_features", V6_FEATURES))
        if meta["k9_entry"] is None:
            raise ValueError("未找到 key_payload 条目 (K9)")
        k9_bytes = _decode_entry_bytes(str(pak_path), meta["k9_entry"], meta["chunk_records"],
                                       meta["chunk_refs"], pre_master_key, b"", cache)
        full_master_key = derive_full_master_key(header["k2"], meta["k6"], header["k8"],
                                                  k9_bytes[:FRAGMENT_SIZE], meta["k10"],
                                                  True)
    emap = {e["hash_bytes"]: e for e in meta["entries"] if e["flags"] == ENTRY_FILE}
    if target_hash not in emap:
        raise ValueError(f"未找到: {rel_name} (hash={target_hash.hex().upper()})")
    return _decode_entry_bytes(str(pak_path), emap[target_hash], meta["chunk_records"],
                               meta["chunk_refs"], pre_master_key, full_master_key, cache)


# ============================================================================
# Info / verify / drag / CLI
# ============================================================================

def info(pak_path: Path, sign_key_path: str | None = None):
    trailer_state = None
    chunk_count = None
    ref_count = None
    with pak_path.open("rb") as fp:
        try:
            h = read_header(fp)
            pre_master_key = derive_pre_master_key(h["k2"], h["k8"], True)
            index_plain = decrypt_index(fp, h, pre_master_key)
            meta = parse_index(index_plain, seed=h.get("seed", 0),
                               v6_features=h.get("v6_features", V6_FEATURES))
            chunk_count = len(meta["chunk_records"])
            ref_count = len(meta["chunk_refs"])
            key_material_signature = None
            if meta.get("k9_entry") is not None:
                k9_bytes = _decode_entry_bytes(str(pak_path), meta["k9_entry"], meta["chunk_records"],
                                               meta["chunk_refs"], pre_master_key, b"", None)
                full_master_key = derive_full_master_key(h["k2"], meta["k6"], h["k8"],
                                                         k9_bytes[:FRAGMENT_SIZE], meta["k10"], True)
                key_material_signature = compute_key_material_signature(h["k2"], meta["k6"], h["k8"],
                                                                        k9_bytes[:FRAGMENT_SIZE], meta["k10"],
                                                                        h.get("v6_features", V6_FEATURES),
                                                                        pre_master_key, full_master_key)
        except Exception as e:
            print(f"错误: {e}")
            return
        if h["has_trailer"]:
            try:
                trailer_state = read_trailer(fp, index_plain, h, sign_key_path,
                                             key_material_signature)
            except Exception:
                trailer_state = None

    print(f"文件:     {pak_path}")
    print(f"大小:     {format_size(pak_path.stat().st_size)}")
    print(f"魔术头:   {MAGIC.decode()}")
    print(f"版本:     {h['version']}")
    flags_desc = (f"trailer={h['has_trailer']}, idx_compressed={h['index_compressed']}, "
                  f"chunk_index={h['chunk_index']}, aes_gcm={h.get('aes_gcm',False)}, "
                  f"ed25519={h.get('ed25519_signed',False)}, idx_obfuscated={h.get('index_obfuscated',False)}, "
                  f"strong_wb={h.get('strong_wb',False)}")
    print(f"Flags:    0x{h['flags']:04X} ({flags_desc})")
    print(f"Seed:     0x{h.get('seed',0):04X}")
    print(f"文件数:   {h['file_count']}")
    print(f"chunk数:  {chunk_count}")
    print(f"chunk引用:{ref_count}")
    print(f"整文件阈值: {format_size(meta['whole_file_threshold'])}")
    print(f"CDC(min/avg/max): {format_size(meta['cdc_min_size'])}/{format_size(meta['cdc_avg_size'])}/{format_size(meta['cdc_max_size'])}")
    print(f"索引偏移: {h['index_offset']} ({format_size(h['index_offset'])})")
    print(f"索引加密: {format_size(h['index_encrypted_sz'])}")
    print(f"索引明文: {format_size(h['index_plain_sz'])}")
    if h["has_trailer"]:
        if trailer_state is None:
            print("Trailer:  读取失败")
        else:
            print(f"Trailer:  {'校验通过' if trailer_state else '校验失败'}")


def verify(pak_path: Path, sign_key_path: str | None = None):
    """Full verification of a .lpk file."""
    print(f"正在完整校验: {pak_path}")
    results = full_verify_pack(pak_path, sign_key_path)
    for k, v in results.items():
        if k in ("chunk_errors", "file_errors"):
            print(f"  {k}:")
            for err in v:
                print(f"    {err}")
        else:
            print(f"  {k}: {v}")
    if results.get("overall"):
        print("校验结果: 全部通过")
        return True
    print("校验结果: 存在问题")
    return False


def auto_drag_mode(paths: list):
    if len(paths) == 1:
        p = Path(paths[0])
        if p.is_dir():
            pak_path = p.parent / f"{p.name}.lpk"
            manifest_path = p.parent / f"{p.name}_manifest.txt"
            pack(p, pak_path, manifest_path, dedup_mode=True, show_progress=True, compression="auto")
            print(f"\n已封包: {pak_path}")
            print(f"已生成清单: {manifest_path}")
            wait_for_any_key()
            return
        if p.suffix.lower() == ".lpk" and p.is_file():
            output_dir = p.parent / f"{p.stem}_unpacked"
            clear_dir(output_dir)
            output_dir.mkdir(parents=True, exist_ok=True)
            unpack(p, output_dir, manifest_path=None, show_progress=True)
            print(f"\n已按hash解包到: {output_dir}")
            wait_for_any_key()
            return
    if len(paths) == 2:
        p1, p2 = Path(paths[0]), Path(paths[1])
        items = {p1.suffix.lower(): p1, p2.suffix.lower(): p2}
        if ".lpk" in items and ".txt" in items:
            pak_path = items[".lpk"]
            manifest_path = items[".txt"]
            output_dir = pak_path.parent / f"{pak_path.stem}_unpacked"
            clear_dir(output_dir)
            output_dir.mkdir(parents=True, exist_ok=True)
            unpack(pak_path, output_dir, manifest_path=manifest_path, show_progress=True)
            print(f"\n已按清单解包到: {output_dir}")
            wait_for_any_key()
            return
    print("拖拽模式:")
    print("  1个文件夹           → 封包为 .lpk + 生成清单 .txt")
    print("  1个 .lpk 文件       → 按 hash 解包 (无原始路径)")
    print("  1个 .lpk + 1个 .txt → 按清单解包 (恢复原始路径)")
    wait_for_any_key()


def main():
    if len(sys.argv) > 1 and sys.argv[1] not in {"pack", "unpack", "info", "read", "verify", "-h", "--help"}:
        auto_drag_mode(sys.argv[1:])
        return

    parser = argparse.ArgumentParser(description="LitePAK v6 工具")
    sub = parser.add_subparsers(dest="cmd")

    p_pack = sub.add_parser("pack", help="封包")
    p_pack.add_argument("--input", required=True)
    p_pack.add_argument("--pak", required=True)
    p_pack.add_argument("--manifest", required=True)
    p_pack.add_argument("--dedup", action="store_true", default=True)
    p_pack.add_argument("--no-dedup", action="store_false", dest="dedup")
    p_pack.add_argument("--compression", default="auto", choices=["zlib", "zstd", "lzma", "auto", "raw"])
    p_pack.add_argument("--cdc-avg-kb", type=int, default=128, choices=[128, 256], help="CDC 平均块大小")
    p_pack.add_argument("--whole-file-threshold-kb", type=int, default=1024, help="小于等于该阈值的文件走整文件模式")
    p_pack.add_argument("--workers", type=int, default=0)
    p_pack.add_argument("--parallel", default="thread", choices=["thread", "process"])
    p_pack.add_argument("--sign-key", default=None, help="Ed25519 签名私钥文件 (32字节)")

    p_unpack = sub.add_parser("unpack", help="解包")
    p_unpack.add_argument("--pak", required=True)
    p_unpack.add_argument("--manifest", required=False, help="清单文件 (可选, 恢复原始文件名)")
    p_unpack.add_argument("--output", required=True)
    p_unpack.add_argument("--workers", type=int, default=0)
    p_unpack.add_argument("--parallel", default="thread", choices=["thread", "process"])
    p_unpack.add_argument("--no-trailer-check", action="store_true")
    p_unpack.add_argument("--sign-key", default=None, help="Ed25519 验签私钥文件 (32字节)")

    p_info = sub.add_parser("info", help="查看封包信息")
    p_info.add_argument("--pak", required=True)
    p_info.add_argument("--sign-key", default=None, help="Ed25519 验签私钥文件 (32字节)")

    p_read = sub.add_parser("read", help="提取单个文件")
    p_read.add_argument("--pak", required=True)
    p_read.add_argument("--name", required=True, help="文件相对路径 (如 script/start.ks)")
    p_read.add_argument("--output", required=True)

    p_verify = sub.add_parser("verify", help="完整校验封包")
    p_verify.add_argument("--pak", required=True)
    p_verify.add_argument("--sign-key", default=None, help="Ed25519 验签私钥文件 (32字节)")

    args = parser.parse_args()

    if args.cmd == "pack":
        pack(Path(args.input), Path(args.pak), Path(args.manifest),
             dedup_mode=args.dedup,
             show_progress=True,
             compression=args.compression,
             workers=args.workers,
             cdc_avg_size=args.cdc_avg_kb * 1024,
             whole_file_threshold=args.whole_file_threshold_kb * 1024,
             sign_key_path=args.sign_key)
        wait_for_any_key()
    elif args.cmd == "unpack":
        out = Path(args.output)
        clear_dir(out)
        out.mkdir(parents=True, exist_ok=True)
        manifest = Path(args.manifest) if args.manifest else None
        unpack(Path(args.pak), out,
               manifest_path=manifest,
               workers=args.workers,
               parallel=args.parallel,
               verify_trailer=not args.no_trailer_check,
               sign_key_path=args.sign_key)
        wait_for_any_key()
    elif args.cmd == "info":
        info(Path(args.pak), args.sign_key)
    elif args.cmd == "read":
        data = extract_by_name(Path(args.pak), args.name)
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        print(f"已导出: {out} ({format_size(len(data))})")
    elif args.cmd == "verify":
        if not verify(Path(args.pak), args.sign_key):
            sys.exit(1)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
