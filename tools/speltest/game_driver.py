"""GameDriver — de 'handen en ogen' van het testharnas.

Spreekt het volledige sim-protocol (docs/protocol.md §5): publiceert sim/modus,
sim/locatie en sim/bediening, en leest pof/status, pof/controle, pof/portalen,
pof/toestanden, pof/ziekte, pof/middernacht, pof/dienaars en pof/events.

Wordt in-process gebruikt door runner.py, en heeft een one-shot CLI met een
persistente sessie-state (`.session.json`) zodat een live AI-agent het spel via
losse Bash-commando's beurt-per-beurt kan spelen (zie AGENT.md).
"""
from __future__ import annotations

import argparse
import json
import os
import time
from dataclasses import dataclass
from typing import Any

from . import config
from .mqtt_client import MqttBus
from .oracle import Orakel, RondeContext

STATE_FILE = os.path.join(os.path.dirname(__file__), ".session.json")


@dataclass
class RondeData:
    """Alles wat het orakel nodig heeft om één ronde te beoordelen."""
    event: dict[str, Any]
    start_pos: dict[str, int]
    end_pos: dict[str, int]
    paths: dict[str, list[list[int]]]
    portals: list[list[int]]
    happy_palen: list[int]
    medicijn_palen: list[int]
    zieke: list[str]
    dienaars: dict[str, str]
    mn_dicht: bool
    mn_paal: int | None
    controle: dict[str, Any] | None = None


class StallFout(Exception):
    """Engine reageerde niet binnen de timeout — mogelijke crash/stall."""


