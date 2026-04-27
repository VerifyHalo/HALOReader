#!/usr/bin/env python3
"""
Convert Data2 .dat files → Intan RHD v2 format for the seizure pipeline.

Data2 format
  - 9 channels, float32 mV, 256 Hz, sequential storage (all of ch0, then all of ch1, ...)
  - 1 hour per file  →  921 600 samples per channel

RHD v2 format (what the pipeline's rhd_reader.cpp expects)
  - uint16 ADC codes, 128-sample interleaved blocks
  - Conversion:  ADC = clamp(round(val_mV * 1000 / 0.195 + 32768), 0, 65535)

Channel mapping
  dat ch 0-8  →  RHD ch 2-10
  RHD ch 0-1  →  dummy (32768 = 0 V), because the pipeline skips them
  extractDataChannels() will expose dat channels as FPGA channels 0-8

Usage
  python3 dat_to_rhd.py                          # convert all files in ./Data2
  python3 dat_to_rhd.py --file Data2/MSO1CDP6IES.dat
  python3 dat_to_rhd.py --outdir /path/to/output
  python3 dat_to_rhd.py --threshold-info         # print NEO range to guide threshold tuning
"""

import struct
import numpy as np
import os
import sys
import argparse
from pathlib import Path

# ── Data2 constants ──────────────────────────────────────────────────────────
FS               = 256
N_DAT_CHANNELS   = 9
SAMPLES_PER_HOUR = FS * 3600          # 921 600

# ── RHD v2 constants ─────────────────────────────────────────────────────────
SAMPLES_PER_BLOCK = 128               # fixed for RHD v2
N_RHD_CHANNELS    = N_DAT_CHANNELS + 2  # 2 dummy + 9 real = 11
RHD_MAGIC         = 0xC6912702

# Intan ADC formula:  V_µV = (ADC − 32768) × 0.195
# Inverse:            ADC  = V_µV / 0.195 + 32768
ADC_OFFSET  = 32768
ADC_SCALE   = 1000.0 / 0.195         # mV  → ADC units  (~5128.2 counts/mV)


# ── Struct helpers ────────────────────────────────────────────────────────────
def _u16(v): return struct.pack('<H', int(v) & 0xFFFF)
def _i16(v): return struct.pack('<h', int(v))
def _u32(v): return struct.pack('<I', int(v) & 0xFFFFFFFF)
def _f32(v): return struct.pack('<f', float(v))


def _qstring(s: str | None) -> bytes:
    """Encode a Qt-style QString: uint32 byte-length + UTF-16LE, or 0xFFFFFFFF for null."""
    if not s:
        return _u32(0xFFFFFFFF)
    encoded = s.encode('utf-16-le')
    return _u32(len(encoded)) + encoded


