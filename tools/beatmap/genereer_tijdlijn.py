#!/usr/bin/env python3
"""
Genereer een bommen-tijdlijn (global.bommenTijdlijn) uit een muziek-WAV.

Proof-of-concept. Draait ENKEL op de dev-PC (niet op de Pi). Leest een WAV met de
stdlib `wave`-module, doet spectral-flux onset-detectie (numpy-only, geen librosa),
en schrijft exact het schema dat de minigame "Bommen vermijden" al gebruikt:

    { "cmds": [ {"send":s,"paal":p,"laad":ms,"hold":ms,"pink":ms,"hz":n}, ... ],
      "expl": [ {"t":s,"palen":[...]}, ... ],
      "duur": seconden }

De aanpak spiegelt hoe Nic het handmatig deed: een muzikale hit = een EXPLOSIE-moment `t`;
de bom start `som/1000` s eerder zodat hij precies op die hit dooft (= scoort).

Gebruik:
    python genereer_tijdlijn.py <pad-naar.wav> [--dichtheid laag|midden|hoog] [--uit out/x.json]

De artistieke laag (welke hit dramatisch is, welk paalpatroon "goed voelt") blijft mensenwerk;
dit levert een strak-op-de-beat baseline om te verfijnen. Zie README.md.
"""
import argparse
import json
import os
import sys
import wave

import numpy as np

# ---- Bom-vorm (default, gelijk aan de handmatige tijdlijn) --------------------------------
LAAD_MS = 3830   # oplaad-ramp
HOLD_MS = 0      # vasthouden (voor groeps-bommen die op elkaar wachten)
PINK_MS = 3330   # knipperen op felst
PINK_HZ = 2
SOM_MS = LAAD_MS + HOLD_MS + PINK_MS      # totale looptijd; doven = scoren
SOM_S = SOM_MS / 1000.0                    # ~7.16 s

# ---- STFT-parameters -------------------------------------------------------------------
HOP = 512
WIN = 1024

# ---- Dichtheid-presets: (min gap tussen explosies in s, drempel-percentiel, groeps-percentiel)
DICHTHEID = {
    "laag":   (1.8, 82, 96),
    "midden": (1.2, 72, 93),
    "hoog":   (0.8, 60, 90),
}


def lees_mono(pad):
    """WAV -> (mono float32 in [-1,1], samplerate). Ondersteunt 16-bit PCM mono/stereo."""
    with wave.open(pad, "rb") as w:
        rate = w.getframerate()
        kanalen = w.getnchannels()
        breedte = w.getsampwidth()
        n = w.getnframes()
        ruw = w.readframes(n)
    if breedte != 2:
        sys.exit(f"Alleen 16-bit PCM ondersteund (dit bestand: {breedte*8}-bit).")
    data = np.frombuffer(ruw, dtype="<i2").astype(np.float32)
    if kanalen > 1:
        data = data.reshape(-1, kanalen).mean(axis=1)   # mixdown naar mono
    data /= 32768.0
    return data, rate


