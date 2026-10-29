#!/usr/bin/env python3
"""Offline decoder for NFC sniffer SD dumps.
"""
from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cu_mfkey import AuthSession, RecoverResult, recover_key

BANNER = r"""
 ███╗   ██╗███████╗ ██████╗    ███████╗███╗   ██╗██╗███████╗███████╗███████╗██████╗
 ████╗  ██║██╔════╝██╔════╝    ██╔════╝████╗  ██║██║██╔════╝██╔════╝██╔════╝██╔══██╗
 ██╔██╗ ██║█████╗  ██║         ███████╗██╔██╗ ██║██║█████╗  █████╗  █████╗  ██████╔╝
 ██║╚██╗██║██╔══╝  ██║         ╚════██║██║╚██╗██║██║██╔══╝  ██╔══╝  ██╔══╝  ██╔══██╗
 ██║ ╚████║██║     ╚██████╗    ███████║██║ ╚████║██║██║     ██║     ███████╗██║  ██║
 ╚═╝  ╚═══╝╚═╝      ╚═════╝    ╚══════╝╚═╝  ╚═══╝╚═╝╚═╝     ╚═╝     ╚══════╝╚═╝  ╚═╝
"""


# --- ISO14443-A CRC -----------------------------------------------------------


def crc_a(data: bytes) -> int:
    crc = 0x6363
    for b in data:
        ch = b ^ (crc & 0xFF)
        ch = (ch ^ (ch << 4)) & 0xFF
        crc = ((crc >> 8) ^ (ch << 8) ^ (ch << 3) ^ (ch >> 4)) & 0xFFFF
    return crc


def crc_ok(frame: bytes) -> bool:
    if len(frame) < 3:
        return False
    c = crc_a(frame[:-2])
    return (c & 0xFF) == frame[-2] and (c >> 8) == frame[-1]


# --- Crypto1 ------------------------------------------------------------------


def _parity(x: int) -> int:
    x ^= x >> 16
    x ^= x >> 8
    x ^= x >> 4
    x ^= x >> 2
    x ^= x >> 1
    return x & 1


def _filter(x: int) -> int:
    f = 0xF22C0 >> (x & 0xF) & 16
    f |= 0x6C9C0 >> ((x >> 4) & 0xF) & 8
    f |= 0x3C8B0 >> ((x >> 8) & 0xF) & 4
    f |= 0x1E458 >> ((x >> 12) & 0xF) & 2
    f |= 0x0D938 >> ((x >> 16) & 0xF) & 1
    return (0xEC57E80A >> f) & 1


LF_POLY_ODD = 0x29CE5C
LF_POLY_EVEN = 0x870804


class Crypto1:
    def __init__(self, key: int) -> None:
        self.odd = 0
        self.even = 0
        for i in range(47, 0, -2):
            self.odd = (self.odd << 1) | ((key >> ((i - 1) ^ 7)) & 1)
            self.even = (self.even << 1) | ((key >> (i ^ 7)) & 1)

    def bit(self, inp: int, is_enc: bool) -> int:
        out = _filter(self.odd)
        feed = out & (1 if is_enc else 0)
        feed ^= 1 if inp else 0
        feed ^= LF_POLY_ODD & self.odd
        feed ^= LF_POLY_EVEN & self.even
        self.even = ((self.even << 1) | _parity(feed)) & 0xFFFFFF
        self.odd, self.even = self.even, self.odd
        return out

    def word(self, inp: int, is_enc: bool) -> int:
        ret = 0
        for i in range(32):
            bit_in = (inp >> (i ^ 24)) & 1
            ret |= self.bit(bit_in, is_enc) << (i ^ 24)
        return ret


def prng_successor(x: int, n: int) -> int:
    x = struct.unpack(">I", struct.pack("<I", x))[0]
    for _ in range(n):
        x = (
            (x >> 1) | ((((x >> 16) ^ (x >> 18) ^ (x >> 19) ^ (x >> 21)) & 1) << 31)
        ) & 0xFFFFFFFF
    return struct.unpack(">I", struct.pack("<I", x))[0]


def crypto1_byte(s: Crypto1, inp: int = 0, is_enc: bool = False) -> int:
    ret = 0
    for i in range(8):
        ret |= s.bit((inp >> i) & 1, is_enc) << i
    return ret & 0xFF


def crypto1_decrypt(s: Crypto1, data: bytes) -> bytes:
    """Decrypt post-auth MIFARE bytes (parity reuses next filter bit — no extra clock)."""
    return bytes(b ^ crypto1_byte(s, 0, False) for b in data)


def crypto1_after_auth(
    key: int, uid: int, nt: int, nr_enc: int, ar_enc: int, at_enc: int
) -> tuple[Crypto1, dict]:
    """Prime cipher through auth; return state ready for session traffic."""
    s = Crypto1(key)
    s.word(uid ^ nt, False)
    ks1 = s.word(nr_enc, True)
    nr = nr_enc ^ ks1
    ks2 = s.word(0, False)
    ar = ar_enc ^ ks2
    ks3 = s.word(0, False)
    at = at_enc ^ ks3
    r = dict(
        nr=nr,
        ar=ar,
        at=at,
        ar_exp=prng_successor(nt, 64),
        at_exp=prng_successor(nt, 96),
    )
    r["ar_ok"] = ar == r["ar_exp"]
    r["at_ok"] = at == r["at_exp"]
    return s, r


def crypto1_verify(key, uid, nt, nr_enc, ar_enc, at_enc=None):
    if at_enc is not None:
        _s, r = crypto1_after_auth(key, uid, nt, nr_enc, ar_enc, at_enc)
        return r
    s = Crypto1(key)
    s.word(uid ^ nt, False)
    ks1 = s.word(nr_enc, True)
    nr = nr_enc ^ ks1
    ks2 = s.word(0, False)
    ar = ar_enc ^ ks2
    r = dict(nr=nr, ar=ar, ar_exp=prng_successor(nt, 64))
    r["ar_ok"] = ar == r["ar_exp"]
    return r