# ── RHD header writer ─────────────────────────────────────────────────────────
def _build_header(sample_rate: float, n_channels: int) -> bytes:
    """Return the binary RHD v2 header for a single amplifier group."""
    b = bytearray()

    # Magic + version 2.0
    b += _u32(RHD_MAGIC)
    b += _u16(2) + _u16(0)

    # Sample rate
    b += _f32(sample_rate)

    # Frequency settings (26 bytes): dsp_enabled + 6 × float32 filter freqs
    b += _u16(0)        # dsp_enabled = off
    b += _f32(0.0)      # actual_dsp_cutoff
    b += _f32(0.0)      # actual_lower_bandwidth
    b += _f32(10000.0)  # actual_upper_bandwidth
    b += _f32(0.0)      # desired_dsp_cutoff
    b += _f32(0.0)      # desired_lower_bandwidth
    b += _f32(10000.0)  # desired_upper_bandwidth
    assert len(b) == 4 + 4 + 4 + 26, f"freq offset wrong: {len(b)}"

    # Notch filter (uint16): 0 = none
    b += _u16(0)

    # Impedance test frequencies (2 × float32)
    b += _f32(0.0) + _f32(0.0)

    # Notes (3 null QStrings)
    b += _qstring(None) * 3

    # num_temp_sensor_channels (v ≥ 1.1)
    b += _u16(0)

    # eval_board_mode (v ≥ 1.3)
    b += _u16(0)

    # reference_channel QString (v > 1)
    b += _qstring(None)

    # num_signal_groups = 1
    b += _u16(1)

    # ── Signal group ──
    b += _qstring("Port A")   # group name
    b += _qstring("A")        # group prefix
    b += _u16(1)              # enabled
    b += _u16(n_channels)     # num channels in group
    b += _u16(n_channels)     # num amplifier channels in group (dummy field)

    for ch in range(n_channels):
        label = f"A-{ch:03d}"
        b += _qstring(label)  # native_channel_name
        b += _qstring(label)  # custom_channel_name
        b += _i16(ch)         # native_order
        b += _i16(ch)         # custom_order
        b += _i16(0)          # signal_type = 0 (amplifier)
        b += _i16(1)          # channel_enabled
        b += _i16(ch)         # chip_channel
        b += _i16(0)          # board_stream
        # trigger channel info (4 × int16 = 8 bytes)
        b += _i16(0) * 4
        # impedance (2 × float32 = 8 bytes)
        b += _f32(0.0) + _f32(0.0)

    return bytes(b)


# ── ADC conversion ────────────────────────────────────────────────────────────
def _to_adc(data_mv: np.ndarray) -> np.ndarray:
    """float32 mV  →  uint16 Intan ADC codes, clamped to [0, 65535]."""
    adc = np.round(data_mv.astype(np.float64) * ADC_SCALE + ADC_OFFSET).astype(np.int32)
    return np.clip(adc, 0, 65535).astype(np.uint16)


# ── Core converter ────────────────────────────────────────────────────────────
def convert(dat_path: Path, rhd_path: Path) -> None:
    stem = dat_path.stem
    print(f"  [{stem}]  loading ...", end='\r', flush=True)

    raw = np.fromfile(dat_path, dtype=np.float32)
    expected = N_DAT_CHANNELS * SAMPLES_PER_HOUR
    if raw.size != expected:
        raise ValueError(
            f"{dat_path.name}: expected {expected} floats "
            f"({N_DAT_CHANNELS} ch × {SAMPLES_PER_HOUR} samples), got {raw.size}"
        )

    # (9, 921600) — sequential channel layout
    data_mv = raw.reshape(N_DAT_CHANNELS, SAMPLES_PER_HOUR)
    adc     = _to_adc(data_mv)   # (9, 921600)  uint16

    # Prepend 2 dummy channels (0 V = ADC 32768) so the pipeline's ch-skip logic works
    dummy  = np.full((2, SAMPLES_PER_HOUR), ADC_OFFSET, dtype=np.uint16)
    rhd_ch = np.vstack([dummy, adc])  # (11, 921600)

    n_blocks   = SAMPLES_PER_HOUR // SAMPLES_PER_BLOCK   # 7 200 exactly
    header_b   = _build_header(float(FS), N_RHD_CHANNELS)

    print(f"  [{stem}]  writing {n_blocks} blocks ...", end='\r', flush=True)

    with open(rhd_path, 'wb') as f:
        f.write(header_b)
        for blk in range(n_blocks):
            s0, s1 = blk * SAMPLES_PER_BLOCK, (blk + 1) * SAMPLES_PER_BLOCK
            # Timestamps: int32 LE, sample-absolute indices
            f.write(np.arange(s0, s1, dtype='<i4').tobytes())
            # Amplifier data: interleaved by sample then channel → (128, 11) → row-major
            f.write(rhd_ch[:, s0:s1].T.astype('<u2').tobytes())

    size_mb = rhd_path.stat().st_size / 1e6
    print(f"  [{stem}]  → {rhd_path.name}  ({size_mb:.1f} MB)      ")


