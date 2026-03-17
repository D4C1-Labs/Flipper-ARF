#!/usr/bin/env python3
"""
LF Analyzer -- Decodes captures from the LF Sniffer FAP
=========================================================
Analyzes edge timings captured by the Flipper Zero FAP and determines:
  - Modulation type (ASK/FSK/PSK)
  - Encoding (Manchester, Biphase, NRZ, PWM)
  - Bit period and carrier frequency
  - Decoded bit stream
  - Likely protocol (Hitag2, Hitag S, EM4100, etc.)
  - VAG KeylessGO challenge candidate if applicable

Usage:
  python3 lf_analyze.py capture_0000.csv
  python3 lf_analyze.py capture_0000.csv --verbose
  python3 lf_analyze.py capture_0000.csv --protocol hitag2
"""

import argparse
import csv
import sys
import math
from collections import Counter
from pathlib import Path


# -- CSV loader ----------------------------------------------------------------

def load_csv(path: str) -> tuple[list, dict]:
    edges = []
    meta  = {}

    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith('#'):
                # Parse key:value pairs from metadata comment lines
                if 'edges:' in line:
                    for token in line.split():
                        if ':' in token:
                            k, v = token.split(':', 1)
                            try:
                                meta[k.lstrip('#')] = int(v)
                            except ValueError:
                                pass
                continue
            if line.startswith('index'):
                continue  # skip column header
            parts = line.split(',')
            if len(parts) >= 3:
                try:
                    edges.append({
                        'idx':    int(parts[0]),
                        'dur_us': int(parts[1]),
                        'level':  int(parts[2]),
                        'note':   parts[3].strip() if len(parts) > 3 else ''
                    })
                except ValueError:
                    pass

    print(f"[*] Loaded {len(edges)} edges")
    if meta:
        print(f"    Metadata: {meta}")
    return edges, meta


# -- Carrier frequency detection ----------------------------------------------

def analyze_carrier(edges: list, verbose: bool = False) -> dict:
    """
    Detects the LF carrier by measuring the shortest pulses in the capture.
    These correspond to half-periods of the unmodulated carrier.
    """
    durations = [e['dur_us'] for e in edges if 1 < e['dur_us'] < 50]

    if not durations:
        return {'carrier_hz': 0, 'half_period_us': 0}

    counter     = Counter(durations)
    most_common = counter.most_common(20)

    if verbose:
        print("\n[*] Most frequent edge durations (us):")
        for dur, cnt in most_common[:10]:
            print(f"    {dur:4d} us  x{cnt:5d}")

    # Carrier half-period = most frequent pulse in the 1-20 us range
    short = [(d, c) for d, c in most_common if 1 < d < 20]
    if not short:
        short = [(d, c) for d, c in most_common if d < 50]

    if not short:
        return {'carrier_hz': 0, 'half_period_us': 0}

    half_period = short[0][0]
    carrier_hz  = int(1_000_000 / (2 * half_period)) if half_period > 0 else 0

    print(f"\n[*] Detected carrier:")
    print(f"    Half-period : {half_period} us")
    print(f"    Frequency   : {carrier_hz:,} Hz  (~{carrier_hz/1000:.1f} kHz)")

    if 100_000 < carrier_hz < 150_000:
        print(f"    [+] Matches 125 kHz LF band (KeylessGO / RFID)")
    elif 110_000 < carrier_hz < 140_000:
        print(f"    [~] Close to 125 kHz")

    return {
        'carrier_hz':     carrier_hz,
        'half_period_us': half_period,
        'period_us':      half_period * 2,
    }


# -- Packet segmentation ------------------------------------------------------

def find_packets(edges: list, gap_threshold_us: int = 5000) -> list:
    """Splits the edge list into packets separated by gaps."""
    packets = []
    start   = 0

    for i, e in enumerate(edges):
        if e['dur_us'] > gap_threshold_us:
            if i - start > 8:
                packets.append(edges[start:i])
            start = i + 1

    if len(edges) - start > 8:
        packets.append(edges[start:])

    print(f"\n[*] Detected packets: {len(packets)}")
    for i, pkt in enumerate(packets):
        total = sum(e['dur_us'] for e in pkt)
        print(f"    PKT {i}: {len(pkt)} edges,  {total} us  ({total/1000:.1f} ms)")

    return packets


# -- Bit period detection -----------------------------------------------------