def mf_plain_label(direction: str, plain: bytes) -> tuple[str, bool | None, bool]:
    """Classify decrypted Classic frame → (label, crc_ok|None, stop_stream)."""
    if direction == "R":
        if len(plain) == 4 and crc_ok(plain):
            cmd, blk = plain[0], plain[1]
            if cmd == 0x30:
                return f"READ blk {blk}", True, False
            if cmd == 0xA0:
                return f"WRITE blk {blk}", True, False
            if cmd in (0x60, 0x61):
                kt = "A" if cmd == 0x60 else "B"
                return f"NESTED AUTH-{kt} blk {blk}", True, True
            names = {0xC0: "DEC", 0xC1: "INC", 0xC2: "RESTORE", 0xB0: "TRANSFER"}
            if cmd in names:
                return f"{names[cmd]} blk {blk}", True, False
            return f"CMD {cmd:02X} blk {blk}", True, False
        if len(plain) == 16 and crc_ok(plain):  # rare: write data
            return "WRITE data", True, False
        return "enc cmd", False if len(plain) == 4 else None, False
    # card
    if len(plain) == 18 and crc_ok(plain):
        return "READ resp", True, False
    if len(plain) == 1:
        nibble = plain[0] & 0x0F
        if nibble == 0x0A:
            return "ACK", True, False
        return f"NACK 0x{nibble:X}", True, False
    return "enc data", False if len(plain) in (4, 18) else None, False


def parse_key(s: str) -> int:
    hx = re.sub(r"[^0-9A-Fa-f]", "", s)
    if len(hx) != 12:
        raise argparse.ArgumentTypeError(
            f"key must be 12 hex digits (6 bytes), got {len(hx)}: {s!r}"
        )
    return int(hx, 16)


def selftest() -> int:
    key, uid, nt = 0x8829DA9DAF76, 0x5C7517BE, 0x2213178D
    nr_enc, ar_enc, at_enc = 0x2F5A9279, 0x291B3E53, 0x8E7805B1
    ok = prng_successor(nt, 64) == 0x915DABBB and prng_successor(nt, 96) == 0xE9AF917C
    print(
        f"prng_successor: {'OK' if ok else 'FAIL'} "
        f"(suc64={prng_successor(nt, 64):08X} exp 915DABBB, "
        f"suc96={prng_successor(nt, 96):08X} exp E9AF917C)"
    )
    r = crypto1_verify(key, uid, nt, nr_enc, ar_enc, at_enc)
    print(
        f"crypto1: nr={r['nr']:08X} (exp 12345678)  ar={r['ar']:08X} ar_ok={r['ar_ok']}  "
        f"at={r['at']:08X} at_ok={r['at_ok']}"
    )
    passed = ok and r["nr"] == 0x12345678 and r["ar_ok"] and r["at_ok"]
    print("SELFTEST", "PASS" if passed else "FAIL")
    return 0 if passed else 1


# --- Parse / annotate ---------------------------------------------------------


@dataclass
class Frame:
    direction: str  # R / C
    t0: int
    t1: int
    data: bytes
    parity_bad: list[bool]
    us0: float = 0.0
    us1: float = 0.0
    annotation: str = ""
    encrypted: bool = False
    crypto_note: str = ""
    crypto_ok: bool | None = None  # None = n/a
    plain: bytes | None = None  # decrypted Crypto1 payload when known


@dataclass
class Capture:
    couple: int = -1
    rf_field: int = -1
    overrun: int = 0
    valid: int = 0
    backlog: int = 0
    period_ns: int = 406
    raw_header: str = ""
    frames: list[Frame] = field(default_factory=list)

    @property
    def couple_name(self) -> str:
        return {0: "strong", 1: "fair", 2: "weak"}.get(self.couple, "?")


