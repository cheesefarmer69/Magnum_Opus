"""Speel-strategieën: elk profiel mikt op een ander soort fout.

Een strategie krijgt de driver (met `start_pos` gesnapshot bij volgende()) en het
onthulde event, en geeft een **plan** terug: `{spelernaam: [paal, paal, ...]}` — de
opeenvolgende palen waarlangs die speler loopt (elke stap = één settled hop). Spelers
die niet in het plan staan, blijven stil.

- braaf      — exact legaal (regressie op de happy-loop).
- grens      — exact op de grenzen (off-by-one, middernacht-oversteek, portaal-laatste-hop).
- overtreder — opzettelijk fout (alle straffen + niet-doelwit dat beweegt).
- chaos      — willekeurig (races, stalls, settle-problemen).
- exploit    — gericht misbruik (portaal-pingpong, happy-herhaling, dienaar-omzeiling).
"""
from __future__ import annotations

import random
from typing import Any, Callable

from . import config

RING = config.RING


def _vooruit(paal: int, n: int) -> int:
    return ((paal - 1 + n) % RING) + 1


def _achteruit(paal: int, n: int) -> int:
    return ((paal - 1 - n) % RING) + 1


def _pad_vooruit(start: int, n: int) -> list[int]:
    return [_vooruit(start, k) for k in range(1, n + 1)]


def _beweging_doelwitten(driver, event: dict[str, Any]) -> list[str]:
    """Spelernamen die in dit event mogen bewegen (leeg bij toestand/wereld)."""
    if event.get("voorwaarde") in ("min", "max", "of") and event.get("doelwitType") in ("speler", "groep"):
        return [n for n in (event.get("doelwit") or []) if n in driver.start_pos]
    return []


def _budget(event: dict[str, Any]) -> tuple[int | None, int | None]:
    return event.get("getalWaarde"), event.get("getalWaarde2")


def _actieve_portalen(driver) -> list[tuple[int, int]]:
    return [(a, b) for a, b in (driver._portals() or [])]


def _andere_speler(driver, behalve: list[str]) -> str | None:
    kand = [n for n in driver.start_pos if n not in behalve]
    return random.choice(kand) if kand else None


# ---------------------------------------------------------------------------
# Strategieën
# ---------------------------------------------------------------------------
def braaf(driver, event: dict[str, Any]) -> dict[str, list[int]]:
    plan: dict[str, list[int]] = {}
    doel = _beweging_doelwitten(driver, event)
    x, y = _budget(event)
    for naam in doel:
        start = driver.start_pos[naam]
        if event["voorwaarde"] == "max":
            n = min(x or 0, 3)
            if n > 0:
                plan[naam] = _pad_vooruit(start, n)
        elif event["voorwaarde"] == "min":
            plan[naam] = _pad_vooruit(start, (x or 1))
        elif event["voorwaarde"] == "of":
            plan[naam] = _pad_vooruit(start, x or 1)
    return plan


def grens(driver, event: dict[str, Any]) -> dict[str, list[int]]:
    plan: dict[str, list[int]] = {}
    doel = _beweging_doelwitten(driver, event)
    x, y = _budget(event)
    portalen = _actieve_portalen(driver)
    for naam in doel:
        start = driver.start_pos[naam]
        if event["voorwaarde"] == "max":
            # precies op het budget, of bewust 0 (stil = legaal)
            n = x if random.random() < 0.7 else 0
            plan[naam] = _pad_vooruit(start, n) if n else []
        elif event["voorwaarde"] == "of":
            n = random.choice([x, y])
            plan[naam] = _pad_vooruit(start, n or 1)
        else:
            plan[naam] = _pad_vooruit(start, x or 1)
        # af en toe een portaal als laatste hop (telt 0 — mag het budget niet breken)
        if portalen and random.random() < 0.3 and plan.get(naam):
            a, b = portalen[0]
            laatste = plan[naam][-1]
            # loop naar a en spring naar b (richting-agnostisch)
            extra = _pad_vooruit(laatste, ((a - laatste) % RING)) if a != laatste else []
            plan[naam] = plan[naam] + extra + [b]
    return plan


