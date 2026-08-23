#!/usr/bin/env python3
"""Compare Engine A's renders against SuperCollider's, voice by voice.

The claim 8W8 makes about Engine A is that it is a TRANSCRIPTION of sc808,
not an impression of it. This is what makes that claim checkable: render each
voice in scsynth, render it here, and measure how much is left when you
subtract one from the other.

Two numbers per voice:

  null    the residual after subtraction, in dB relative to the reference's
          own peak. This is the real result. Below about -60 dB the two are
          the same signal for any purpose; above -20 dB something is wrong.

  gain    the scale factor that minimises the residual (least squares). It
          should be 1.0. It is reported separately because a pure level
          difference is a different bug from a shape difference, and the null
          figure alone cannot tell them apart.

The noise-driven voices (snare, clap, maracas, rim shot) will NOT null: their
noise comes from a different RNG stream than scsynth's, so subtracting them
measures the noise rather than the transcription. Those are marked `NOISE` and
get two different numbers instead — worst-case 1/6-octave band error and
worst-case envelope error, both in dB. See noise_match().
"""
import sys, os, struct, math, wave

try:
    import numpy as np
except ImportError:
    sys.exit("numpy required")

# Voices whose output is dominated by an unseeded noise source, so a sample
# level null is not available and not meaningful.
NOISE_VOICES = {"snare", "clap", "maracas", "rimshot"}

# SC's Pan2 at position 0 puts sqrt(1/2) of the mono signal in each channel.
# Our voices are pre-Pan2 mono, so the reference is scaled back up.
PAN2_CENTRE = math.sqrt(0.5)