class AuthAnnotator:
    """Label AUTH frames, collect recovery sessions, optional Crypto1 verify/stream."""

    def __init__(self, key: int | None, key_source: str = "") -> None:
        self.key = key
        self.key_source = key_source  # "cli" | "mfkey64" | ...
        self.uid = None
        self.key_type = "A"
        self.block = 0
        self.awaiting_nt = False
        self.nt = None
        self.nr_enc = None
        self.ar_enc = None
        self._open_session: AuthSession | None = None
        self.sessions: list[AuthSession] = []
        self.ar_ok = 0
        self.ar_fail = 0
        self.at_ok = 0
        self.at_fail = 0
        self.n_incomplete = 0
        self.stream: Crypto1 | None = None
        self._pending_read_blk: int | None = None
        self.blocks: dict[int, bytes] = {}  # block → 16-byte plaintext
        self.other_key = False  # traffic after nested auth (unknown key)

    def annotate(self, fr: Frame) -> None:
        d, data = fr.direction, fr.data
        base = _base_label(d, data)

        # Plain reactivation ends any Crypto1 session.
        if d == "R" and base in ("WUPA", "REQA", "HLTA"):
            self.stream = None
            self.other_key = False
            self._pending_read_blk = None

        if d == "R" and base.startswith("SELECT cl1") and len(data) >= 6:
            self.uid = int.from_bytes(data[2:6], "big")
            self._reset_auth()
            fr.annotation = base
            return

        if d == "R" and base.startswith("AUTH-"):
            self.stream = None
            self.other_key = False
            self.awaiting_nt = True
            self.nt = None
            self.nr_enc = None
            self.ar_enc = None
            self._open_session = None
            self.key_type = "A" if data[0] == 0x60 else "B"
            self.block = data[1] if len(data) > 1 else 0
            fr.annotation = base  # AUTH-A / AUTH-B blk N
            return

        if d == "C" and self.awaiting_nt and len(data) == 4:
            # nt is plaintext — skip clearly broken parity frames
            if not any(fr.parity_bad):
                self.nt = int.from_bytes(data, "big")
                self.awaiting_nt = False
            fr.annotation = "AUTH: nt"
            return

        if d == "R" and base == "nr||ar" and len(data) == 8 and self.stream is None:
            self.nr_enc = int.from_bytes(data[:4], "big")
            self.ar_enc = int.from_bytes(data[4:8], "big")
            fr.annotation = "AUTH: nr ar (enc)"
            fr.encrypted = True
            if self.uid is not None and self.nt is not None:
                sess = AuthSession(
                    uid=self.uid,
                    key_type=self.key_type,
                    block=self.block,
                    nt=self.nt,
                    nr_enc=self.nr_enc,
                    ar_enc=self.ar_enc,
                    at_enc=None,
                )
                self.sessions.append(sess)
                self._open_session = sess
            if self.key is None:
                return
            if self.uid is None or self.nt is None:
                self.n_incomplete += 1
                fr.crypto_note = "incomplete"
                fr.crypto_ok = False
                return
            r = crypto1_verify(self.key, self.uid, self.nt, self.nr_enc, self.ar_enc)
            if r["ar_ok"]:
                self.ar_ok += 1
                fr.crypto_ok = True
            else:
                self.ar_fail += 1
                fr.crypto_ok = False
            fr.crypto_note = (
                f"ar {'OK' if r['ar_ok'] else 'FAIL'}  "
                f"nr={r['nr']:08X}  ar={r['ar']:08X}"
            )
            return

        if (
            d == "C"
            and self.stream is None
            and self.nr_enc is not None
            and self.ar_enc is not None
            and self.nt is not None
            and len(data) == 4
        ):
            fr.annotation = "AUTH: at (enc)"
            fr.encrypted = True
            at_enc = int.from_bytes(data, "big")
            if self._open_session is not None:
                self._open_session.at_enc = at_enc
            if self.key is not None and self.uid is not None:
                s, r = crypto1_after_auth(
                    self.key, self.uid, self.nt, self.nr_enc, self.ar_enc, at_enc
                )
                if r.get("at_ok"):
                    self.at_ok += 1
                    fr.crypto_ok = True
                    self.stream = s  # continue Crypto1 for session traffic
                    self.other_key = False
                else:
                    self.at_fail += 1
                    fr.crypto_ok = False
                fr.crypto_note = (
                    f"at {'OK' if r.get('at_ok') else 'FAIL'}  at={r['at']:08X}"
                )
            self.nr_enc = None
            self.ar_enc = None
            self._open_session = None
            return

        # Follow Crypto1 after a verified auth (READ/WRITE/… until nested AUTH).
        if self.stream is not None and self.key is not None and data:
            plain = crypto1_decrypt(self.stream, data)
            fr.plain = plain
            fr.encrypted = True
            label, ok, stop = mf_plain_label(d, plain)
            fr.annotation = label
            fr.crypto_ok = ok
            fr.crypto_note = f"plain={plain.hex().upper()}"
            if label.startswith("READ blk "):
                self._pending_read_blk = int(label.rsplit(" ", 1)[-1])
            elif label == "READ resp" and self._pending_read_blk is not None:
                self.blocks[self._pending_read_blk] = plain[:16]
                fr.crypto_note += (
                    f"  blk{self._pending_read_blk}={plain[:16].hex().upper()}"
                )
                self._pending_read_blk = None
            if stop:
                self.stream = None
                self.other_key = True
                self._pending_read_blk = None
                fr.crypto_note += "  (stream ends — other sector key)"
            elif ok is False and len(plain) in (4, 18):
                self.stream = None
                self._pending_read_blk = None
            return

        if self.other_key and data and base in ("", "nr||ar", "nt/at"):
            fr.encrypted = True
            fr.annotation = "enc (other key)"
            return

        fr.annotation = base

    def begin_capture(self) -> None:
        """Reset per-capture Crypto1 stream (key/blocks/sessions kept)."""
        self.stream = None
        self.other_key = False
        self._pending_read_blk = None
        self.awaiting_nt = False
        self.nt = None
        self.nr_enc = None
        self.ar_enc = None
        self._open_session = None

    def _reset_auth(self) -> None:
        self.awaiting_nt = False
        self.nt = None
        self.nr_enc = None
        self.ar_enc = None
        self._open_session = None
        self.stream = None
        self.other_key = False
        self._pending_read_blk = None

    def summary(self) -> str:
        if self.key is None:
            return ""
        src = f" ({self.key_source})" if self.key_source else ""
        if self.ar_ok or self.ar_fail or self.at_ok or self.at_fail:
            parts = (
                f"crypto1 key={self.key:012X}{src}  "
                f"ar ok={self.ar_ok} fail={self.ar_fail}  "
                f"at ok={self.at_ok} fail={self.at_fail}"
            )
        elif self.n_incomplete:
            parts = (
                f"crypto1 key={self.key:012X}{src}  incomplete auth={self.n_incomplete}"
            )
        else:
            parts = f"crypto1 key={self.key:012X}{src}  no Classic auth handshake found"
        if self.blocks:
            blks = " ".join(
                f"{b}:{self.blocks[b].hex().upper()}" for b in sorted(self.blocks)
            )
            parts += f"  |  blocks {blks}"
        return parts


def _base_label(direction: str, data: bytes) -> str:
    if not data:
        return ""
    b0 = data[0]
    if direction == "R":
        if len(data) == 1 and b0 == 0x26:
            return "REQA"
        if len(data) == 1 and b0 == 0x52:
            return "WUPA"
        if b0 in (0x93, 0x95, 0x97) and len(data) >= 2:
            cl = {0x93: 1, 0x95: 2, 0x97: 3}[b0]
            if data[1] == 0x20:
                return f"ANTICOLL cl{cl}"
            if data[1] == 0x70:
                return f"SELECT cl{cl}"
        if b0 in (0x60, 0x61) and len(data) >= 2:
            return f"AUTH-{'A' if b0 == 0x60 else 'B'} blk {data[1]}"
        if b0 == 0x50 and len(data) >= 2 and data[1] == 0x00:
            return "HLTA"
        if b0 == 0xE0:
            return "RATS"
        if len(data) == 8:
            return "nr||ar"
        return ""
    # card
    if len(data) == 2:
        return "ATQA"
    if len(data) == 3 and data[0] in (0x08, 0x18, 0x20, 0x28):
        return f"SAK 0x{data[0]:02X}"
    if len(data) == 1:
        return "ACK/NACK"
    if len(data) == 5:
        return "UID+BCC"
    if len(data) == 4:
        return "nt/at"
    return ""


