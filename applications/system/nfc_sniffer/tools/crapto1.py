#!/usr/bin/env python3
"""Pure-Python Crypto1 / crapto1 helpers for mfkey64 (and light rollback).

Port of bla’s crapto1 (GPLv2+) — enough for offline Classic key recovery from
one complete auth (uid, nt, {nr}, {ar}, {at}).
"""
from __future__ import annotations

from dataclasses import dataclass

LF_POLY_ODD = 0x29CE5C
LF_POLY_EVEN = 0x870804


def BIT(x: int, n: int) -> int:
    return (x >> n) & 1


def BEBIT(x: int, n: int) -> int:
    return BIT(x, n ^ 24)


def evenparity32(x: int) -> int:
    x ^= x >> 16
    x ^= x >> 8
    x ^= x >> 4
    x ^= x >> 2
    x ^= x >> 1
    return x & 1


def filter_(x: int) -> int:
    f = (0xF22C0 >> (x & 0xF)) & 16
    f |= (0x6C9C0 >> ((x >> 4) & 0xF)) & 8
    f |= (0x3C8B0 >> ((x >> 8) & 0xF)) & 4
    f |= (0x1E458 >> ((x >> 12) & 0xF)) & 2
    f |= (0x0D938 >> ((x >> 16) & 0xF)) & 1
    return (0xEC57E80A >> f) & 1


# Precompute 20-bit filter LUT (matches C filterlut).
_FILTER_LUT = bytearray(1 << 20)
for _i in range(1 << 20):
    _FILTER_LUT[_i] = filter_(_i)


def filter(x: int) -> int:
    return _FILTER_LUT[x & 0xFFFFF]


def extend_table_simple(tbl: list[int], bit: int) -> list[int]:
    out: list[int] = []
    for x in tbl:
        x = (x << 1) & 0xFFFFFFFF
        f0 = filter(x)
        f1 = filter(x | 1)
        if f0 != f1:
            out.append(x | (f0 ^ bit))
        elif f0 == bit:
            out.append(x)
            out.append(x | 1)
    return out


S1 = [
    0x62141,
    0x310A0,
    0x18850,
    0x0C428,
    0x06214,
    0x0310A,
    0x85E30,
    0xC69AD,
    0x634D6,
    0xB5CDE,
    0xDE8DA,
    0x6F46D,
    0xB3C83,
    0x59E41,
    0xA8995,
    0xD027F,
    0x6813F,
    0x3409F,
    0x9E6FA,
]
S2 = [
    0x3A557B00,
    0x5D2ABD80,
    0x2E955EC0,
    0x174AAF60,
    0x0BA557B0,
    0x05D2ABD8,
    0x0449DE68,
    0x048464B0,
    0x42423258,
    0x278192A8,
    0x156042D0,
    0x0AB02168,
    0x43F89B30,
    0x61FC4D98,
    0x765EAD48,
    0x7D8FDD20,
    0x7EC7EE90,
    0x7F63F748,
    0x79117020,
]
T1 = [
    0x4F37D,
    0x279BE,
    0x97A6A,
    0x4BD35,
    0x25E9A,
    0x12F4D,
    0x097A6,
    0x80D66,
    0xC4006,
    0x62003,
    0xB56B4,
    0x5AB5A,
    0xA9318,
    0xD0F39,
    0x6879C,
    0xB057B,
    0x582BD,
    0x2C15E,
    0x160AF,
    0x8F6E2,
    0xC3DC4,
    0xE5857,
    0x72C2B,
    0x39615,
    0x98DBF,
    0xC806A,
    0xE0680,
    0x70340,
    0x381A0,
    0x98665,
    0x4C332,
    0xA272C,
]
T2 = [
    0x3C88B810,
    0x5E445C08,
    0x2982A580,
    0x14C152C0,
    0x4A60A960,
    0x253054B0,
    0x52982A58,
    0x2FEC9EA8,
    0x1156C4D0,
    0x08AB6268,
    0x42F53AB0,
    0x217A9D58,
    0x161DC528,
    0x0DAE6910,
    0x46D73488,
    0x25CB11C0,
    0x52E588E0,
    0x6972C470,
    0x34B96238,
    0x5CFC3A98,
    0x28DE96C8,
    0x12CFC0E0,
    0x4967E070,
    0x64B3F038,
    0x74F97398,
    0x7CDC3248,
    0x38CE92A0,
    0x1C674950,
    0x0E33A4A8,
    0x01B959D0,
    0x40DCACE8,
    0x26CEDDF0,
]
C1 = [0x846B5, 0x4235A, 0x211AD]
C2 = [0x1A822E0, 0x21A822E0, 0x21A822E0]


@dataclass
class Crypto1State:
    odd: int = 0
    even: int = 0


