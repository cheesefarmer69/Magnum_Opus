"""Rapportbouwer — schrijft rapport.md (mens) + rapport.json (machine).

Het markdown-rapport vat de sessie samen: aantal rondes, bevindingen per
severity/categorie, en per bevinding het scenario (verwacht vs. gekregen) met het
reproductie-commando. De volledige ronde-log en bevindingen staan ook als JSON.
"""
from __future__ import annotations

import json
import os
from collections import Counter
from typing import Any


def schrijf_rapport(out_dir: str, data: dict[str, Any]) -> None:
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "rapport.json"), "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    with open(os.path.join(out_dir, "rapport.md"), "w", encoding="utf-8") as f:
        f.write(_markdown(data))


def _markdown(data: dict[str, Any]) -> str:
    bev = data.get("bevindingen", [])
    log = data.get("rondes_log", [])
    sev = Counter(b.get("severity", "?") for b in bev)
    typ = Counter(b.get("type", "?") for b in bev)

    r: list[str] = []
    r.append("# Magnum Opus — speltest-rapport\n")
    r.append("## Samenvatting\n")
    r.append(f"- Broker: `{data.get('broker')}`")
    r.append(f"- Strategieën: {', '.join(data.get('strategieen', []))}")
    r.append(f"- Rondes per strategie: {data.get('rondes_per_strategie')}")
    r.append(f"- Totaal gespeelde rondes: {len(log)}")
    r.append(f"- Protocol-fuzzing: {'ja' if data.get('fuzz') else 'nee'}")
    r.append(f"- **Bevindingen: {len(bev)}**")
    if sev:
        r.append("  - per severity: " + ", ".join(f"{k}={v}" for k, v in sev.items()))
    if typ:
        r.append("  - per type: " + ", ".join(f"{k}={v}" for k, v in typ.items()))
    r.append("")

    if not bev:
        r.append("✅ Geen bevindingen — engine kwam overeen met het orakel en bleef live.\n")
    else:
        r.append("## Bevindingen\n")
        for b in bev:
            r.append(_bevinding_md(b))

    r.append("## Per-ronde detail (eerste 40)\n")
    r.append("| # | strategie | event | voorwaarde | mismatches |")
    r.append("|--:|-----------|-------|-----------|-----------|")
    for reg in log[:40]:
        mm = len(reg.get("mismatches", []))
        vlag = "❌" if mm else "✅"
        r.append(f"| {reg.get('ronde')} | {reg.get('strategie')} | {reg.get('event')} "
                 f"| {reg.get('voorwaarde')} | {vlag} {mm} |")
    if len(log) > 40:
        r.append(f"\n_({len(log) - 40} rondes meer — zie rapport.json)_")
    r.append("")
    return "\n".join(r)


def _bevinding_md(b: dict[str, Any]) -> str:
    out: list[str] = []
    out.append(f"### [{b.get('severity', '?').upper()}] {b.get('type')} — `{b.get('id')}`\n")
    if b.get("type") == "stall":
        out.append(f"- Strategie: {b.get('strategie')} · ronde {b.get('ronde')} · fase **{b.get('fase')}**")
        out.append(f"- Event: {b.get('event')}")
        out.append(f"- Detail: {b.get('detail')}")
        out.append(f"- Herstel: {b.get('herstel')}\n")
        return "\n".join(out)
    if b.get("type", "").startswith("fuzz"):
        out.append(f"- Payload: {b.get('payload')}")
        out.append(f"- Detail: {b.get('detail')}\n")
        return "\n".join(out)
    # scoring-mismatch
    out.append(f"- Strategie: {b.get('strategie')} · ronde {b.get('ronde')} · event **{b.get('event')}**")
    for m in b.get("mismatches", []):
        out.append(f"  - speler **{m.get('speler')}** · veld `{m.get('veld')}`: "
                   f"verwacht `{m.get('verwacht')}`, kreeg `{m.get('gekregen')}` "
                   f"(basis-status: {m.get('base_status')}; regel: {m.get('regel')})")
    out.append(f"- Replay: `out/replays/{b.get('replay')}.json`")
    out.append(f"- Reproduceer: `python -m tools.speltest.runner --replay out/replays/{b.get('replay')}.json`\n")
    return "\n".join(out)