def parse_dump(path: str) -> list[Capture]:
    captures: list[Capture] = []
    cur: Capture | None = None
    period_ns = 406

    for raw in open(path, errors="replace"):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("----"):
            cur = None
            continue
        if line.startswith("L"):
            cur = Capture(raw_header=line, period_ns=period_ns)
            m = re.match(r"L(\d+)\s+f(\d+)\s+o(\d+)\s+v(\d+)\s+m(\d+)\s+p(\d+)", line)
            if m:
                (
                    cur.couple,
                    cur.rf_field,
                    cur.overrun,
                    cur.valid,
                    cur.backlog,
                    cur.period_ns,
                ) = map(int, m.groups())
                period_ns = cur.period_ns
            captures.append(cur)
            continue
        m = re.match(r"^([RC])\s+(\d+)\s+(\d+)\s+(.*)$", line)
        if not m:
            continue
        if cur is None:
            cur = Capture(period_ns=period_ns)
            captures.append(cur)
        direction, t0, t1, rest = (
            m.group(1),
            int(m.group(2)),
            int(m.group(3)),
            m.group(4),
        )
        bytes_out: list[int] = []
        parity_bad: list[bool] = []
        for tok in rest.split():
            bang = tok.endswith("!")
            hx = tok[:-1] if bang else tok
            if re.fullmatch(r"[0-9A-Fa-f]{2}", hx):
                bytes_out.append(int(hx, 16))
                parity_bad.append(bang)
        data = bytes(bytes_out)
        fr = Frame(
            direction=direction,
            t0=t0,
            t1=t1,
            data=data,
            parity_bad=parity_bad,
            us0=t0 * cur.period_ns / 1000.0,
            us1=t1 * cur.period_ns / 1000.0,
        )
        cur.frames.append(fr)
    return captures


class Ansi:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    TITLE = "\033[38;5;255m"
    META = "\033[38;5;245m"
    BOX = "\033[38;5;66m"
    COUPLE_STRONG = "\033[38;5;114m"  # green
    COUPLE_FAIR = "\033[38;5;214m"  # orange
    COUPLE_WEAK = "\033[38;5;203m"  # red
    PCD = "\033[38;5;114m"  # soft green — reader
    PICC = "\033[38;5;75m"  # steel blue — card
    HEX = "\033[38;5;252m"
    NOTE = "\033[38;5;146m"
    ENC_BADGE = "\033[38;5;176m"
    CRC_OK = "\033[38;5;108m"
    CRC_BAD = "\033[38;5;167m"
    PARITY = "\033[38;5;214m"  # amber bang, not full-row red
    CRYPTO_OK = "\033[38;5;84m"
    CRYPTO_BAD = "\033[38;5;203m"
    GAP = "\033[38;5;240m"

    @staticmethod
    def enabled() -> bool:
        if os.environ.get("NO_COLOR"):
            return False
        return sys.stdout.isatty()


def _c(color: str, text: str, on: bool) -> str:
    if not on or not text:
        return text
    return f"{color}{text}{Ansi.RESET}"


def _fmt_hex(data: bytes, parity_bad: list[bool], color: bool) -> str:
    parts = []
    for b, p in zip(data, parity_bad):
        tok = f"{b:02X}"
        if p:
            tok = _c(Ansi.PARITY, tok + "!", color)
        else:
            tok = _c(Ansi.HEX, tok, color)
        parts.append(tok)
    return " ".join(parts)


def _bcc_ok(data: bytes) -> bool:
    if len(data) != 5:
        return False
    return (data[0] ^ data[1] ^ data[2] ^ data[3]) == data[4]


def _integrity(fr: Frame) -> str:
    """Short integrity badge for our frames (not a CRC column clone)."""
    if fr.plain is not None:
        if len(fr.plain) >= 3:
            return "crc+" if crc_ok(fr.plain) else "crc-"
        return "enc"
    if fr.encrypted:
        return "enc"
    if fr.annotation == "AUTH: nt":
        return ""
    if fr.annotation == "UID+BCC" or (fr.direction == "C" and len(fr.data) == 5):
        return "bcc+" if _bcc_ok(fr.data) else "bcc-"
    if len(fr.data) < 3:
        return ""
    return "crc+" if crc_ok(fr.data) else "crc-"


def _couple_style(couple: int) -> tuple[str, str]:
    """Return (bar, ansi color) for coupling strength."""
    if couple == 0:
        return "███", Ansi.COUPLE_STRONG
    if couple == 1:
        return "██░", Ansi.COUPLE_FAIR
    if couple == 2:
        return "█░░", Ansi.COUPLE_WEAK
    return "░░░", Ansi.META


def _fmt_gap(us: float | None) -> str:
    if us is None:
        return "   ·   "
    if us >= 1000:
        return f"+{us/1000:5.2f}ms"
    return f"+{us:5.1f}µs"


def _fmt_dur(us: float) -> str:
    if us >= 1000:
        return f"{us/1000:5.2f}ms"
    return f"{us:5.1f}µs"


def _fmt_us(us: float | None, width: int = 8) -> str:
    if us is None:
        return f"{'—':>{width}}"
    if abs(us) >= 1000:
        return f"{us/1000:{width - 2}.2f}ms"
    return f"{us:{width - 1}.1f}µs"


def _median(vals: list[float]) -> float | None:
    if not vals:
        return None
    s = sorted(vals)
    n = len(s)
    mid = n // 2
    if n % 2:
        return s[mid]
    return (s[mid - 1] + s[mid]) / 2.0


# Reader→card exchange pairs for timing summary (label substrings).
_EXCHANGE_STEPS: list[tuple[str, str, str]] = [
    ("WUPA/REQA → ATQA", "WUPA", "ATQA"),
    ("WUPA/REQA → ATQA", "REQA", "ATQA"),
    ("ANTICOLL → UID+BCC", "ANTICOLL", "UID+BCC"),
    ("SELECT → SAK", "SELECT", "SAK"),
    ("AUTH → nt", "AUTH-", "AUTH: nt"),
    ("nr‖ar → at", "AUTH: nr ar", "AUTH: at"),
]


