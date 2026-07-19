#!/usr/bin/env python3
"""
Genereer een gechoreografeerde bommen-tijdlijn (global.bommenTijdlijn) uit een MIDI-bestand.

Waar `genereer_tijdlijn.py` een WAV *analyseert* (onsets raden), leest deze generator de MIDI
**noot-exact** — veel strakker. Bedoeld voor de AoT-hardstyle-track "YouSeeBIGGIRL", maar de
constanten bovenaan zijn eenvoudig aan te passen voor een ander nummer.

Choreografie v2 (juli 2026 — "maak het intens"): vijf secties die de intensiteitsboog van het
nummer volgen, met COMBINATIES van bom-vormen en SEQUENTIES — alles verankerd op MIDI-noten:

  intro     0-20   ademruimte: trage SLOW-dread-bommen (7,2 s opladen — dreiging, geen paniek)
  opbouw    20-40  STD-bommen op de beat + de eerste CHASE-sequenties (3 palen die na elkaar
                   beginnen te laden maar SAMEN pinken en SAMEN ontploffen op één beat)
  drop      40-70  dichte PUNCH-bommen (1,8 s — snel!), QUAD-ring-blasts op de accenten,
                   MIRROR-paren (p en p+12) en twee WALLs van 5 aaneengesloten palen
  breakdown 70-82  sfeer-golven (geen scoring) + twee SLOW-dreads die de stilte OVERBRUGGEN
                   en pas op de eerste beats van de finale ontploffen
  finale    82-113 climax: PUNCH-beats, CHASE-4-sequenties, QUAD-accenten en als slotstuk een
                   MEGA-WALL van 12 palen op de laatste grote hit
  outro     113+   uitademen (niets)

Zelfde-paal-conflicten worden automatisch weggedraaid (een paal krijgt geen nieuwe cue zolang
zijn vorige bom nog loopt), zodat de firmware-bom-queue (diepte 4, max ~2 pending) nooit klem
loopt en replace-on-fire geen choreografie opeet.

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
STD_WAV = os.path.join(ROOT, "pi/audio-player/audio/muziek", "aot_youseebiggirl.wav")

DRUMS_TRACK = "Drums"     # tracknaam met de beat
BEAT_NOTE = 38            # drijvende dodge-beat (289 hits)
ACCENT_NOTE = 36          # grote accent-momenten (10 hits)

# ---- Bom-vormen ---------------------------------------------------------------------------
SLOW   = dict(laad=3830, hold=0, pink=3330, hz=2)   # som 7,16 s — trage dread (intro/breakdown)
STD    = dict(laad=1800, hold=0, pink=1200, hz=2)   # som 3,0 s  — allrounder (opbouw)
PUNCH  = dict(laad=1000, hold=0, pink=800,  hz=4)   # som 1,8 s  — snel en fel (drop/finale)
ACCENT = dict(laad=2200, hold=0, pink=1400, hz=3)   # som 3,6 s  — quad-ring-blast

def som_s(v):
    return (v["laad"] + v["hold"] + v["pink"]) / 1000.0

# ---- Secties + specials -------------------------------------------------------------------
SFEER = [(70.0, 82.0)]      # breakdown / strings-swell: sfeer-golven i.p.v. bommen
INTRO, OPBOUW, DROP, FINALE = (0.0, 20.0), (20.0, 40.0), (40.0, 70.0), (82.0, 113.0)
MIN_GAP = {"intro": 7.0, "opbouw": 1.6, "drop": 0.9, "finale": 1.0}
ACCENT_MIN_SEND = 0.3       # bom die vóór de muziekstart zou moeten opladen -> overslaan
DEDUP_S = 0.35              # losse bom te dicht bij een accent/special -> laten vallen


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


def sectie(t):
    if INTRO[0] <= t < INTRO[1]:
        return "intro"
    if OPBOUW[0] <= t < OPBOUW[1]:
        return "opbouw"
    if DROP[0] <= t < DROP[1]:
        return "drop"
    if FINALE[0] <= t < FINALE[1]:
        return "finale"
    return None


def dichtstbij(t, lijst):
    return min((abs(t - x) for x in lijst), default=1e9)


def beat_bij(beat, doel, marge=1.2):
    """De beat-noot het dichtst bij `doel` (of None) — specials landen zo altijd op de beat."""
    kandidaat = min(beat, key=lambda b: abs(b - doel), default=None)
    return kandidaat if kandidaat is not None and abs(kandidaat - doel) <= marge else None


class Plan:
    """Verzamelt cues + explosies en bewaakt de zelfde-paal-regel: een paal krijgt geen nieuwe
    cue die OVERLAPT met een al geplande bom op die paal (interval [send, expl] + marge) —
    tussen twee bommen door is een paal gewoon vrij. Bij conflict schuiven we door naar de
    volgende vrije paal op de ring."""

    MARGE = 0.30   # s ademruimte tussen twee bommen op dezelfde paal

    def __init__(self):
        self.cmds = []
        self.expl = []          # {t, palen}
        self.per_paal = {}      # paal -> [(send, expl_t), ...] geplande intervallen

    def vrij(self, paal, send, expl_t):
        for s, e in self.per_paal.get(paal, []):
            if not (expl_t + self.MARGE <= s or e + self.MARGE <= send):
                return False
        return True

    def kies_vrij(self, paal, send, expl_t):
        for k in range(24):
            p = ((paal - 1 + k) % 24) + 1
            if self.vrij(p, send, expl_t):
                return p
        return None

    def bom(self, expl_t, paal, vorm, hold_extra=0, schuif=True):
        """Plan één bom die exact op `expl_t` dooft. Geeft de gekozen paal terug (of None)."""
        som = (vorm["laad"] + vorm["hold"] + hold_extra + vorm["pink"]) / 1000.0
        send = round(expl_t - som, 2)
        if send < ACCENT_MIN_SEND:
            return None
        p = self.kies_vrij(paal, send, expl_t) if schuif else (paal if self.vrij(paal, send, expl_t) else None)
        if p is None:
            return None
        self.cmds.append({"send": send, "paal": p, "laad": vorm["laad"],
                          "hold": vorm["hold"] + hold_extra, "pink": vorm["pink"], "hz": vorm["hz"]})
        self.per_paal.setdefault(p, []).append((send, expl_t))
        return p

    def groep(self, expl_t, palen, vorm, stagger=0.0):
        """Plan een groep die SAMEN op `expl_t` dooft; leden starten `stagger` s na elkaar
        (chase-gevoel) via oplopende holds — allen pinken en doven tegelijk."""
        gekozen = []
        n = len(palen)
        for i, paal in enumerate(palen):
            # lid i start i*stagger later -> zijn hold is (n-1-i)*stagger korter dan de eerste
            hold_extra = int(round((n - 1 - i) * stagger * 1000))
            p = self.bom(expl_t, paal, vorm, hold_extra=hold_extra)
            if p is not None:
                gekozen.append(p)
        if gekozen:
            self.expl.append({"t": round(expl_t, 2), "palen": sorted(set(gekozen))})
        return gekozen

    def solo(self, expl_t, paal, vorm):
        p = self.bom(expl_t, paal, vorm)
        if p is not None:
            self.expl.append({"t": round(expl_t, 2), "palen": [p]})
        return p


def bouw(beat, accent, duur):
    plan = Plan()
    specials_t = []

    # ---- 1) QUAD-accenten: 4 palen gespreid rond de ring, op elke accent-noot --------------
    base = 0
    for t in accent:
        if in_sfeer(t) or t - som_s(ACCENT) < ACCENT_MIN_SEND:
            continue
        palen = [((base + k) % 24) + 1 for k in (0, 6, 12, 18)]
        plan.groep(round(t, 2), palen, ACCENT)
        specials_t.append(t)
        base = (base + 7) % 24

    # ---- 2) CHASES: opbouw 3-leden (~elke 10 s), finale 4-leden (~elke 8 s) ----------------
    chase_base = 2
    for doel in [24.0, 33.0]:
        t = beat_bij(beat, doel)
        if t and not in_sfeer(t):
            palen = [((chase_base + k * 2) % 24) + 1 for k in range(3)]
            plan.groep(round(t, 2), palen, STD, stagger=0.22)
            specials_t.append(t)
            chase_base = (chase_base + 9) % 24
    for doel in [86.0, 94.0, 102.0]:
        t = beat_bij(beat, doel)
        if t and not in_sfeer(t):
            palen = [((chase_base + k * 2) % 24) + 1 for k in range(4)]
            plan.groep(round(t, 2), palen, PUNCH, stagger=0.22)
            specials_t.append(t)
            chase_base = (chase_base + 11) % 24

    # ---- 3) WALLS: drop 2x 5 aaneengesloten palen; finale-slot een MEGA-WALL van 12 --------
    wall_vorm = dict(laad=400, hold=0, pink=1600, hz=3)
    for doel, breedte in [(48.0, 5), (61.0, 5)]:
        t = beat_bij(beat, doel)
        if t:
            s0 = (int(doel * 7) % 24)
            palen = [((s0 + k) % 24) + 1 for k in range(breedte)]
            plan.groep(round(t, 2), palen, wall_vorm, stagger=0.15)
            specials_t.append(t)
    slot = beat_bij(beat, 110.0, marge=3.0)
    if slot:
        mega = dict(laad=500, hold=0, pink=2000, hz=3)
        palen = [((3 + k) % 24) + 1 for k in range(12)]
        plan.groep(round(slot, 2), palen, mega, stagger=0.12)
        specials_t.append(slot)

    # ---- 4) MIRROR-paren in de drop (p en p+12 tegelijk) -----------------------------------
    mirror_base = 5
    for doel in [44.0, 52.0, 57.0, 66.0]:
        t = beat_bij(beat, doel)
        if t and not in_sfeer(t):
            palen = [((mirror_base) % 24) + 1, ((mirror_base + 12) % 24) + 1]
            plan.groep(round(t, 2), palen, PUNCH)
            specials_t.append(t)
            mirror_base = (mirror_base + 5) % 24

    # ---- 5) BREAKDOWN-dreads: 2 SLOW-bommen die de stilte overbruggen ----------------------
    for doel, paal in [(82.5, 7), (83.5, 19)]:
        t = beat_bij(beat, doel)
        if t:
            plan.solo(round(t, 2), paal, SLOW)
            specials_t.append(t)

    # ---- 6) losse beat-bommen per sectie (vullen rond de specials) -------------------------
    ptr = 0
    laatste = -1e9
    for t in beat:
        sec = sectie(t)
        if sec is None or in_sfeer(t):
            continue
        vorm = {"intro": SLOW, "opbouw": STD, "drop": PUNCH, "finale": PUNCH}[sec]
        if t - som_s(vorm) < ACCENT_MIN_SEND:
            continue
        if (t - laatste) < MIN_GAP[sec]:
            continue
        if dichtstbij(t, specials_t) < (DEDUP_S if sec != "intro" else 2.0):
            continue
        p = plan.solo(round(t, 2), (ptr % 24) + 1, vorm)
        if p is not None:
            laatste = t
            ptr = (ptr + 5) % 24

    plan.expl.sort(key=lambda e: e["t"])
    plan.cmds.sort(key=lambda c: (c["send"], c["paal"]))
    sfeer = [{"van": round(v, 2), "tot": round(w, 2)} for v, w in SFEER]
    return {"cmds": plan.cmds, "expl": plan.expl, "sfeer": sfeer, "duur": round(duur, 2)}


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
    print(f"Resultaat : {len(tl['expl'])} explosies ({len(groepen)} groepen), "
          f"{len(tl['cmds'])} bom-cues, sfeer {tl['sfeer']}")
    print("Dichtheid per 10s:")
    for lo in range(0, int(duur) + 10, 10):
        n = sum(1 for e in tl["expl"] if lo <= e["t"] < lo + 10)
        acc = sum(1 for e in groepen if lo <= e["t"] < lo + 10)
        print(f"  {lo:3d}-{lo+10:3d}s  {n:2d} expl {'(grp '+str(acc)+')' if acc else '':9s} {'#'*n}")
    print(f"Geschreven: {args.uit}")


if __name__ == "__main__":
    main()