def detect_bit_period(packet: list, carrier: dict, verbose: bool = False) -> dict:
    """
    Estimates the bit period from modulated pulse widths.
    VAG KeylessGO uses Manchester at ~4 kbps over 125 kHz, so bit_period ~250 us.
    EM4100 / Hitag use 125 kHz / 32 = ~256 us per bit.
    """
    min_dur   = carrier.get('half_period_us', 4)
    durations = [e['dur_us'] for e in packet if e['dur_us'] > min_dur * 2]

    if not durations:
        return {'bit_period_us': 0, 'baud_rate': 0}

    # Histogram with 10 us resolution
    hist  = Counter(round(d / 10) * 10 for d in durations)
    peaks = sorted(hist.items(), key=lambda x: x[1], reverse=True)

    if verbose:
        print("\n    Modulated pulse durations (top 15):")
        for dur, cnt in peaks[:15]:
            print(f"      {dur:5d} us  x{cnt:4d}")

    if not peaks:
        return {'bit_period_us': 0, 'baud_rate': 0}

    # Bit period = smallest frequently-occurring modulated pulse
    candidates = [d for d, c in peaks if c >= 3]
    if not candidates:
        candidates = [peaks[0][0]]

    half_period = min(candidates)

    # Check if this is a half-period (consecutive same-width pulses that
    # alternate level = Manchester half-periods). If so, double it.
    # Two equal half-periods make one Manchester bit period.
    same_width_pairs = sum(
        1 for i in range(len(packet)-1)
        if abs(packet[i]['dur_us'] - half_period) < half_period*0.3
        and abs(packet[i+1]['dur_us'] - half_period) < half_period*0.3
    )
    total_half_pulses = sum(
        1 for e in packet
        if abs(e['dur_us'] - half_period) < half_period*0.3
    )
    # If >80% of pulses are this width, they are half-periods
    if total_half_pulses > len(packet) * 0.7:
        bit_period = half_period * 2
        is_half = True
    else:
        bit_period = half_period
        is_half = False

    baud_rate = int(1_000_000 / bit_period) if bit_period > 0 else 0

    print(f"\n[*] Estimated bit period: {bit_period} us  ({baud_rate} baud)")
    if is_half:
        print(f"    (half-period detected: {half_period} us x2)")

    if 3800 < baud_rate < 4200:
        print("    -> Likely: Manchester 4 kbps (Hitag2 / Hitag S / EM4100 / VAG)")
    elif 7500 < baud_rate < 8500:
        print("    -> Likely: Manchester 8 kbps")
    elif 1900 < baud_rate < 2100:
        print("    -> Likely: 2 kbps biphase (EM4100)")

    return {
        'bit_period_us': bit_period,
        'baud_rate':     baud_rate,
        'half_bit_us':   bit_period // 2,
    }


# -- Manchester decoder -------------------------------------------------------

def decode_manchester(packet: list, bit_period_us: int, verbose: bool = False) -> list:
    """
    Decodes Manchester-encoded bit stream from edge timings.
    Both half-period and full-period pulses are handled.
    Returns a list of integers (0 or 1).
    """
    if bit_period_us == 0:
        return []

    half      = bit_period_us // 2
    tolerance = half // 2  # +/- 50% of the half-period

    bits = []
    i    = 0

    while i < len(packet):
        dur = packet[i]['dur_us']

        if abs(dur - half) < tolerance:
            # Half-period pulse -- needs the next one to complete a bit
            if i + 1 < len(packet):
                dur2 = packet[i + 1]['dur_us']
                if abs(dur2 - half) < tolerance:
                    level = packet[i]['level']
                    bits.append(1 if level else 0)
                    i += 2
                    continue
            i += 1

        elif abs(dur - bit_period_us) < tolerance:
            # Full-period pulse -- encodes one bit by itself
            level = packet[i]['level']
            bits.append(1 if level else 0)
            i += 1

        else:
            if verbose:
                print(f"    [!] Unexpected duration at edge {i}: {dur} us")
            i += 1

    return bits


# -- Bit / byte helpers -------------------------------------------------------

def bits_to_bytes(bits: list) -> bytes:
    result = bytearray()
    for i in range(0, len(bits) - len(bits) % 8, 8):
        byte = 0
        for j in range(8):
            byte = (byte << 1) | bits[i + j]
        result.append(byte)
    return bytes(result)