def _norm_label(lab: str) -> str:
    return lab.replace(" · enc", "").strip()


def _find_exchange_pair(
    frames: list[Frame], r_sub: str, c_sub: str
) -> tuple[Frame, Frame] | None:
    for i, fr in enumerate(frames):
        if fr.direction != "R":
            continue
        lab = _norm_label(fr.annotation)
        if r_sub == "AUTH-":
            if not lab.startswith("AUTH-"):
                continue
        elif r_sub not in lab:
            continue
        for j in range(i + 1, min(i + 4, len(frames))):
            nxt = frames[j]
            if nxt.direction != "C":
                continue
            nlab = _norm_label(nxt.annotation)
            if c_sub in nlab:
                return fr, nxt
            break
    return None


def _collect_step_timings(
    captures: list[Capture],
) -> tuple[dict[str, dict[str, list[float]]], int]:
    """Median-ready samples: step → {r_dur, r2c, c_dur, c2r} lists."""
    buckets: dict[str, dict[str, list[float]]] = {}
    n_used = 0
    ranked = sorted(
        [c for c in captures if c.frames],
        key=lambda c: (
            0 if c.couple == 0 else 1 if c.couple == 1 else 2,
            -len(c.frames),
        ),
    )
    for cap in ranked:
        frames = cap.frames
        if not any(_norm_label(f.annotation) == "ATQA" for f in frames):
            continue
        used = False
        seen_steps: set[str] = set()
        for step_name, r_sub, c_sub in _EXCHANGE_STEPS:
            if step_name in seen_steps:
                continue
            pair = _find_exchange_pair(frames, r_sub, c_sub)
            if not pair:
                continue
            r, c = pair
            r2c = c.us0 - r.us1
            r_dur = r.us1 - r.us0
            c_dur = c.us1 - c.us0
            c2r = None
            for nxt in frames:
                if nxt.us0 >= c.us1 and nxt.direction == "R":
                    c2r = nxt.us0 - c.us1
                    break
            b = buckets.setdefault(
                step_name, {"r_dur": [], "r2c": [], "c_dur": [], "c2r": []}
            )
            b["r_dur"].append(r_dur)
            b["r2c"].append(r2c)
            b["c_dur"].append(c_dur)
            if c2r is not None:
                b["c2r"].append(c2r)
            seen_steps.add(step_name)
            used = True
        if used:
            n_used += 1
    return buckets, n_used


def _card_identity(captures: list[Capture], tracker: AuthAnnotator) -> dict[str, str]:
    uid = atqa = sak = auth = ""
    for s in tracker.sessions:
        if s.uid:
            uid = f"{s.uid:08X}"
            auth = f"Key{s.key_type} blk {s.block}"
            break
    for cap in captures:
        for fr in cap.frames:
            lab = _norm_label(fr.annotation)
            if lab == "ATQA" and len(fr.data) >= 2 and not atqa:
                atqa = f"{fr.data[0]:02X}{fr.data[1]:02X}"
            if lab.startswith("SAK") and not sak:
                sak = lab.replace("SAK ", "")
            if lab.startswith("SELECT") and len(fr.data) >= 6 and not uid:
                uid = fr.data[2:6].hex().upper()
    return {
        "uid": uid or "—",
        "atqa": atqa or "—",
        "sak": sak or "—",
        "auth": auth or "—",
    }


def render_summary(
    captures: list[Capture], tracker: AuthAnnotator, color: bool, width: int = 78
) -> None:
    live = [c for c in captures if c.frames]
    n_r = sum(1 for c in live for f in c.frames if f.direction == "R")
    n_c = sum(1 for c in live for f in c.frames if f.direction == "C")
    couple_counts = {0: 0, 1: 0, 2: 0}
    for c in live:
        if c.couple in couple_counts:
            couple_counts[c.couple] += 1

    ident = _card_identity(captures, tracker)
    buckets, n_timed = _collect_step_timings(captures)

    top = "┏ summary " + "━" * max(8, width - 10)
    print(_c(Ansi.BOX, top, color))
    print(
        _c(Ansi.META, "┃ ", color)
        + _c(Ansi.BOLD + Ansi.TITLE, "card  ", color)
        + _c(
            Ansi.NOTE,
            f"UID {ident['uid']}   ATQA {ident['atqa']}   SAK {ident['sak']}",
            color,
        )
    )
    key_line = "—"
    if tracker.key is not None:
        src = f" via {tracker.key_source}" if tracker.key_source else ""
        key_line = f"{tracker.key:012X}{src}"
    print(
        _c(Ansi.META, "┃ ", color)
        + _c(Ansi.BOLD + Ansi.TITLE, "auth  ", color)
        + _c(Ansi.NOTE, f"{ident['auth']}   key ", color)
        + _c(
            Ansi.CRYPTO_OK if tracker.key is not None else Ansi.META,
            key_line,
            color,
        )
    )
    auth_ok = tracker.ar_ok
    auth_fail = tracker.ar_fail + tracker.at_fail
    couple_s = (
        f"strong×{couple_counts[0]}  fair×{couple_counts[1]}  weak×{couple_counts[2]}"
    )
    print(
        _c(Ansi.META, "┃ ", color)
        + _c(Ansi.BOLD + Ansi.TITLE, "dump  ", color)
        + _c(
            Ansi.META,
            f"{len(live)} captures · R={n_r} C={n_c} · "
            f"auth ok={auth_ok} fail={auth_fail} · {couple_s}",
            color,
        )
    )
    if tracker.blocks:
        print(_c(Ansi.BOX, "┃", color))
        print(
            _c(Ansi.META, "┃ ", color)
            + _c(Ansi.BOLD + Ansi.TITLE, "read  ", color)
            + _c(Ansi.META, f"{len(tracker.blocks)} block(s) decrypted", color)
        )
        for blk in sorted(tracker.blocks):
            hx = tracker.blocks[blk].hex().upper()
            spaced = " ".join(hx[i : i + 2] for i in range(0, len(hx), 2))
            print(
                _c(Ansi.META, "┃   ", color)
                + _c(Ansi.NOTE, f"blk {blk:02d}  {spaced}", color)
            )

    if buckets:
        print(_c(Ansi.BOX, "┃", color))
        print(
            _c(Ansi.META, "┃ ", color)
            + _c(
                Ansi.GAP,
                f"exchange timings  (median of {n_timed} captures with ATQA)",
                color,
            )
        )
        hdr = f"┃ {'step':<22}  {'R dur':>8}  {'R→C':>8}  {'C dur':>8}  {'C→R':>8}"
        print(_c(Ansi.GAP, hdr, color))
        # Stable step order
        order = [
            "WUPA/REQA → ATQA",
            "ANTICOLL → UID+BCC",
            "SELECT → SAK",
            "AUTH → nt",
            "nr‖ar → at",
        ]
        for step in order:
            b = buckets.get(step)
            if not b or not b["r_dur"]:
                continue
            line = (
                f"┃ {step:<22}  "
                f"{_fmt_us(_median(b['r_dur']))}  "
                f"{_fmt_us(_median(b['r2c']))}  "
                f"{_fmt_us(_median(b['c_dur']))}  "
                f"{_fmt_us(_median(b['c2r']))}"
            )
            print(_c(Ansi.NOTE, line, color))
        print(
            _c(Ansi.GAP, "┃   R→C = card reply delay   C→R = reader turnaround", color)
        )

    print(_c(Ansi.BOX, "┗" + "━" * (width - 1), color))


