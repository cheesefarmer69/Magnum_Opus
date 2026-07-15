#!/usr/bin/env python3
"""
Genereer een gechoreografeerde bommen-tijdlijn (global.bommenTijdlijn) uit een MIDI-bestand.

Waar `genereer_tijdlijn.py` een WAV *analyseert* (onsets raden), leest deze generator de MIDI
**noot-exact** — veel strakker. Bedoeld voor de AoT-hardstyle-track "YouSeeBIGGIRL", maar de
constanten bovenaan zijn eenvoudig aan te passen voor een ander nummer.

Choreografie:
  - de drijvende beat (BEAT_NOTE) -> losse bommen, uitgedund met een sectie-geschaalde min-gap,
    palen roterend rond de 24-ring zodat spelers moeten blijven bewegen;
  - de zeldzame accenten (ACCENT_NOTE) -> grote GROEPS-explosies (meerdere palen tegelijk);
  - rustige secties (SFEER) -> sfeer-golven (actie 16, geen scoring) i.p.v. bommen.

Output = het bestaande bommenTijdlijn-schema (cmds/expl/duur), uitgebreid met `sfeer:[{van,tot}]`.

Gebruik:  python tools/beatmap/genereer_uit_midi.py [pad.mid] [--uit out/x.json]
"""
import argparse
import json
import os
import struct
import sys
import wave

HIER = os.path.dirname(__file__)
ROOT = os.path.abspath(os.path.join(HIER, "..", ".."))

# ---- Bron (defaults voor AoT — YouSeeBIGGIRL) --------------------------------------------
STD_MID = os.path.join(ROOT, "pi/audio-player/_inbox",
                       "AOT_Hardstyle_YouSeeBIGGIRL_-_Hiroyuki_Sawano_KLICKAUD.mid")
STD_WAV = os.path.join(ROOT, "pi/audio-player/_inbox",
                       "AOT_Hardstyle_YouSeeBIGGIRL_-_Hiroyuki_Sawano_KLICKAUD.wav")

DRUMS_TRACK = "Drums"     # tracknaam met de beat
BEAT_NOTE = 38            # drijvende dodge-beat (289 hits)
ACCENT_NOTE = 36          # grote accent-momenten (10 hits)

# ---- Bom-vormen (punchy, hardstyle) ------------------------------------------------------
BOM = dict(laad=1800, hold=0, pink=1200, hz=2)     # som 3.0 s
ACCENT = dict(laad=2200, hold=0, pink=1400, hz=3)  # som 3.6 s, iets dramatischer + sneller pinken
SOM_S = (BOM["laad"] + BOM["hold"] + BOM["pink"]) / 1000.0
ACC_SOM_S = (ACCENT["laad"] + ACCENT["hold"] + ACCENT["pink"]) / 1000.0

# ---- Secties: (van_s, tot_s, min_gap_s tussen losse bommen). Volgt de intensiteitsboog. ----
SECTIES = [
    (0.0, 20.0, 2.4),    # intro (rustig)
    (20.0, 40.0, 1.8),   # opbouw
    (40.0, 70.0, 1.25),  # drop (dicht)
    (80.0, 130.0, 1.4),  # finale-climax (70-80 is breakdown -> sfeer, geen bommen)
]
SFEER = [(70.0, 82.0)]   # breakdown / strings-swell: sfeer-golven i.p.v. bommen
ACCENT_MIN_SEND = 0.3    # bom die vóór de muziekstart zou moeten opladen -> overslaan
DEDUP_S = 0.35           # losse bom binnen deze afstand van een accent -> laten vallen


def parse_midi(pad):
    """Minimale MIDI-parser -> {tracknaam: [(abs_tick, note, vel)]}, plus division."""
    data = open(pad, "rb").read()
    pos = 0

    def rd(n):
        nonlocal pos
        b = data[pos:pos + n]
        pos += n
        return b

    def varlen():
        nonlocal pos
        v = 0
        while True:
            c = data[pos]
            pos += 1
            v = (v << 7) | (c & 0x7f)
            if not c & 0x80:
                return v

    assert rd(4) == b"MThd", "geen MThd-header"
    hlen = struct.unpack(">I", rd(4))[0]
    _fmt, ntrk, div = struct.unpack(">HHH", rd(hlen))
    tracks = {}
    for _ in range(ntrk):
        assert rd(4) == b"MTrk", "geen MTrk"
        ln = struct.unpack(">I", rd(4))[0]
        end = pos + ln
        abs_t, status, name, evs = 0, 0, "", []
        while pos < end:
            abs_t += varlen()
            b = data[pos]
            if b & 0x80:
                status = b
                pos += 1
            else:
                b = status
            if b == 0xFF:                       # meta
                meta = data[pos]; pos += 1
                l = varlen()
                payload = data[pos:pos + l]; pos += l
                if meta == 0x03:
                    name = payload.decode("latin1", "replace")
            elif b in (0xF0, 0xF7):             # sysex
                pos += varlen()
            else:
                hi = b & 0xF0
                if hi in (0xC0, 0xD0):
                    pos += 1
                else:
                    d1, d2 = data[pos], data[pos + 1]; pos += 2
                    if hi == 0x90 and d2 > 0:
                        evs.append((abs_t, d1, d2))
        tracks.setdefault(name, []).extend(evs)
        pos = end
    return tracks, div