def overtreder(driver, event: dict[str, Any]) -> dict[str, list[int]]:
    plan: dict[str, list[int]] = {}
    doel = _beweging_doelwitten(driver, event)
    x, y = _budget(event)
    keuze = random.random()
    if doel:
        for naam in doel:
            start = driver.start_pos[naam]
            if event["voorwaarde"] == "max":
                plan[naam] = _pad_vooruit(start, (x or 0) + 2)          # TE VEEL
            elif event["voorwaarde"] == "of":
                plan[naam] = _pad_vooruit(start, (x or 1) + 1)          # ONGELDIGE KEUZE (mits != y)
            else:
                plan[naam] = [_achteruit(start, 1)]                     # TERUG IN TIJD
        # soms ook een niet-doelwit laten bewegen (BEWOOG)
        if keuze < 0.5:
            ander = _andere_speler(driver, doel)
            if ander:
                plan[ander] = _pad_vooruit(driver.start_pos[ander], 1)
    else:
        # toestand/wereld: niemand mag bewegen -> beweeg toch iemand (BEWOOG)
        ander = _andere_speler(driver, [])
        if ander:
            plan[ander] = _pad_vooruit(driver.start_pos[ander], random.randint(1, 2))
    return plan


def chaos(driver, event: dict[str, Any]) -> dict[str, list[int]]:
    plan: dict[str, list[int]] = {}
    portalen = _actieve_portalen(driver)
    for naam in driver.start_pos:
        if random.random() < 0.5:
            continue
        start = driver.start_pos[naam]
        keuze = random.random()
        if keuze < 0.5:
            plan[naam] = _pad_vooruit(start, random.randint(1, 6))
        elif keuze < 0.7:
            plan[naam] = [_achteruit(start, random.randint(1, 3))]
        elif portalen:
            a, b = random.choice(portalen)
            plan[naam] = _pad_vooruit(start, (a - start) % RING) + [b]
        else:
            plan[naam] = _pad_vooruit(start, random.randint(0, 8))
    return plan


def exploit(driver, event: dict[str, Any]) -> dict[str, list[int]]:
    """Gericht misbruik zoeken: portaal-pingpong, happy-herhaling, dienaar-omzeiling."""
    plan: dict[str, list[int]] = {}
    doel = _beweging_doelwitten(driver, event)
    x, _ = _budget(event)
    portalen = _actieve_portalen(driver)
    happy = set(driver._uur_effect_palen("happy_hour"))
    dienaars = driver._dienaars()

    for naam in (doel or list(driver.start_pos)):
        start = driver.start_pos[naam]
        # 1) Probeer op een happy-hour-uur te eindigen binnen het budget (×2 farmen).
        if happy and (event.get("voorwaarde") == "max"):
            for doelpaal in happy:
                afst = (doelpaal - start) % RING
                if 0 < afst <= (x or 0):
                    plan[naam] = _pad_vooruit(start, afst)
                    break
        # 2) Portaal-pingpong (zou ONGELDIGE TELEPORT moeten geven — test dat dat klopt).
        if naam not in plan and portalen and random.random() < 0.5:
            a, b = portalen[0]
            plan[naam] = _pad_vooruit(start, (a - start) % RING) + [b, a]
        # 3) Dienaar die toch voor zichzelf probeert te scoren (mag niet lukken).
        if naam in dienaars and naam not in plan:
            plan[naam] = _pad_vooruit(start, min(x or 2, 3))
    return plan


STRATEGIEEN: dict[str, Callable[[Any, dict], dict[str, list[int]]]] = {
    "braaf": braaf,
    "grens": grens,
    "overtreder": overtreder,
    "chaos": chaos,
    "exploit": exploit,
}