class GameDriver:
    def __init__(self, bus: MqttBus, settle_s: float = 0.5):
        self.bus = bus
        self.settle_s = settle_s
        self.posities: dict[str, int] = {}     # naam -> huidige (settled) paal
        self.start_pos: dict[str, int] = {}    # snapshot bij volgende()
        self.paths: dict[str, list[list[int]]] = {}
        self.huidig_event: dict[str, Any] | None = None
        self.middernacht_aan = True

    # ---- setup / besturing ----
    def setup_sim(self, middernacht: bool = True, uitgesloten: list[str] | None = None) -> None:
        self.middernacht_aan = middernacht
        self.bus.publish_json(config.T_MODUS, {"sim24": True}, retain=False)
        self.bus.publish_json(config.T_MIDDERNACHT_CONFIG, {"aan": middernacht}, retain=True)
        self.bus.publish_json(config.T_EVENTS_CONFIG, {"uitgesloten": uitgesloten or []}, retain=True)
        time.sleep(0.6)

    def wis_stats(self) -> None:
        self.bus.publish_str(config.T_BEDIENING, "wis-stats")
        time.sleep(0.3)

    def set_manueel(self, aan: bool) -> None:
        self.bus.publish_str(config.T_BEDIENING, "manueel-aan" if aan else "manueel-uit")
        time.sleep(0.2)

    def start(self) -> None:
        self.bus.publish_str(config.T_BEDIENING, "start")
        time.sleep(0.4)

    def stop(self) -> None:
        self.bus.publish_str(config.T_BEDIENING, "stop")
        time.sleep(0.3)

    # ---- posities ----
    def _publish_locaties(self) -> None:
        lijst = [{"mac": config.mac_van(n), "paal": p} for n, p in self.posities.items()]
        self.bus.publish_json(config.T_LOCATIE, lijst)

    def plaats(self, posities: dict[str, int], settle: float | None = None) -> None:
        """Zet (een deel van) de spelers neer en laat settelen."""
        self.posities.update(posities)
        self._publish_locaties()
        time.sleep(settle if settle is not None else self.settle_s)

    def init_spelers(self, spreiding: int = 4) -> None:
        """Verdeel de spelers gelijkmatig over de ring en laat settelen."""
        namen = list(config.SPELERS)
        self.posities = {n: 1 + (i * spreiding) % config.RING for i, n in enumerate(namen)}
        self._publish_locaties()
        time.sleep(self.settle_s + 0.3)

    def walk(self, naam: str, palen: list[int], settle: float | None = None) -> None:
        """Loop een speler stap voor stap langs `palen` (elke stap = settled hop)."""
        for p in palen:
            van = self.posities.get(naam)
            if van is None:
                self.posities[naam] = p
            else:
                self.paths.setdefault(naam, []).append([van, p])
                self.posities[naam] = p
            self._publish_locaties()
            time.sleep(settle if settle is not None else self.settle_s)

    # ---- ronde-flow (manueel) ----
    def laad_events(self, timeout: float = 5.0) -> dict[str, dict]:
        ev = self.bus.wacht_op(config.T_EVENTS, lambda p: isinstance(p, list) and len(p) > 0,
                               timeout=timeout, vanaf_nu=False)
        if not ev:
            return {}
        return {e.get("naam"): e for e in ev if isinstance(e, dict)}

    def volgende(self, timeout: float = 25.0) -> dict[str, Any]:
        """Trigger het volgende event en wacht tot het onthuld + uitgevoerd is.

        Geeft het 'event'-dict terug (samengesteld uit pof/status + pof/events).
        Snapshot de huidige posities als start_pos en wist de pad-opname.
        """
        self.start_pos = dict(self.posities)
        self.paths = {}
        events_def = self.laad_events()
        self.bus.wis(config.T_STATUS)
        self.bus.publish_str(config.T_BEDIENING, "volgende")
        status = self.bus.wacht_op(
            config.T_STATUS,
            lambda s: isinstance(s, dict) and s.get("fase") == "wacht_controle",
            timeout=timeout, vanaf_nu=True)
        if status is None:
            raise StallFout("Geen 'wacht_controle' na 'volgende' (event-keuze/reveal hangt?)")
        naam = status.get("eventNaam")
        ev_def = events_def.get(naam, {})
        self.huidig_event = {
            "naam": naam,
            "categorie": ev_def.get("categorie"),
            "voorwaarde": ev_def.get("voorwaarde") or "geen",
            "gevolgen": ev_def.get("gevolgen") or [],
            "doelwit": status.get("doelwit") or [],
            "doelwitType": status.get("doelwitType"),
            "getalWaarde": status.get("getalWaarde"),
            "getalWaarde2": status.get("getalWaarde2"),
            "groepLabel": status.get("groepLabel"),
        }
        return self.huidig_event

    def controle(self, timeout: float = 15.0) -> dict[str, Any]:
        """Voer de controle uit en wacht op het pof/controle-resultaat."""
        self.bus.wis(config.T_CONTROLE)
        self.bus.publish_str(config.T_BEDIENING, "controle")
        res = self.bus.wacht_op(config.T_CONTROLE, lambda p: isinstance(p, dict),
                                timeout=timeout, vanaf_nu=True)
        if res is None:
            raise StallFout("Geen pof/controle na 'controle' (verificatie hangt?)")
        return res

    # ---- wereld-snapshot voor het orakel ----
    def _portals(self) -> list[list[int]]:
        p = self.bus.laatste(config.T_PORTALEN) or []
        out = []
        for item in p:
            palen = item.get("palen") if isinstance(item, dict) else None
            if palen and len(palen) == 2:
                out.append([palen[0], palen[1]])
        return out

    def _uur_effect_palen(self, effect: str) -> list[int]:
        t = self.bus.laatste(config.T_TOESTANDEN) or []
        return [r.get("uur") for r in t if isinstance(r, dict) and r.get("effect") == effect]

    def _zieke(self) -> list[str]:
        z = self.bus.laatste(config.T_ZIEKTE) or []
        return [r.get("speler") for r in z if isinstance(r, dict)]

    def _dienaars(self) -> dict[str, str]:
        d = self.bus.laatste(config.T_DIENAARS) or {}
        return d if isinstance(d, dict) else {}

    def _middernacht(self) -> tuple[bool, int | None]:
        m = self.bus.laatste(config.T_MIDDERNACHT) or {}
        if not isinstance(m, dict) or not self.middernacht_aan:
            return False, None
        dicht = m.get("open") is False
        return dicht, m.get("paal")

    def verzamel_ronde(self, controle: dict[str, Any] | None) -> RondeData:
        mn_dicht, mn_paal = self._middernacht()
        return RondeData(
            event=dict(self.huidig_event or {}),
            start_pos=dict(self.start_pos),
            end_pos=dict(self.posities),
            paths={k: list(v) for k, v in self.paths.items()},
            portals=self._portals(),
            happy_palen=self._uur_effect_palen("happy_hour"),
            medicijn_palen=self._uur_effect_palen("medicijn"),
            zieke=self._zieke(),
            dienaars=self._dienaars(),
            mn_dicht=mn_dicht, mn_paal=mn_paal,
            controle=controle,
        )

    @staticmethod
    def naar_context(rd: RondeData) -> RondeContext:
        return RondeContext(
            event=rd.event, start_pos=rd.start_pos, end_pos=rd.end_pos, paths=rd.paths,
            portals=[(a, b) for a, b in rd.portals],
            happy_palen=set(rd.happy_palen), medicijn_palen=set(rd.medicijn_palen),
            zieke=set(rd.zieke), dienaars=rd.dienaars,
            mn_dicht=rd.mn_dicht, mn_paal=rd.mn_paal,
        )


# ===========================================================================
# One-shot CLI met persistente sessie-state (voor de live AI-agent)
# ===========================================================================
def _load_state() -> dict[str, Any]:
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE, encoding="utf-8") as f:
            return json.load(f)
    return {}


def _save_state(driver: GameDriver) -> None:
    st = {
        "posities": driver.posities,
        "start_pos": driver.start_pos,
        "paths": driver.paths,
        "huidig_event": driver.huidig_event,
        "middernacht_aan": driver.middernacht_aan,
    }
    with open(STATE_FILE, "w", encoding="utf-8") as f:
        json.dump(st, f, ensure_ascii=False, indent=2)