# ── Threshold advisor ─────────────────────────────────────────────────────────
def print_threshold_info(dat_dir: Path) -> None:
    """
    Compute per-channel NEO statistics for one N and one S file to guide
    threshold selection in the pipeline GUI.
    """
    import textwrap

    pairs = [
        ("MSO1CDP6IEN.dat", "MSO1CDP6IES.dat", (13, 24)),   # seizure at min 13, sec 24
    ]

    print("\n── Threshold advisor ─────────────────────────────────────────────")
    print("NEO = x[n]² − x[n-1]·x[n+1]  computed on Intan ADC codes (centred at 32768)\n")

    for fn_n, fn_s, (sz_min, sz_sec) in pairs:
        for fn, label in [(fn_n, "NORMAL"), (fn_s, "SEIZURE")]:
            p = dat_dir / fn
            if not p.exists():
                print(f"  {fn} not found, skipping")
                continue
            raw  = np.fromfile(p, dtype=np.float32).reshape(N_DAT_CHANNELS, SAMPLES_PER_HOUR)
            adc  = _to_adc(raw).astype(np.int32) - ADC_OFFSET  # centred

            if label == "SEIZURE":
                onset = (sz_min * 60 + sz_sec) * FS
                window = slice(onset, onset + 60 * FS)
            else:
                window = slice(0, 60 * FS)

            print(f"  {label}  ({fn})  — first 60 s of window")
            print(f"  {'Ch':<4}  {'median NEO':>14}  {'p95 NEO':>14}  {'max NEO':>14}")
            for ch in range(N_DAT_CHANNELS):
                x = adc[ch, window]
                neo = (x[1:-1].astype(np.int64) ** 2
                       - x[:-2].astype(np.int64) * x[2:].astype(np.int64))
                neo = np.abs(neo)
                print(f"  {ch+1:<4}  {int(np.median(neo)):>14,}  "
                      f"{int(np.percentile(neo, 95)):>14,}  {int(neo.max()):>14,}")
            print()

    print(textwrap.dedent("""
    Recommended starting threshold (GUI → Threshold spinbox):
      • For seizure files:   try  50 000 000  (50M)  and tune down if you miss events
      • For normal files:    this value should produce few/no detections
      Default pipeline value of 150 000 is scaled for a different DAQ — it will
      trigger constantly on this data.  Set it to ~50M first, then adjust.
    """))


# ── CLI ───────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--file',   type=Path, help='Convert a single .dat file')
    ap.add_argument('--indir',  type=Path, default=Path('Data2'),
                    help='Input directory with .dat files (default: ./Data2)')
    ap.add_argument('--outdir', type=Path, default=None,
                    help='Output directory for .rhd files (default: same as --indir)')
    ap.add_argument('--threshold-info', action='store_true',
                    help='Print NEO statistics to guide threshold selection, then exit')
    args = ap.parse_args()

    dat_dir = args.indir.resolve()
    out_dir = (args.outdir or dat_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.threshold_info:
        print_threshold_info(dat_dir)
        return

    if args.file:
        files = [args.file.resolve()]
    else:
        files = sorted(dat_dir.glob('*.dat'))
        if not files:
            sys.exit(f"No .dat files found in {dat_dir}")

    print(f"Converting {len(files)} file(s)  →  {out_dir}\n")
    for dat_path in files:
        rhd_path = out_dir / (dat_path.stem + '.rhd')
        try:
            convert(dat_path, rhd_path)
        except Exception as e:
            print(f"  ERROR {dat_path.name}: {e}")

    print(f"\nDone.  {len(files)} file(s) written.")
    print("\nNext steps:")
    print("  1. Open the seizure pipeline GUI")
    print("  2. Load any .rhd file from the output directory")
    print("  3. Set Threshold to ~50 000 000 (50M) as a starting point")
    print("     (run --threshold-info to see per-file NEO ranges first)")
    print("  4. Run with FPGA or software fallback — both will work")


if __name__ == '__main__':
    main()