def lfsr_recovery64(ks2: int, ks3: int, progress=None) -> list[Crypto1State]:
    """Reverse 64 bits of keystream into possible cipher states.

    progress: optional callable(done: int, total: int) called periodically.
    """
    oks = [0] * 32
    eks = [0] * 32
    for i in range(30, -1, -2):
        oks[i >> 1] = BEBIT(ks2, i)
        oks[16 + (i >> 1)] = BEBIT(ks3, i)
    for i in range(31, -1, -2):
        eks[i >> 1] = BEBIT(ks2, i)
        eks[16 + (i >> 1)] = BEBIT(ks3, i)

    results: list[Crypto1State] = []
    total = 0x100000
    step = total // 100  # ~1% ticks

    for n, i in enumerate(range(0xFFFFF, -1, -1)):
        if progress is not None and (n % step == 0 or n + 1 == total):
            progress(n + 1, total)
        if filter(i) != oks[0]:
            continue

        table = [i]
        ok = True
        for j in range(1, 29):
            table = extend_table_simple(table, oks[j])
            if not table:
                ok = False
                break
        if not ok:
            continue

        low = 0
        for j in range(19):
            low = (low << 1) | evenparity32(i & S1[j])
        hi = [evenparity32(i & T1[j]) for j in range(32)]

        for entry in table:
            cur = entry
            good = True
            for j in range(3):
                cur = (cur << 1) & 0xFFFFFFFF
                cur |= evenparity32((i & C1[j]) ^ (cur & C2[j]))
                if filter(cur) != oks[29 + j]:
                    good = False
                    break
            if not good:
                continue

            win = 0
            for j in range(19):
                win = (win << 1) | evenparity32(cur & S2[j])
            win ^= low
            for j in range(32):
                win = ((win << 1) ^ hi[j] ^ evenparity32(cur & T2[j])) & 0xFFFFFFFF
                if filter(win) != eks[j]:
                    good = False
                    break
            if not good:
                continue

            cur = (cur << 1) | evenparity32(LF_POLY_EVEN & cur)
            odd = cur ^ evenparity32(LF_POLY_ODD & win)
            results.append(Crypto1State(odd=odd & 0xFFFFFFFF, even=win & 0xFFFFFFFF))

    return results


def lfsr_rollback_bit(s: Crypto1State, inp: int, fb: int) -> int:
    s.odd &= 0xFFFFFF
    s.odd, s.even = s.even, s.odd

    out = s.even & 1
    s.even >>= 1
    out ^= LF_POLY_EVEN & s.even
    out ^= LF_POLY_ODD & s.odd
    out ^= 1 if inp else 0
    ret = filter(s.odd)
    out ^= ret & (1 if fb else 0)

    s.even |= evenparity32(out) << 23
    return ret


def lfsr_rollback_word(s: Crypto1State, inp: int, fb: int) -> int:
    ret = 0
    for n in range(31, -1, -1):
        ret |= lfsr_rollback_bit(s, BEBIT(inp, n), fb) << (24 ^ n)
    return ret & 0xFFFFFFFF


def crypto1_get_lfsr(s: Crypto1State) -> int:
    key = 0
    for i in range(23, -1, -1):
        key = (key << 1) | BIT(s.odd, i ^ 3)
        key = (key << 1) | BIT(s.even, i ^ 3)
    return key


def crypto1_bit(s: Crypto1State, inp: int, is_encrypted: int) -> int:
    ret = filter(s.odd)
    feed = ret & (1 if is_encrypted else 0)
    feed ^= 1 if inp else 0
    feed ^= LF_POLY_ODD & s.odd
    feed ^= LF_POLY_EVEN & s.even
    s.even = ((s.even << 1) | evenparity32(feed)) & 0xFFFFFF
    s.odd, s.even = s.even, s.odd
    return ret


def crypto1_word(s: Crypto1State, inp: int, is_encrypted: int) -> int:
    ret = 0
    for n in range(32):
        ret |= crypto1_bit(s, BEBIT(inp, n), is_encrypted) << (24 ^ n)
    return ret & 0xFFFFFFFF


def prng_successor(x: int, n: int) -> int:
    # Match C SWAPENDIAN + LFSR
    x = ((x >> 8) & 0xFF00FF) | ((x & 0xFF00FF) << 8)
    x = ((x >> 16) | (x << 16)) & 0xFFFFFFFF
    for _ in range(n):
        x = (
            x >> 1 | (((x >> 16) ^ (x >> 18) ^ (x >> 19) ^ (x >> 21)) & 1) << 31
        ) & 0xFFFFFFFF
    x = ((x >> 8) & 0xFF00FF) | ((x & 0xFF00FF) << 8)
    x = ((x >> 16) | (x << 16)) & 0xFFFFFFFF
    return x


def mfkey64(
    uid: int,
    nt: int,
    nr_enc: int,
    ar_enc: int,
    at_enc: int,
    progress=None,
) -> int | None:
    """Recover Classic key from one complete auth. Returns 48-bit key or None."""
    p64 = prng_successor(nt, 64)
    ks2 = (ar_enc ^ p64) & 0xFFFFFFFF
    ks3 = (at_enc ^ prng_successor(p64, 32)) & 0xFFFFFFFF
    states = lfsr_recovery64(ks2, ks3, progress=progress)
    if not states:
        return None
    # First state is the recovered one (C mfkey64 uses the list head).
    s = Crypto1State(odd=states[0].odd, even=states[0].even)
    lfsr_rollback_word(s, 0, 0)
    lfsr_rollback_word(s, 0, 0)
    lfsr_rollback_word(s, nr_enc, 1)
    lfsr_rollback_word(s, uid ^ nt, 0)
    return crypto1_get_lfsr(s) & ((1 << 48) - 1)
