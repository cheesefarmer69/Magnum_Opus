#!/usr/bin/env python3
"""Lijst alle WAV's op die het spel OPVRAAGT maar die niet op schijf staan.

Leest twee bronnen, want niet alle audio staat in de event-configs:
  1. de `[CONFIG]`-event-injects (audioVoor/audioNa/audioAfgelopen/sfxReactie)
  2. de hardgecodeerde "...wav"-verwijzingen in de function-nodes (systeemcues,
     afgelopen-cues van knop-events, cutscenes)

Ontbrekende bestanden zijn niet fataal — `player.py` slaat ze stil over — maar op dat
moment hoor je niets. Gebruik de uitvoer als opnamelijst.

Gebruik:  python tools/audio/ontbrekende_wavs.py [--alle]
          --alle  toont ook de bestanden die WEL bestaan
"""
import json
import os
import re
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HIER, "..", ".."))
FLOWS = os.path.join(ROOT, "pi", "node-red", "flows.json")
AUDIO = os.path.join(ROOT, "pi", "audio-player", "audio")

CAT_MAP = {"verplaatsing": "verplaatsingen", "toestand": "toestanden", "wereld": "wereld-events"}
CONFIG_NODES = ("c6a0000000000002", "c6a000000000002a", "c6a000000000002b")
# mappen die als een pad-prefix in de code voorkomen
PREFIXEN = ("events", "sound-effect", "muziek", "prefix", "woorden", "doelwit", "groepen", "getallen", "spelers")


def verzamel():
    flows = json.load(open(FLOWS, encoding="utf-8"))
    byid = {n["id"]: n for n in flows if isinstance(n, dict) and "id" in n}
    gevraagd = {}          # pad -> waar het vandaan komt

    def voeg_toe(pad, bron):
        gevraagd.setdefault(pad, set()).add(bron)

    # 1) event-configs
    for nid in CONFIG_NODES:
        if nid not in byid:
            continue
        for e in json.loads(byid[nid]["payload"]):
            cat = CAT_MAP.get(e.get("categorie"), "")
            naam = e.get("naam") or e.get("id")
            for veld in ("audioVoor", "audioNa"):
                if e.get(veld):
                    voeg_toe(f"events/{cat}/{e[veld]}", naam)
            if e.get("audioAfgelopen"):
                voeg_toe(f"events/afgelopen/{e['audioAfgelopen']}", naam)
            if e.get("sfxReactie"):
                voeg_toe(f"sound-effect/{cat}/{e['sfxReactie']}", naam)
            for opt in (e.get("audioVoorOpties") or []):
                if opt:
                    voeg_toe(f"events/{cat}/{opt}", naam)

    # 2) hardgecodeerd in function-nodes
    patroon = re.compile(r'"((?:' + "|".join(PREFIXEN) + r')/[A-Za-z0-9_\-/]+\.wav)"')
    for n in flows:
        if not isinstance(n, dict) or n.get("type") != "function":
            continue
        for m in patroon.finditer(n.get("func", "")):
            voeg_toe(m.group(1), n.get("name") or n.get("id"))
    return gevraagd


def main():
    alle = "--alle" in sys.argv
    gevraagd = verzamel()
    ontbreekt, aanwezig = [], []
    for pad in sorted(gevraagd):
        (aanwezig if os.path.exists(os.path.join(AUDIO, pad)) else ontbreekt).append(pad)

    print(f"Opgevraagd: {len(gevraagd)} WAV's  |  aanwezig: {len(aanwezig)}  |  ONTBREEKT: {len(ontbreekt)}\n")
    if ontbreekt:
        print("NOG OP TE NEMEN:")
        for pad in ontbreekt:
            bronnen = ", ".join(sorted(gevraagd[pad]))
            print(f"  {pad:<58} <- {bronnen}")
    if alle and aanwezig:
        print("\nAANWEZIG:")
        for pad in aanwezig:
            print(f"  {pad}")
    return 1 if ontbreekt else 0


if __name__ == "__main__":
    sys.exit(main())