def print_bits(bits: list, label: str = "Bits"):
    print(f"\n[*] {label} ({len(bits)} bits):")
    for i in range(0, len(bits), 64):
        chunk  = bits[i:i + 64]
        groups = [chunk[j:j + 8] for j in range(0, len(chunk), 8)]
        line   = '  '.join(''.join(str(b) for b in g) for g in groups)
        print(f"    {i//8:4d}: {line}")

    if len(bits) >= 8:
        data    = bits_to_bytes(bits)
        hex_str = ' '.join(f'{b:02X}' for b in data)
        print(f"\n    HEX: {hex_str}")


# -- Protocol identification --------------------------------------------------

def identify_protocol(bits: list, baud_rate: int, carrier_hz: int) -> str:
    n = len(bits)

    print(f"\n[*] Protocol identification:")
    print(f"    Bits: {n}  |  Baud: {baud_rate}  |  Carrier: {carrier_hz:,} Hz")

    # EM4100: 64-bit frame, 9-bit preamble of all-ones, Manchester
    if 50 < n < 80 and baud_rate > 3000:
        for start in range(n - 9):
            if all(bits[start + j] == 1 for j in range(9)):
                print(f"    -> EM4100 candidate (preamble at bit {start})")
                data_bits = bits[start + 9:]
                if len(data_bits) >= 55:
                    _decode_em4100(data_bits[:55])
                break

    # Hitag2: ~96-bit frame, Manchester 4 kbps
    if 80 < n < 120 and 3500 < baud_rate < 4500:
        print(f"    -> Hitag2 candidate ({n} bits)")
        _analyze_hitag2(bits)

    # Hitag S: variable length, Manchester 4 kbps
    if n > 120 and 3500 < baud_rate < 4500:
        print(f"    -> Hitag S candidate ({n} bits)")

    # VAG KeylessGO challenge: typically 32-64 data bits
    if 20 < n < 80 and 3500 < baud_rate < 4500:
        print(f"    -> VAG KeylessGO challenge candidate")
        _analyze_vag_challenge(bits)

    return "unknown"


def _decode_em4100(bits: list):
    if len(bits) < 55:
        return
    # EM4100 data layout: [version 8b][data 32b][col_parity 4b][stop 1b]
    # Each row: 4 data bits + 1 parity bit
    print("\n    EM4100 decode:")
    customer = (bits[0] << 7) | (bits[1] << 6) | (bits[2] << 5) | \
               (bits[3] << 4) | (bits[5] << 3) | (bits[6] << 2) | \
               (bits[7] << 1) | bits[8]
    print(f"      Customer code: 0x{customer:02X}  ({customer})")


def _analyze_hitag2(bits: list):
    if len(bits) < 32:
        return
    data = bits_to_bytes(bits[:32])
    print(f"\n    Hitag2 first 32 bits: {data.hex().upper()}")
    print(f"    As uint32 BE        : 0x{int.from_bytes(data, 'big'):08X}")


def _analyze_vag_challenge(bits: list):
    """
    Looks for a VAG KeylessGO challenge structure.
    The car transmits: [sync/preamble] [32-bit challenge] [checksum]
    """
    print(f"\n    VAG challenge analysis:")

    # Alternating preamble (010101...) is typically used as sync
    for start in range(min(len(bits) - 8, 20)):
        window      = bits[start:start + 8]
        alternating = all(window[j] != window[j + 1] for j in range(7))
        if alternating:
            print(f"      Alternating sync at bit {start}: {''.join(str(b) for b in window)}")
            payload_start = start + 8
            if len(bits) - payload_start >= 32:
                challenge_bits  = bits[payload_start:payload_start + 32]
                challenge_bytes = bits_to_bytes(challenge_bits)
                challenge_val   = int.from_bytes(challenge_bytes, 'big')
                print(f"      Challenge candidate (32b): 0x{challenge_val:08X}")
                print(f"      As bytes               : {challenge_bytes.hex().upper()}")
            break

    # Also print every 32-bit aligned window as a candidate
    print(f"\n    All 32-bit blocks in capture:")
    for offset in range(0, min(len(bits) - 32, 48), 4):
        chunk = bits[offset:offset + 32]
        val   = 0
        for b in chunk:
            val = (val << 1) | b
        preview = ''.join(str(b) for b in chunk[:16])
        print(f"      offset {offset:3d}: 0x{val:08X}  [{preview}...]")


# -- Full analysis entry point ------------------------------------------------