def print_banner(color: bool) -> None:
    print(_c(Ansi.PCD, BANNER.rstrip("\n"), color))
    print(
        _c(
            Ansi.META,
            "  Flipper NFC Sniffer  ·  offline decode & Crypto1 key recovery",
            color,
        )
    )
    print()


def print_guide(color: bool) -> None:
    print(_c(Ansi.BOLD + Ansi.TITLE, "  What this does", color))
    print(
        _c(
            Ansi.META,
            "    Decode SD dumps from the Flipper NFC Sniffer app\n"
            "    Label ISO14443-A reader↔card frames with timings\n"
            "    Auto-recover MIFARE Classic keys (mfkey64) when auth is complete\n"
            "    Summarize card identity and exchange delays",
            color,
        )
    )
    print()
    print(_c(Ansi.BOLD + Ansi.TITLE, "  How to run", color))
    me = Path(sys.argv[0]).name
    print(
        _c(
            Ansi.NOTE,
            f"    {me} <sniff.txt>                 decode + recover key\n"
            f"    {me} <sniff.txt> --key HEX       verify with a known key\n"
            f"    {me} <sniff.txt> --no-recover    annotate only\n"
            f"    {me} <sniff.txt> --plain         machine-friendly lines",
            color,
        )
    )
    print()
    print(
        _c(
            Ansi.META,
            "  Sniffs are usually at  ext/nfc_sniffer/YYYYMMDD_HHMMSS.txt  on the SD card.",
            color,
        )
    )
    print()


def prompt_sniff_path(color: bool) -> str | None:
    """Interactive path entry; returns path or None if cancelled."""
    print(_c(Ansi.BOLD + Ansi.TITLE, "  Select sniff file", color))
    print(_c(Ansi.META, "  Paste or type the path to a .txt dump, then Enter.", color))
    print(_c(Ansi.META, "  Leave empty or type q to quit.", color))
    print()
    try:
        raw = input(_c(Ansi.PCD, "  sniff> ", color)).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return None
    if not raw or raw.lower() in ("q", "quit", "exit"):
        return None
    # Strip drag-drop quotes
    if len(raw) >= 2 and raw[0] == raw[-1] and raw[0] in "\"'":
        raw = raw[1:-1]
    return os.path.expanduser(raw)


class ProgressBar:
    """Simple TTY progress line on stderr (keeps stdout clean for --plain)."""

    def __init__(self, label: str, color: bool, stream=None) -> None:
        self.label = label
        self.color = color and Ansi.enabled()
        self.stream = stream or sys.stderr
        self._last = -1
        self._t0 = time.monotonic()
        self._tty = hasattr(self.stream, "isatty") and self.stream.isatty()

    def __call__(self, done: int, total: int) -> None:
        if not self._tty or total <= 0:
            return
        pct = min(100, int(100 * done / total))
        if pct == self._last and done < total:
            return
        self._last = pct
        width = 28
        filled = int(width * pct / 100)
        bar = "█" * filled + "░" * (width - filled)
        elapsed = time.monotonic() - self._t0
        line = f"  {self.label}  {bar}  {pct:3d}%  {elapsed:5.1f}s"
        if self.color:
            line = f"{Ansi.PICC}{line}{Ansi.RESET}"
        self.stream.write("\r" + line + "   ")
        self.stream.flush()
        if done >= total:
            self.stream.write("\n")
            self.stream.flush()

    def finish_msg(self, msg: str, ok: bool = True) -> None:
        if self._tty:
            self.stream.write("\r" + " " * 72 + "\r")
            self.stream.flush()
        mark = "✓" if ok else "✗"
        col = Ansi.CRYPTO_OK if ok else Ansi.CRYPTO_BAD
        print(_c(col, f"  {mark} {msg}", self.color), file=self.stream)
        self.stream.flush()


def stage(n: int, total: int, title: str, color: bool) -> None:
    print(
        _c(Ansi.META, f"  [{n}/{total}] ", color)
        + _c(Ansi.BOLD + Ansi.TITLE, title, color)
    )