def onset_envelope(sig, rate):
    """Spectral-flux onset-envelope + frame-tijden + RMS-energie per frame."""
    venster = np.hanning(WIN).astype(np.float32)
    n_frames = 1 + (len(sig) - WIN) // HOP
    mag = np.empty((n_frames, WIN // 2 + 1), dtype=np.float32)
    rms = np.empty(n_frames, dtype=np.float32)
    for i in range(n_frames):
        seg = sig[i * HOP: i * HOP + WIN] * venster
        mag[i] = np.abs(np.fft.rfft(seg))
        rms[i] = np.sqrt(np.mean(seg * seg) + 1e-12)
    # spectral flux = som van positieve magnitude-verschillen tussen opeenvolgende frames
    diff = np.diff(mag, axis=0)
    flux = np.maximum(diff, 0.0).sum(axis=1)
    flux = np.concatenate([[0.0], flux])
    # normaliseer + lichte smoothing
    if flux.max() > 0:
        flux /= flux.max()
    k = np.ones(3, dtype=np.float32) / 3.0
    flux = np.convolve(flux, k, mode="same")
    tijden = np.arange(n_frames) * HOP / rate
    return flux, tijden, rms


def pick_onsets(flux, tijden, rate, min_gap_s):
    """Adaptieve piek-detectie op de flux-envelope. Geeft (tijd, sterkte)-lijst terug."""
    fps = rate / HOP
    W = int(0.4 * fps)                     # venster voor lokale drempel (~0.4 s)
    onsets = []
    laatste_t = -1e9
    for i in range(2, len(flux) - 2):
        lo, hi = max(0, i - W), min(len(flux), i + W)
        drempel = flux[lo:hi].mean() + 0.6 * flux[lo:hi].std()
        is_piek = flux[i] >= flux[i - 1] and flux[i] > flux[i + 1] and flux[i] >= flux[i - 2] and flux[i] > flux[i + 2]
        if is_piek and flux[i] > drempel and (tijden[i] - laatste_t) >= min_gap_s:
            onsets.append((float(tijden[i]), float(flux[i])))
            laatste_t = tijden[i]
    return onsets


def schat_tempo(flux, rate):
    """Grove BPM-schatting via autocorrelatie van de onset-envelope (70-180 BPM)."""
    fps = rate / HOP
    x = flux - flux.mean()
    ac = np.correlate(x, x, mode="full")[len(x) - 1:]
    lo = int(fps * 60 / 180)     # kortste lag (180 BPM)
    hi = int(fps * 60 / 70)      # langste lag (70 BPM)
    if hi >= len(ac):
        return None
    lag = lo + int(np.argmax(ac[lo:hi]))
    return round(60.0 * fps / lag, 1)


def bouw_tijdlijn(onsets, duur, min_gap_s, drempel_pct, groep_pct):
    """Kies uit de onsets de EXPLOSIE-momenten en bouw cmds/expl.

    - onset-sterkte boven `drempel_pct`-percentiel  -> een explosie
    - onset-sterkte boven `groep_pct`-percentiel     -> groeps-explosie (3 palen)
    - paal roteert rond de ring (1..24) voor spreiding
    """
    if not onsets:
        return {"cmds": [], "expl": [], "duur": round(duur, 2)}
    sterktes = np.array([s for _, s in onsets])
    drempel = np.percentile(sterktes, drempel_pct)
    groep_drempel = np.percentile(sterktes, groep_pct)

    cmds, expl = [], []
    paal_ptr = 0
    laatste_t = -1e9
    for t, s in onsets:
        if s < drempel:
            continue
        send = t - SOM_S
        if send < 0.3:                      # bom zou vóór muziekstart moeten opladen -> overslaan
            continue
        if (t - laatste_t) < min_gap_s:
            continue
        laatste_t = t
        # groeps-explosie bij een zeer sterke hit: 3 naburige palen op de ring
        if s >= groep_drempel:
            palen = [((paal_ptr + k) % 24) + 1 for k in (0, 8, 16)]  # gespreid rond de 24-ring
        else:
            palen = [(paal_ptr % 24) + 1]
        for p in palen:
            cmds.append({"send": round(send, 2), "paal": p,
                         "laad": LAAD_MS, "hold": HOLD_MS, "pink": PINK_MS, "hz": PINK_HZ})
        expl.append({"t": round(t, 2), "palen": palen})
        paal_ptr = (paal_ptr + 5) % 24      # stap 5 -> mooie spreiding rond de ring
    return {"cmds": cmds, "expl": expl, "duur": round(duur, 2)}


def stille_vensters(rms, tijden, min_duur_s=6.0):
    """Rapporteer aaneengesloten rustige stukken (kandidaat voor sfeer-golven, niet in de JSON)."""
    drempel = np.percentile(rms, 35)
    vensters, start = [], None
    for i, e in enumerate(rms):
        if e < drempel and start is None:
            start = tijden[i]
        elif e >= drempel and start is not None:
            if tijden[i] - start >= min_duur_s:
                vensters.append((round(start, 1), round(float(tijden[i]), 1)))
            start = None
    return vensters


def main():
    ap = argparse.ArgumentParser(description="Genereer bommen-tijdlijn uit een WAV.")
    ap.add_argument("wav", help="pad naar de muziek-WAV")
    ap.add_argument("--dichtheid", choices=list(DICHTHEID), default="midden")
    ap.add_argument("--uit", default=None, help="output-JSON (default out/<naam>_generated.json)")
    args = ap.parse_args()

    if not os.path.isfile(args.wav):
        sys.exit(f"WAV niet gevonden: {args.wav}")

    sig, rate = lees_mono(args.wav)
    duur = len(sig) / rate
    flux, tijden, rms = onset_envelope(sig, rate)
    min_gap, drempel_pct, groep_pct = DICHTHEID[args.dichtheid]
    onsets = pick_onsets(flux, tijden, rate, min_gap_s=0.12)   # ruwe onsets dicht; explosie-gap komt later
    tempo = schat_tempo(flux, rate)
    tijdlijn = bouw_tijdlijn(onsets, duur, min_gap, drempel_pct, groep_pct)
    stiltes = stille_vensters(rms, tijden)

    naam = os.path.splitext(os.path.basename(args.wav))[0]
    uit = args.uit or os.path.join(os.path.dirname(__file__), "out", f"{naam}_generated.json")
    os.makedirs(os.path.dirname(uit), exist_ok=True)
    with open(uit, "w", encoding="utf-8") as f:
        json.dump(tijdlijn, f, ensure_ascii=False, separators=(",", ":"))

    print(f"WAV        : {args.wav}")
    print(f"Duur       : {duur:.2f} s   |  {rate} Hz  |  ruwe onsets: {len(onsets)}")
    print(f"Tempo      : ~{tempo} BPM (grove schatting)" if tempo else "Tempo      : onbekend")
    print(f"Dichtheid  : {args.dichtheid}")
    print(f"Gegenereerd: {len(tijdlijn['expl'])} explosies, {len(tijdlijn['cmds'])} bom-cues")
    if stiltes:
        print(f"Rustige stukken (kandidaat sfeer-golf): "
              + ", ".join(f"{a}-{b}s" for a, b in stiltes))
    print(f"Geschreven : {uit}")


if __name__ == "__main__":
    main()