def analyze_capture(path: str, verbose: bool = False, protocol_hint: str = None):

    print(f"\n{'='*60}")
    print(f"LF Capture Analyzer -- {Path(path).name}")
    print('='*60)

    edges, meta = load_csv(path)

    if not edges:
        print("ERROR: No data found in file.")
        return

    # Step 1: detect carrier
    carrier = analyze_carrier(edges, verbose)

    # Step 2: segment packets
    packets = find_packets(edges)

    if not packets:
        print("\n[!] No packets detected. Check:")
        print("    - Car is emitting 125 kHz LF field")
        print("    - Coil is connected correctly to PB2")
        print("    - LM393 is powered from 3.3V")
        return

    # Step 3: analyze each packet
    for i, pkt in enumerate(packets):
        print(f"\n{'─'*50}")
        print(f"PACKET {i} -- {len(pkt)} edges")
        print('─'*50)

        bit_info   = detect_bit_period(pkt, carrier, verbose)
        bit_period = bit_info.get('bit_period_us', 0)
        baud_rate  = bit_info.get('baud_rate', 0)

        if bit_period == 0:
            print("  [!] Could not determine bit period")
            continue

        # Step 4: Manchester decode
        bits = decode_manchester(pkt, bit_period, verbose)

        if len(bits) < 8:
            print(f"  [!] Only {len(bits)} bits decoded -- likely noise or raw carrier")
            if verbose:
                raw = [e['dur_us'] for e in pkt[:40]]
                print(f"  RAW first durations: {raw}")
            continue

        print_bits(bits, f"Manchester decoded ({len(bits)} bits)")

        # Step 5: identify protocol
        identify_protocol(bits, baud_rate, carrier.get('carrier_hz', 0))

        # Step 6: apply explicit decoder if requested
        if protocol_hint == 'hitag2':
            decode_hitag2_explicit(bits)

    print(f"\n{'='*60}")
    print("SUMMARY")
    print('='*60)
    print(f"  Carrier   : {carrier.get('carrier_hz', 0):,} Hz")
    print(f"  Packets   : {len(packets)}")
    print(f"  Total edges: {len(edges)}")
    print()
    print("NEXT STEPS:")
    print("  1. If you see 'Keyless challenge candidate' -> use the 0xXXXXXXXX value")
    print("     with your AUT64 implementation to compute the expected response.")
    print("  2. If Hitag2 is detected -> the protocol is HMAC-SHA1 / AUT64 depending")
    print("     on the generation.")
    print("  3. If carrier is 125 kHz but no packets appear -> car is not emitting")
    print("     a challenge (get closer, < 30 cm).")


def decode_hitag2_explicit(bits: list):
    """Explicit Hitag2 frame decode when --protocol hitag2 is specified."""
    print("\n[*] Hitag2 frame decode:")
    if len(bits) < 5:
        return

    print(f"  Start of frame : {bits[0]}")
    if len(bits) > 5:
        cmd  = 0
        for b in bits[1:5]:
            cmd = (cmd << 1) | b
        cmds = {
            0b0001: 'REQUEST',
            0b0011: 'SELECT',
            0b0101: 'READ',
            0b1001: 'WRITE'
        }
        print(f"  Command        : 0b{cmd:04b} = {cmds.get(cmd, 'UNKNOWN')}")

    if len(bits) >= 37:
        uid_bits = bits[5:37]
        uid_val  = 0
        for b in uid_bits:
            uid_val = (uid_val << 1) | b
        print(f"  UID candidate  : 0x{uid_val:08X}")


# -- CLI ----------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Analyze LF captures from the Flipper Zero LF Sniffer FAP',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 lf_analyze.py capture_0000.csv
  python3 lf_analyze.py capture_0000.csv --verbose
  python3 lf_analyze.py capture_0000.csv --protocol hitag2
        """
    )
    parser.add_argument('capture',          help='CSV file from LF Sniffer FAP')
    parser.add_argument('--verbose', '-v',  action='store_true')
    parser.add_argument('--protocol',
                        choices=['hitag2', 'em4100', 'Keyless', 'auto'],
                        default='auto',
                        help='Force a specific protocol decoder (default: auto)')
    args = parser.parse_args()

    if not Path(args.capture).exists():
        print(f"ERROR: {args.capture} not found")
        sys.exit(1)

    analyze_capture(
        args.capture,
        verbose=args.verbose,
        protocol_hint=args.protocol if args.protocol != 'auto' else None
    )


if __name__ == '__main__':
    main()