def note_times(tracks, div, track, note, bpm=120.0):
    """Absolute tijden (s) van note-on `note` in `track` (constant tempo)."""
    sec_per_tick = (60.0 / bpm) / div
    evs = tracks.get(track, [])
    return sorted(t * sec_per_tick for t, n, v in evs if n == note)


def in_sfeer(t):
    return any(v <= t < w for v, w in SFEER)


def min_gap(t):
    for v, w, g in SECTIES:
        if v <= t < w:
            return g
    return None   # buiten elke sectie (bv. breakdown) -> geen losse bommen


def dichtstbij(t, lijst):
    return min((abs(t - x) for x in lijst), default=1e9)


def bouw(beat, accent, duur):
    """Bouw cmds + expl uit de beat- (losse bommen) en accent-noten (groeps-explosies)."""
    expl = []

    # 1) accenten -> groeps-explosies (highlights), 4 palen gespreid rond de ring
    base = 0
    for t in accent:
        if t - ACC_SOM_S < ACCENT_MIN_SEND or in_sfeer(t):
            continue
        palen = sorted(((base + k) % 24) + 1 for k in (0, 6, 12, 18))
        expl.append({"t": round(t, 2), "palen": palen, "_acc": True})
        base = (base + 7) % 24
    accent_t = [e["t"] for e in expl]

    # 2) beat -> losse bommen, sectie-geschaalde min-gap, palen roterend
    ptr = 0
    laatste = -1e9
    for t in beat:
        g = min_gap(t)
        if g is None or in_sfeer(t):
            continue
        if t - SOM_S < ACCENT_MIN_SEND:
            continue
        if (t - laatste) < g:
            continue
        if dichtstbij(t, accent_t) < DEDUP_S:   # accent wint van een losse bom op ~hetzelfde moment
            continue
        laatste = t
        expl.append({"t": round(t, 2), "palen": [(ptr % 24) + 1], "_acc": False})
        ptr = (ptr + 5) % 24

    expl.sort(key=lambda e: e["t"])

    # 3) cmds afleiden (send terugrekenen zodat doven == t)
    cmds = []
    for e in expl:
        vorm, som = (ACCENT, ACC_SOM_S) if e.pop("_acc") else (BOM, SOM_S)
        send = round(e["t"] - som, 2)
        for p in e["palen"]:
            cmds.append({"send": send, "paal": p, "laad": vorm["laad"],
                         "hold": vorm["hold"], "pink": vorm["pink"], "hz": vorm["hz"]})
    cmds.sort(key=lambda c: (c["send"], c["paal"]))
    sfeer = [{"van": round(v, 2), "tot": round(w, 2)} for v, w in SFEER]
    return {"cmds": cmds, "expl": expl, "sfeer": sfeer, "duur": round(duur, 2)}


def wav_duur(pad):
    try:
        with wave.open(pad, "rb") as w:
            return w.getnframes() / w.getframerate()
    except Exception:
        return 122.1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mid", nargs="?", default=STD_MID)
    ap.add_argument("--wav", default=STD_WAV)
    ap.add_argument("--uit", default=os.path.join(HIER, "out", "aot_youseebiggirl.json"))
    args = ap.parse_args()

    if not os.path.isfile(args.mid):
        sys.exit(f"MIDI niet gevonden: {args.mid}")
    tracks, div = parse_midi(args.mid)
    beat = note_times(tracks, div, DRUMS_TRACK, BEAT_NOTE)
    accent = note_times(tracks, div, DRUMS_TRACK, ACCENT_NOTE)
    duur = wav_duur(args.wav)
    tl = bouw(beat, accent, duur)

    os.makedirs(os.path.dirname(args.uit), exist_ok=True)
    with open(args.uit, "w", encoding="utf-8") as f:
        json.dump(tl, f, ensure_ascii=False, separators=(",", ":"))

    groepen = [e for e in tl["expl"] if len(e["palen"]) > 1]
    print(f"MIDI      : {os.path.basename(args.mid)}")
    print(f"Beat/acc  : {len(beat)} beat-noten (n{BEAT_NOTE}), {len(accent)} accenten (n{ACCENT_NOTE})")
    print(f"Duur      : {duur:.1f} s")
    print(f"Resultaat : {len(tl['expl'])} explosies ({len(groepen)} groeps-accenten), "
          f"{len(tl['cmds'])} bom-cues, sfeer {tl['sfeer']}")
    # dichtheid per 10 s
    print("Dichtheid per 10s:")
    for lo in range(0, int(duur) + 10, 10):
        n = sum(1 for e in tl["expl"] if lo <= e["t"] < lo + 10)
        acc = sum(1 for e in groepen if lo <= e["t"] < lo + 10)
        print(f"  {lo:3d}-{lo+10:3d}s  {n:2d} expl {'(acc '+str(acc)+')' if acc else '':7s} {'#'*n}")
    print(f"Geschreven: {args.uit}")


if __name__ == "__main__":
    main()