def read_wav(path):
    """Read a WAV as float64 mono. Handles the float32 that both sides emit."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    pos, fmt, ch, bits, samples = 12, None, 1, 32, None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt, ch = struct.unpack("<HH", body[:4])
            bits = struct.unpack("<H", body[14:16])[0]
        elif cid == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    if samples is None or fmt is None:
        raise ValueError(f"{path}: missing fmt or data chunk")
    if fmt == 3 and bits == 32:
        a = np.frombuffer(samples, dtype="<f4").astype(np.float64)
    elif fmt == 1 and bits == 16:
        a = np.frombuffer(samples, dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise ValueError(f"{path}: unsupported format {fmt}/{bits}")
    if ch > 1:
        a = a.reshape(-1, ch)[:, 0]      # left channel; pan is centred
    return a


def noise_match(a, b, sr=44100.0):
    """How closely two NOISE-driven voices agree, as two dB figures.

    A voice built on WhiteNoise cannot null: scsynth's RNG stream and ours are
    different streams, so the output is a different noise of the same colour.
    Subtracting them measures the noise, not the transcription, which is why
    the earlier "-20 dB spectral" figure meant nothing at all.

    What CAN be compared is everything the noise is shaped by, and that is the
    whole of the voice apart from the sample values:

      band    largest disagreement between 1/6-octave band energies, in dB.
              This is the filter chain — centre frequencies, Q, gains. Two
              draws of the same noise through the same filters land within
              roughly a dB of each other in a band this wide; several dB means
              the filters differ.

      env     largest disagreement between short-window RMS envelopes, in dB,
              over the part of the note that is above -40 dB. This is the
              envelope generators and their curves, which ARE deterministic.

    Both are worst-case, not averages: an error in one band or one moment is
    exactly what we want to catch.
    """
    # Confine the analysis to the part of the note that is actually sounding,
    # and average several overlapping windows across it (Welch). Both matter:
    # a 70 ms maracas inside a 740 ms FFT is mostly silence, and a single
    # window of a random signal has enough variance on its own to report 8 dB
    # of "error" where there is none.
    def active(x):
        env = np.abs(x)
        loud = np.where(env > env.max() * 1e-3)[0]
        return (0, len(x)) if len(loud) == 0 else (loud[0], loud[-1] + 1)

    lo, hi = active(a)
    span = hi - lo
    nfft = 4096
    while nfft > 256 and nfft > span // 4:
        nfft >>= 1
    hop = max(nfft // 2, 1)
    starts = list(range(lo, max(lo + 1, hi - nfft + 1), hop))[:64]
    if not starts:
        starts = [lo]

    win = np.hanning(nfft)
    PA = np.zeros(nfft // 2 + 1)
    PB = np.zeros(nfft // 2 + 1)
    for st in starts:
        sa = a[st:st + nfft]
        sb = b[st:st + nfft]
        if len(sa) < nfft:
            sa = np.pad(sa, (0, nfft - len(sa)))
        if len(sb) < nfft:
            sb = np.pad(sb, (0, nfft - len(sb)))
        PA += np.abs(np.fft.rfft(sa * win)) ** 2
        PB += np.abs(np.fft.rfft(sb * win)) ** 2
    PA /= len(starts)
    PB /= len(starts)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)

    # 1/6-octave band energies. Wide enough that two draws of the same noise
    # through the same filter agree closely, narrow enough to catch a filter
    # whose corner or Q is wrong.
    edges, f = [], 30.0
    while f < sr / 2:
        edges.append(f)
        f *= 2 ** (1 / 6)

    bands, peak_energy = [], 0.0
    for blo, bhi in zip(edges[:-1], edges[1:]):
        m = (freqs >= blo) & (freqs < bhi)
        if m.any():
            ea, eb = float(PA[m].mean()), float(PB[m].mean())
            bands.append((ea, eb))
            peak_energy = max(peak_energy, ea)

    worst_band = 0.0
    for ea, eb in bands:
        # Only bands within 40 dB of the loudest: below that the reference is
        # its own noise floor and the comparison is meaningless.
        if ea < peak_energy * 1e-4:
            continue
        worst_band = max(worst_band,
                         abs(10 * math.log10(max(ea, 1e-30) / max(eb, 1e-30))))

    # RMS envelope in 5 ms windows. This part IS deterministic — the envelope
    # generators and their curves — so it should agree tightly.
    w = int(sr * 0.005)
    k = min(len(a), len(b)) // w
    ra = np.sqrt(np.mean(a[:k * w].reshape(k, w) ** 2, axis=1))
    rb = np.sqrt(np.mean(b[:k * w].reshape(k, w) ** 2, axis=1))
    live = ra > ra.max() * 0.01          # above -40 dB of the peak
    worst_env = 0.0
    if live.any():
        worst_env = float(np.max(np.abs(
            20 * np.log10(np.maximum(ra[live], 1e-12)
                          / np.maximum(rb[live], 1e-12)))))
    return worst_band, worst_env


def best_lag(ref, mine, span=256):
    """Integer sample lag of `mine` relative to `ref`, by correlation.

    A constant offset makes every voice fail by a similar amount and looks
    exactly like eighteen unrelated filter bugs, so it is measured rather
    than assumed away. Reported, never silently corrected: a lag that is not
    zero is itself a finding.
    """
    n = min(len(ref), len(mine))
    a, b = ref[:n], mine[:n]
    # Minimising the residual, NOT maximising correlation: for a signal that
    # sits near a constant (an envelope at full level, say) the correlation
    # surface is almost flat and its argmax is noise — that reported a lag of
    # 34 on an envelope whose true offset was 1.
    best, beste = 0, 1e30
    for lag in range(-span, span + 1):
        if lag >= 0:
            x, y = a[:n - lag], b[lag:]
        else:
            x, y = a[-lag:], b[:n + lag]
        if len(x) < 1000:
            continue
        e = float(np.mean((x - y) ** 2))
        # Strictly better, or equally good but closer to zero: a periodic
        # signal nulls exactly at every multiple of its period, and reporting
        # "lag -172" for a square wave that actually lines up at 0 is just
        # noise in the report.
        if e < beste * (1 - 1e-12) or (e <= beste * (1 + 1e-12) and abs(lag) < abs(best)):
            beste, best = e, lag
    return best


def compare(name, ref_path, mine_path):
    ref = read_wav(ref_path) / PAN2_CENTRE
    mine = read_wav(mine_path)
    n = min(len(ref), len(mine))
    ref, mine = ref[:n], mine[:n]

    peak = np.abs(ref).max()
    if peak < 1e-9:
        return name, "SILENT", 0.0, 0.0, 0.0, 0

    if name in NOISE_VOICES:
        band, env = noise_match(ref, mine)
        return name, "NOISE", band, env, peak, 0

    lag = best_lag(ref, mine)
    if lag > 0:
        a, b = ref[:n - lag], mine[lag:]
    elif lag < 0:
        a, b = ref[-lag:], mine[:n + lag]
    else:
        a, b = ref, mine

    # Least-squares gain: the scale that best explains mine as a copy of ref.
    denom = float(np.dot(a, a))
    gain = float(np.dot(a, b)) / denom if denom > 0 else 0.0
    resid = b - a
    null = 20 * math.log10(max(np.sqrt(np.mean(resid ** 2)), 1e-12)
                           / max(peak, 1e-12))
    return name, "NULL", null, gain, peak, lag


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    # --mono: the probe renders have no Pan2 and no noise sources, so neither
    # the sqrt(1/2) nor the spectral fallback applies.
    mono = "--mono" in sys.argv
    global PAN2_CENTRE, NOISE_VOICES
    if mono:
        PAN2_CENTRE = 1.0
        NOISE_VOICES = set()
    ref_dir, mine_dir = args[0], args[1]
    names = sorted(f[:-4] for f in os.listdir(ref_dir) if f.endswith(".wav"))

    print(f"{'voice':<14} {'type':<6} {'null dB':>9} {'band dB':>8} "
          f"{'env dB':>7} {'gain':>8} {'lag':>4} {'ref peak':>9}")
    print("-" * 72)
    worst_null = -999.0
    worst_band = worst_env = 0.0
    for name in names:
        mine_path = os.path.join(mine_dir, name + ".wav")
        if not os.path.exists(mine_path):
            print(f"{name:<14} {'MISSING':<9}")
            continue
        n, kind, val, gain, peak, lag = compare(
            name, os.path.join(ref_dir, name + ".wav"), mine_path)
        if kind == "NULL":
            print(f"{n:<14} {kind:<6} {val:9.1f} {'':>8} {'':>7} "
                  f"{gain:8.4f} {lag:4d} {peak:9.4f}")
            worst_null = max(worst_null, val)
        elif kind == "NOISE":
            # val = worst band error, gain = worst envelope error
            print(f"{n:<14} {kind:<6} {'':>9} {val:8.2f} {gain:7.2f} "
                  f"{'':>8} {'':>4} {peak:9.4f}")
            worst_band = max(worst_band, val)
            worst_env = max(worst_env, gain)
        else:
            print(f"{n:<14} {kind:<6}")

    print("-" * 72)
    print(f"worst null (deterministic voices) {worst_null:8.1f} dB")
    print(f"worst band error (noise voices)   {worst_band:8.2f} dB")
    print(f"worst envelope error (noise)      {worst_env:8.2f} dB")


if __name__ == "__main__":
    main()
