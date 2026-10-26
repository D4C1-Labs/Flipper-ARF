#!/usr/bin/env python3
"""Offline Classic key recovery via pure-Python mfkey64 (crapto1.py)."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Iterable

from crapto1 import mfkey64 as py_mfkey64

ProgressCb = Callable[[int, int], None]


@dataclass
class AuthSession:
    uid: int
    key_type: str  # "A" or "B"
    block: int
    nt: int
    nr_enc: int
    ar_enc: int
    at_enc: int | None = None


@dataclass
class RecoverResult:
    key: int | None
    tool: str = ""
    status: str = ""  # ok | no_key | none
    detail: str = ""


def recover_key(
    sessions: Iterable[AuthSession],
    progress: ProgressCb | None = None,
    **_unused,
) -> RecoverResult:
    """Recover a Classic key from sniffed auth sessions (needs uid/nt/nr/ar/at)."""
    sess = [s for s in sessions if s.uid is not None and s.nt is not None]
    if not sess:
        return RecoverResult(
            None, status="none", detail="no Classic auth sessions in dump"
        )

    candidates = [s for s in sess if s.at_enc is not None]
    if not candidates:
        return RecoverResult(
            None,
            status="none",
            detail="need a complete auth with {at} for mfkey64",
        )

    # Try first complete session (same key for all auths on same UID/sector).
    s = candidates[0]
    key = py_mfkey64(s.uid, s.nt, s.nr_enc, s.ar_enc, s.at_enc, progress=progress)
    if key is not None:
        return RecoverResult(
            key,
            tool="mfkey64",
            status="ok",
            detail=f"uid={s.uid:08X} nt={s.nt:08X}",
        )
    return RecoverResult(
        None,
        status="no_key",
        detail="mfkey64 found no key for these nonces",
    )