def _restore(driver: GameDriver, st: dict[str, Any]) -> None:
    driver.posities = {k: int(v) for k, v in (st.get("posities") or {}).items()}
    driver.start_pos = {k: int(v) for k, v in (st.get("start_pos") or {}).items()}
    driver.paths = st.get("paths") or {}
    driver.huidig_event = st.get("huidig_event")
    driver.middernacht_aan = st.get("middernacht_aan", True)


def _print(obj: Any) -> None:
    print(json.dumps(obj, ensure_ascii=False, indent=2))


def _cli() -> int:
    ap = argparse.ArgumentParser(description="Magnum Opus speltest — game driver")
    ap.add_argument("--broker", default=config.DEFAULT_BROKER)
    ap.add_argument("--port", type=int, default=config.DEFAULT_PORT)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("selftest", help="verbind, zet sim-modus, plaats 1 speler, lees terug")
    p_setup = sub.add_parser("setup", help="sim-modus + wis-stats + manueel + start + spelers")
    p_setup.add_argument("--geen-middernacht", action="store_true")
    sub.add_parser("peek", help="print pof/status + afgeleid event")
    sub.add_parser("state", help="print volledige pof/*-snapshot + sessie-posities")
    sub.add_parser("next", help="trigger volgend event, wacht tot onthuld")
    p_move = sub.add_parser("move", help="loop een speler langs palen")
    p_move.add_argument("speler")
    p_move.add_argument("palen", nargs="+", type=int)
    sub.add_parser("verify", help="controle + orakel-oordeel")
    sub.add_parser("stop", help="stop het spel")

    args = ap.parse_args()
    bus = MqttBus(args.broker, args.port)
    try:
        bus.connect()
    except Exception as e:
        _print({"fout": f"verbinden mislukt: {e}"})
        return 2
    bus.subscribe(config.SUB_TOPICS)
    time.sleep(0.8)  # retained berichten laten binnenkomen

    driver = GameDriver(bus)
    _restore(driver, _load_state())
    rc = 0
    try:
        if args.cmd == "selftest":
            driver.setup_sim()
            naam = list(config.SPELERS)[0]
            driver.plaats({naam: 7})
            terug = bus.wacht_op(config.T_LOCATIE_SPELERS,
                                 lambda p: isinstance(p, dict) and p.get(naam) == 7,
                                 timeout=6.0, vanaf_nu=True)
            _print({"selftest": "ok" if terug else "GEEN ANTWOORD",
                    "locatie_spelers": bus.laatste(config.T_LOCATIE_SPELERS)})
            rc = 0 if terug else 1

        elif args.cmd == "setup":
            driver.setup_sim(middernacht=not args.geen_middernacht)
            driver.wis_stats()
            driver.set_manueel(True)
            driver.start()
            driver.init_spelers()
            _save_state(driver)
            _print({"setup": "ok", "posities": driver.posities})

        elif args.cmd == "peek":
            st = bus.laatste(config.T_STATUS)
            _print({"status": st})

        elif args.cmd == "state":
            _print({
                "posities": driver.posities,
                "status": bus.laatste(config.T_STATUS),
                "portalen": bus.laatste(config.T_PORTALEN),
                "toestanden": bus.laatste(config.T_TOESTANDEN),
                "ziekte": bus.laatste(config.T_ZIEKTE),
                "middernacht": bus.laatste(config.T_MIDDERNACHT),
                "dienaars": bus.laatste(config.T_DIENAARS),
                "locatie_spelers": bus.laatste(config.T_LOCATIE_SPELERS),
            })

        elif args.cmd == "next":
            ev = driver.volgende()
            _save_state(driver)
            _print({"event": ev, "start_pos": driver.start_pos})

        elif args.cmd == "move":
            driver.walk(args.speler, args.palen)
            _save_state(driver)
            _print({"speler": args.speler, "naar": driver.posities.get(args.speler),
                    "pad": driver.paths.get(args.speler)})

        elif args.cmd == "verify":
            controle = driver.controle()
            rd = driver.verzamel_ronde(controle)
            orakel = Orakel()
            verwacht = orakel.evalueer_ronde(driver.naar_context(rd))
            from .oracle import vergelijk
            mm = vergelijk(verwacht, controle)
            _save_state(driver)
            _print({
                "controle": controle,
                "orakel": {n: {"status": v.collapsed_status, "delta": v.delta} for n, v in verwacht.items()},
                "mismatches": [vars(m) for m in mm],
                "oordeel": "BUG-KANDIDAAT" if mm else "OK",
            })
            rc = 1 if mm else 0

        elif args.cmd == "stop":
            driver.stop()
            _print({"stop": "ok"})
    except StallFout as e:
        _print({"STALL": str(e)})
        rc = 3
    finally:
        bus.close()
    return rc


if __name__ == "__main__":
    raise SystemExit(_cli())