def render_tui(
    path: str,
    captures: list[Capture],
    tracker: AuthAnnotator,
    color: bool,
    *,
    show_title: bool = True,
) -> None:
    live = [c for c in captures if c.frames]
    width = 78

    if show_title:
        print(_c(Ansi.BOLD + Ansi.TITLE, "nfc_sniffer  ·  decode", color))
        print(_c(Ansi.META, path, color))
        if tracker.key is not None:
            src = f"  via {tracker.key_source}" if tracker.key_source else ""
            print(_c(Ansi.CRYPTO_OK, f"crypto1  key={tracker.key:012X}{src}", color))
        print()

    for idx, cap in enumerate(live, 1):
        bar, couple_col = _couple_style(cap.couple)
        top = f"┏ capture {idx:02d} " + "━" * max(8, width - 14 - len(str(idx)))
        print(_c(Ansi.BOX, top, color))
        print(
            _c(Ansi.META, "┃ ", color)
            + _c(couple_col, f"{bar} {cap.couple_name}", color)
            + _c(
                Ansi.META,
                f"   field={cap.rf_field}   overrun={cap.overrun}   valid={cap.valid}",
                color,
            )
        )
        print(
            _c(
                Ansi.META,
                f"┃ backlog={cap.backlog}   period={cap.period_ns}ns   "
                f"frames={len(cap.frames)}",
                color,
            )
        )
        print(_c(Ansi.BOX, "┃", color))

        w_lab = 20
        legend = (
            f"┃ {'t0 µs':>10}  {'':1} {'dur':>7}  {'gap':>8}   "
            f"{'label':<{w_lab}}  {'':4}  wire"
        )
        print(_c(Ansi.GAP, legend, color))

        prev_end: float | None = None
        for fr in cap.frames:
            dur = fr.us1 - fr.us0
            gap = None if prev_end is None else (fr.us0 - prev_end)
            prev_end = fr.us1

            dir_col = Ansi.PCD if fr.direction == "R" else Ansi.PICC
            arrow = "▸" if fr.direction == "R" else "◂"
            integ = _integrity(fr)
            if integ.startswith("crc+") or integ.startswith("bcc+"):
                integ_s = _c(Ansi.CRC_OK, f"{integ:<4}", color)
            elif integ.startswith("crc-") or integ.startswith("bcc-"):
                integ_s = _c(Ansi.CRC_BAD, f"{integ:<4}", color)
            elif integ == "enc":
                integ_s = _c(Ansi.ENC_BADGE, f"{integ:<4}", color)
            else:
                integ_s = f"{'':4}"

            if fr.plain is not None:
                hx = _fmt_hex(fr.plain, [False] * len(fr.plain), color)
            else:
                hx = _fmt_hex(fr.data, fr.parity_bad, color)
            label = fr.annotation or ""
            if fr.encrypted and "enc" not in label.lower():
                label = (label + " · enc").strip(" ·")
            lab_plain = f"{label:<{w_lab}}"
            lab_s = _c(Ansi.NOTE, lab_plain, color)

            t0 = _c(dir_col, f"{fr.us0:10.1f}", color)
            dch = _c(dir_col + Ansi.BOLD, f"{arrow}{fr.direction}", color)
            dur_s = _c(Ansi.META, f"{_fmt_dur(dur):>7}", color)
            gap_s = _c(Ansi.GAP, f"{_fmt_gap(gap):>8}", color)

            print(
                _c(Ansi.BOX, "┃ ", color)
                + f"{t0}  {dch} {dur_s}  {gap_s}   {lab_s}  {integ_s}  {hx}"
            )

            if fr.crypto_note:
                if fr.crypto_ok is True:
                    body = _c(Ansi.CRYPTO_OK, f"✓ {fr.crypto_note}", color)
                elif fr.crypto_ok is False:
                    body = _c(Ansi.CRYPTO_BAD, f"✗ {fr.crypto_note}", color)
                else:
                    body = _c(Ansi.META, fr.crypto_note, color)
                print(
                    _c(Ansi.BOX, "┃ ", color)
                    + _c(Ansi.META, "           ╰ ", color)
                    + body
                )

        print(_c(Ansi.BOX, "┗" + "━" * (width - 1), color))
        print()

    render_summary(captures, tracker, color, width=width)


def render_plain(path: str, captures: list[Capture], tracker: AuthAnnotator) -> None:
    print(f"# NFC-sniff dump: {path}")
    if tracker.key is not None:
        src = f" via {tracker.key_source}" if tracker.key_source else ""
        print(f"# crypto1 verify enabled key={tracker.key:012X}{src}")
    n_r = n_c = 0
    for cap in captures:
        if cap.raw_header:
            print(
                f"# header couple={cap.couple_name} field={cap.rf_field} "
                f"overrun={cap.overrun} valid={cap.valid} backlog={cap.backlog} "
                f"period_ns={cap.period_ns}"
            )
        for fr in cap.frames:
            if fr.direction == "R":
                n_r += 1
            else:
                n_c += 1
            crc = ""
            if fr.plain is not None:
                crc = (
                    " crc_ok"
                    if len(fr.plain) >= 3 and crc_ok(fr.plain)
                    else (" enc" if len(fr.plain) < 3 else " crc_bad")
                )
            elif fr.encrypted:
                crc = " enc"
            elif len(fr.data) >= 3:
                crc = " crc_ok" if crc_ok(fr.data) else " crc_bad"
            par = " parity!" if any(fr.parity_bad) else ""
            if fr.plain is not None:
                hx = " ".join(f"{b:02X}" for b in fr.plain)
            else:
                hx = " ".join(
                    f"{b:02X}{'!' if p else ''}" for b, p in zip(fr.data, fr.parity_bad)
                )
            print(
                f"{fr.us0:10.1f}-{fr.us1:10.1f}us  {fr.direction}  {hx}{crc}{par}"
                + (f"  ; {fr.annotation}" if fr.annotation else "")
                + (f"  ; {fr.crypto_note}" if fr.crypto_note else "")
            )
    print(f"# totals R={n_r} C={n_c}")
    summ = tracker.summary()
    if summ:
        print("# " + summ)
    ident = _card_identity(captures, tracker)
    print(
        f"# card UID={ident['uid']} ATQA={ident['atqa']} SAK={ident['sak']} "
        f"auth={ident['auth']}"
    )
    buckets, n_timed = _collect_step_timings(captures)
    if buckets:
        print(f"# exchange timings (median of {n_timed} captures)")
        for step in (
            "WUPA/REQA → ATQA",
            "ANTICOLL → UID+BCC",
            "SELECT → SAK",
            "AUTH → nt",
            "nr‖ar → at",
        ):
            b = buckets.get(step)
            if not b or not b["r_dur"]:
                continue
            print(
                f"#   {step}: R={_fmt_us(_median(b['r_dur']))}  "
                f"R→C={_fmt_us(_median(b['r2c']))}  "
                f"C={_fmt_us(_median(b['c_dur']))}  "
                f"C→R={_fmt_us(_median(b['c2r']))}"
            )


