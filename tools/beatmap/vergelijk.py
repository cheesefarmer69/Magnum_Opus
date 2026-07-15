#!/usr/bin/env python3
"""
Vergelijk de gegenereerde tijdlijn met Nic's handmatige tijdlijn (ground truth).

Twee vragen:
 1. ZIET de signaalanalyse dezelfde muzikale hits die Nic op gehoor koos?
    -> voor elke handmatige explosie: ligt er een gedetecteerde onset binnen ±150 ms?
 2. Dekt de GEGENEREERDE tijdlijn die hits ook echt af?
    -> voor elke handmatige explosie: ligt er een gegenereerde explosie binnen ±150 ms?

De handmatige tijdlijn wordt uit de Node-RED-inject `[CONFIG] Bommen-tijdlijn` in flows.json gehaald.

Gebruik (defaults werken vanuit de repo-root):
    python tools/beatmap/vergelijk.py
    python tools/beatmap/vergelijk.py --wav ... --gen ... --flows ...
"""
import argparse
import json
import os
import sys

import numpy as np

HIER = os.path.dirname(__file__)
sys.path.insert(0, HIER)
from genereer_tijdlijn import lees_mono, onset_envelope, pick_onsets   # noqa: E402

TOLERANTIE_S = 0.15   # ±150 ms (gelijk aan de default Muziek-offset-marge)


def repo_root():
    return os.path.abspath(os.path.join(HIER, "..", ".."))


def laad_ground_truth(flows_pad):
    """Haal cmds/expl uit de inject-node '[CONFIG] Bommen-tijdlijn'."""
    with open(flows_pad, encoding="utf-8") as f:
        flows = json.load(f)
    for n in flows:
        if isinstance(n, dict) and str(n.get("name", "")).startswith("[CONFIG] Bommen"):
            return json.loads(n["payload"])
    sys.exit("Node '[CONFIG] Bommen-tijdlijn' niet gevonden in flows.json.")


def dichtstbij(doel, kandidaten):
    """Kleinste absolute tijdsafstand van `doel` tot een lijst tijdstippen (of None)."""
    if not len(kandidaten):
        return None
    return float(np.min(np.abs(np.asarray(kandidaten) - doel)))


def rapport(titel, mens_tijden, kandidaten):
    treffers, fouten = 0, []
    for t in mens_tijden:
        d = dichtstbij(t, kandidaten)
        if d is not None and d <= TOLERANTIE_S:
            treffers += 1
            fouten.append(d)
        else:
            fouten.append(d if d is not None else float("nan"))
    n = len(mens_tijden)
    pct = 100.0 * treffers / n if n else 0.0
    goede = [d for d in fouten if not np.isnan(d) and d <= TOLERANTIE_S]
    gem = (sum(goede) / len(goede) * 1000) if goede else float("nan")
    print(f"\n{titel}")
    print(f"  {treffers}/{n} handmatige explosies matchen binnen ±{int(TOLERANTIE_S*1000)} ms  ({pct:.0f}%)")
    if goede:
        print(f"  gemiddelde afwijking van de treffers: {gem:.0f} ms")
    return pct


def main():
    root = repo_root()
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", default=os.path.join(root, "pi/audio-player/audio/muziek/maki_vs_the_hei.wav"))
    ap.add_argument("--gen", default=os.path.join(HIER, "out", "maki_vs_the_hei_generated.json"))
    ap.add_argument("--flows", default=os.path.join(root, "pi/node-red/flows.json"))
    args = ap.parse_args()

    gt = laad_ground_truth(args.flows)
    mens_expl = [e["t"] for e in gt["expl"]]
    print(f"Handmatige tijdlijn : {len(gt['cmds'])} bom-cues, {len(mens_expl)} explosies")

    with open(args.gen, encoding="utf-8") as f:
        gen = json.load(f)
    gen_expl = [e["t"] for e in gen["expl"]]
    print(f"Gegenereerde tijdlijn: {len(gen['cmds'])} bom-cues, {len(gen_expl)} explosies")

    # (1) signaalanalyse: ziet de onset-detectie de menselijke hits?
    sig, rate = lees_mono(args.wav)
    flux, tijden, _ = onset_envelope(sig, rate)
    onsets = [t for t, _ in pick_onsets(flux, tijden, rate, min_gap_s=0.12)]
    print(f"\nGedetecteerde onsets: {len(onsets)}")
    rapport("(1) Ziet de ANALYSE de handmatige hits?", mens_expl, onsets)

    # (2) dekking: bevat de gegenereerde tijdlijn die hits?
    rapport("(2) Dekt de GEGENEREERDE tijdlijn de handmatige hits?", mens_expl, gen_expl)

    print("\nLet op: 100% is geen doel. Nic koos op gehoor een subset + ruimtelijke choreografie;")
    print("de analyse levert een strak-op-de-beat baseline. Deze cijfers tonen of de muzikale")
    print("structuur betrouwbaar wordt herkend, niet of de artistieke keuzes identiek zijn.")


if __name__ == "__main__":
    main()