def _clear_frame_crypto(captures: list[Capture]) -> None:
    for cap in captures:
        for fr in cap.frames:
            fr.annotation = ""
            fr.encrypted = False
            fr.crypto_note = ""
            fr.crypto_ok = None
            fr.plain = None


def decode_nfc_sniff_text(
    path: str,
    key: int | None = None,
    plain: bool = False,
    color: bool | None = None,
    recover: bool = True,
    *,
    staged: bool = False,
) -> int:
    """Decode dump. Returns 0 on success, 1 on hard failure (missing file)."""
    use_color = Ansi.enabled() if color is None else color
    path = os.path.expanduser(path)
    if not os.path.isfile(path):
        print(
            _c(Ansi.CRYPTO_BAD, f"  file not found: {path}", use_color), file=sys.stderr
        )
        return 1

    steps = 4 if (key is None and recover) else 3
    step = 1

    if staged and not plain:
        stage(step, steps, "Load dump", use_color)
        print(_c(Ansi.META, f"       {path}", use_color))
    step += 1

    captures = parse_dump(path)
    n_frames = sum(len(c.frames) for c in captures)
    if staged and not plain:
        print(
            _c(
                Ansi.META,
                f"       {len(captures)} capture blocks · {n_frames} frames",
                use_color,
            )
        )

    if staged and not plain:
        stage(step, steps, "Parse & label ISO14443-A", use_color)
    step += 1

    key_source = "cli" if key is not None else ""
    recover_info: RecoverResult | None = None
    tracker = AuthAnnotator(key, key_source=key_source)
    for cap in captures:
        tracker.begin_capture()
        for fr in cap.frames:
            tracker.annotate(fr)

    if staged and not plain:
        print(
            _c(
                Ansi.META,
                f"       {len(tracker.sessions)} Classic auth session(s) found",
                use_color,
            )
        )

    if key is None and recover:
        if staged and not plain:
            stage(step, steps, "Crypto1 key recovery (mfkey64)", use_color)
            sys.stdout.flush()
        show_prog = staged and not plain and sys.stdout.isatty()
        bar = (
            ProgressBar("mfkey64", use_color, stream=sys.stdout) if show_prog else None
        )
        recover_info = recover_key(tracker.sessions, progress=bar)
        if recover_info.key is not None:
            key = recover_info.key
            key_source = recover_info.tool or "mfkey"
            if staged and not plain:
                if bar:
                    bar.finish_msg(
                        f"recovered key {key:012X}  ({recover_info.detail})", ok=True
                    )
                else:
                    print(
                        _c(
                            Ansi.CRYPTO_OK,
                            f"  ✓ recovered key {key:012X}  ({recover_info.detail})",
                            use_color,
                        )
                    )
            _clear_frame_crypto(captures)
            tracker = AuthAnnotator(key, key_source=key_source)
            for cap in captures:
                tracker.begin_capture()
                for fr in cap.frames:
                    tracker.annotate(fr)
        else:
            if staged and not plain:
                msg = f"{recover_info.status}: {recover_info.detail}"
                if bar:
                    bar.finish_msg(msg, ok=False)
                else:
                    print(_c(Ansi.CRYPTO_BAD, f"  ✗ {msg}", use_color))
        step += 1

    if staged and not plain:
        stage(step, steps, "Render timeline", use_color)
        print()

    if plain:
        if recover_info is not None and recover_info.key is None:
            print(f"# recover: {recover_info.status} — {recover_info.detail}")
        render_plain(path, captures, tracker)
    else:
        if recover_info is not None and recover_info.key is None and not staged:
            print(
                _c(
                    Ansi.CRYPTO_BAD,
                    f"recover  {recover_info.status}: {recover_info.detail}",
                    use_color,
                )
            )
            print()
        render_tui(path, captures, tracker, use_color, show_title=not staged)

    return 0


def main() -> None:
    ap = argparse.ArgumentParser(
        description=(
            "NFC Sniffer offline decoder — timeline TUI + pure-Python mfkey64. "
            "Run without arguments for an interactive guide."
        )
    )
    ap.add_argument("path", nargs="?", help="sniff .txt dump from SD (optional)")
    ap.add_argument(
        "--key",
        type=parse_key,
        metavar="HEX",
        help="6-byte MIFARE Classic key (12 hex digits); skips auto-recovery",
    )
    ap.add_argument(
        "--no-recover",
        action="store_true",
        help="do not run mfkey* even when --key is omitted",
    )
    ap.add_argument(
        "--plain",
        action="store_true",
        help="legacy plain one-line output (no table / no banner)",
    )
    ap.add_argument(
        "--no-banner",
        action="store_true",
        help="skip ASCII banner",
    )
    ap.add_argument(
        "--color",
        choices=("auto", "always", "never"),
        default="auto",
        help="ANSI colors (default: auto)",
    )
    ap.add_argument(
        "--selftest",
        action="store_true",
        help="run Crypto1/PRNG self-test and exit",
    )
    args = ap.parse_args()

    if args.selftest:
        raise SystemExit(selftest())

    color_opt = {"auto": None, "always": True, "never": False}[args.color]
    use_color = Ansi.enabled() if color_opt is None else color_opt
    path = args.path

    if not args.plain and not args.no_banner:
        print_banner(use_color)

    if not path:
        print_guide(use_color)
        if not sys.stdin.isatty():
            print(
                _c(
                    Ansi.META,
                    "  (non-interactive) pass a sniff path:  decode.py <file.txt>",
                    use_color,
                )
            )
            raise SystemExit(2)
        path = prompt_sniff_path(use_color)
        if not path:
            print(_c(Ansi.META, "  cancelled.", use_color))
            raise SystemExit(0)
        print()

    raise SystemExit(
        decode_nfc_sniff_text(
            path,
            key=args.key,
            plain=args.plain,
            color=color_opt,
            recover=not args.no_recover,
            staged=not args.plain,
        )
    )


if __name__ == "__main__":
    main()
