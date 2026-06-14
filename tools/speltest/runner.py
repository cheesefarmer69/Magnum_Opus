"""Runner — orkestreert testsessies, verzamelt bevindingen, schrijft het rapport.

Eén commando start een sessie: zet sim-modus, wist de globale stats, en speelt N
rondes per strategie. Elke ronde wordt tegen het orakel getoetst; een mismatch,
stall of fuzz-liveness-fout is een **bevinding** met een herafspeelbaar replay-bestand.

CLI:
  python -m tools.speltest.runner --strategie all --rondes 30 --fuzz --report out/
  python -m tools.speltest.runner --replay out/replays/<id>.json
"""
from __future__ import annotations

import argparse
import json
import os
import time
import uuid
from typing import Any

from . import config
from .mqtt_client import MqttBus
from .game_driver import GameDriver, RondeData, StallFout
from .oracle import Orakel, vergelijk
from . import strategies as strat
from . import fuzzer
from . import report as rapport


class Sessie:
    def __init__(self, bus: MqttBus, settle: float, middernacht: bool):
        self.bus = bus
        self.driver = GameDriver(bus, settle_s=settle)
        self.orakel = Orakel()
        self.middernacht = middernacht
        self.rondes_log: list[dict[str, Any]] = []
        self.bevindingen: list[dict[str, Any]] = []
        self.events_def: dict[str, dict] = {}

    def voorbereiden(self) -> None:
        self.bus.subscribe(config.SUB_TOPICS)
        time.sleep(0.8)
        self.driver.setup_sim(middernacht=self.middernacht)
        self.driver.wis_stats()
        self.orakel.reset()
        self.driver.set_manueel(True)
        self.driver.start()
        self.driver.init_spelers()
        self.events_def = self.driver.laad_events()

    def afsluiten(self) -> None:
        try:
            self.driver.stop()
        except Exception:
            pass

    # --- één ronde spelen met een strategie ---
    def speel_ronde(self, strategie_naam: str, ronde_nr: int, replay_dir: str) -> None:
        fn = strat.STRATEGIEEN[strategie_naam]
        try:
            event = self.driver.volgende()
        except StallFout as e:
            self._noteer_stall(strategie_naam, ronde_nr, "volgende", str(e))
            raise
        plan = fn(self.driver, event)
        for naam, palen in plan.items():
            if palen:
                self.driver.walk(naam, palen)
        try:
            controle = self.driver.controle()
        except StallFout as e:
            self._noteer_stall(strategie_naam, ronde_nr, "controle", str(e), event)
            raise

        rd = self.driver.verzamel_ronde(controle)
        ctx = self.driver.naar_context(rd)
        verwacht = self.orakel.evalueer_ronde(ctx)
        mismatches = vergelijk(verwacht, controle)

        regel = {
            "strategie": strategie_naam,
            "ronde": ronde_nr,
            "event": event.get("naam"),
            "voorwaarde": event.get("voorwaarde"),
            "doelwit": event.get("doelwit"),
            "plan": plan,
            "orakel": {n: {"status": v.collapsed_status, "delta": v.delta} for n, v in verwacht.items()},
            "controle": controle.get("resultaten"),
            "mismatches": [vars(m) for m in mismatches],
        }
        self.rondes_log.append(regel)

        if mismatches:
            bid = self._schrijf_replay(replay_dir, strategie_naam, event, rd, plan, verwacht, mismatches)
            self.bevindingen.append({
                "id": bid,
                "type": "scoring-mismatch",
                "severity": "hoog",
                "strategie": strategie_naam,
                "ronde": ronde_nr,
                "event": event.get("naam"),
                "mismatches": [vars(m) for m in mismatches],
                "replay": bid,
            })

    def _noteer_stall(self, strategie, ronde, fase, detail, event=None):
        bid = uuid.uuid4().hex[:8]
        self.bevindingen.append({
            "id": bid, "type": "stall", "severity": "kritiek",
            "strategie": strategie, "ronde": ronde, "fase": fase,
            "event": (event or {}).get("naam"), "detail": detail,
            "herstel": "docker restart van de Node-RED-container op de Pi.",
        })

    def _schrijf_replay(self, replay_dir, strategie, event, rd: RondeData, plan, verwacht, mismatches) -> str:
        bid = uuid.uuid4().hex[:8]
        os.makedirs(replay_dir, exist_ok=True)
        # event-id opzoeken zodat de replay het event kan forceren
        ev_id = None
        for naam, e in self.events_def.items():
            if naam == event.get("naam"):
                ev_id = e.get("id")
                break
        payload = {
            "id": bid,
            "strategie": strategie,
            "event_naam": event.get("naam"),
            "event_id": ev_id,
            "middernacht": self.middernacht,
            "start_pos": rd.start_pos,
            "plan": plan,
            "wereld": {
                "portalen": rd.portals, "happy_palen": rd.happy_palen,
                "medicijn_palen": rd.medicijn_palen, "zieke": rd.zieke,
                "dienaars": rd.dienaars, "mn_dicht": rd.mn_dicht, "mn_paal": rd.mn_paal,
            },
            "verwacht": {n: {"status": v.collapsed_status, "delta": v.delta} for n, v in verwacht.items()},
            "gekregen": rd.controle.get("resultaten") if rd.controle else None,
            "mismatches": [vars(m) for m in mismatches],
        }
        with open(os.path.join(replay_dir, f"{bid}.json"), "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)
        return bid


def run(args) -> int:
    bus = MqttBus(args.broker, args.port)
    try:
        bus.connect()
    except Exception as e:
        print(f"FOUT: kon niet verbinden met broker {args.broker}:{args.port}: {e}")
        return 2

    out_dir = args.report
    replay_dir = os.path.join(out_dir, "replays")
    os.makedirs(out_dir, exist_ok=True)

    strategieen = list(strat.STRATEGIEEN) if args.strategie == "all" else \
        [s.strip() for s in args.strategie.split(",") if s.strip()]
    onbekend = [s for s in strategieen if s not in strat.STRATEGIEEN]
    if onbekend:
        print(f"FOUT: onbekende strategie(ën): {onbekend}. Keuze: {list(strat.STRATEGIEEN)}")
        bus.close()
        return 2

    sessie = Sessie(bus, settle=args.settle, middernacht=not args.geen_middernacht)
    fuzz_bevindingen: list[dict[str, Any]] = []
    try:
        sessie.voorbereiden()
        for s in strategieen:
            print(f"== strategie '{s}' — {args.rondes} rondes ==")
            for r in range(1, args.rondes + 1):
                try:
                    sessie.speel_ronde(s, r, replay_dir)
                except StallFout:
                    print(f"  STALL in ronde {r} ({s}) — sessie afgebroken.")
                    break
                if r % 5 == 0:
                    print(f"  ronde {r}/{args.rondes} (bevindingen tot nu: {len(sessie.bevindingen)})")
        sessie.afsluiten()

        if args.fuzz:
            print("== protocol-fuzzing ==")
            fb = fuzzer.fuzz(bus, herhalingen=args.fuzz_herhalingen)
            for b in fb:
                fuzz_bevindingen.append({
                    "id": uuid.uuid4().hex[:8], "type": "fuzz-" + b.categorie,
                    "severity": "kritiek" if b.categorie == "liveness" else "midden",
                    "payload": b.payload_omschrijving, "detail": b.detail,
                })
    finally:
        bus.close()

    alle_bevindingen = sessie.bevindingen + fuzz_bevindingen
    rapport.schrijf_rapport(out_dir, {
        "broker": args.broker,
        "strategieen": strategieen,
        "rondes_per_strategie": args.rondes,
        "fuzz": bool(args.fuzz),
        "rondes_log": sessie.rondes_log,
        "bevindingen": alle_bevindingen,
    })
    print(f"\nKlaar. {len(alle_bevindingen)} bevinding(en). Rapport: {os.path.join(out_dir, 'rapport.md')}")
    return 1 if alle_bevindingen else 0


def run_replay(args) -> int:
    with open(args.replay, encoding="utf-8") as f:
        rp = json.load(f)
    bus = MqttBus(args.broker, args.port)
    try:
        bus.connect()
    except Exception as e:
        print(f"FOUT: kon niet verbinden: {e}")
        return 2
    bus.subscribe(config.SUB_TOPICS)
    time.sleep(0.8)
    driver = GameDriver(bus, settle_s=args.settle)
    driver.setup_sim(middernacht=rp.get("middernacht", True))
    driver.wis_stats()
    driver.set_manueel(True)
    driver.start()
    # forceer het event door alle andere uit te sluiten
    events = driver.laad_events()
    alle_ids = [e.get("id") for e in events.values()]
    doel_id = rp.get("event_id")
    if doel_id:
        bus.publish_json(config.T_EVENTS_CONFIG,
                         {"uitgesloten": [i for i in alle_ids if i != doel_id]}, retain=True)
        time.sleep(0.4)
    # herstel startposities
    driver.plaats({n: int(p) for n, p in rp["start_pos"].items()})
    print(f"Replay {rp['id']} — event '{rp.get('event_naam')}' geforceerd.")
    try:
        event = driver.volgende()
    except StallFout as e:
        print(f"STALL: {e}")
        bus.close()
        return 3
    for naam, palen in rp["plan"].items():
        if palen:
            driver.walk(naam, palen)
    try:
        controle = driver.controle()
    except StallFout as e:
        print(f"STALL: {e}")
        bus.close()
        return 3
    rd = driver.verzamel_ronde(controle)
    verwacht = Orakel().evalueer_ronde(driver.naar_context(rd))
    mm = vergelijk(verwacht, controle)
    # herstel: events-config weer leeg
    bus.publish_json(config.T_EVENTS_CONFIG, {"uitgesloten": []}, retain=True)
    driver.stop()
    bus.close()
    print(json.dumps({
        "gereproduceerd_event": event.get("naam"),
        "orakel": {n: {"status": v.collapsed_status, "delta": v.delta} for n, v in verwacht.items()},
        "controle": controle.get("resultaten"),
        "mismatches": [vars(m) for m in mm],
        "oordeel": "GEREPRODUCEERD (mismatch)" if mm else "geen mismatch deze keer",
    }, ensure_ascii=False, indent=2))
    return 1 if mm else 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Magnum Opus speltest — runner")
    ap.add_argument("--broker", default=config.DEFAULT_BROKER)
    ap.add_argument("--port", type=int, default=config.DEFAULT_PORT)
    ap.add_argument("--strategie", default="all", help="komma-lijst of 'all'")
    ap.add_argument("--rondes", type=int, default=20)
    ap.add_argument("--settle", type=float, default=0.5, help="settle-tijd per hop (s)")
    ap.add_argument("--geen-middernacht", action="store_true")
    ap.add_argument("--fuzz", action="store_true")
    ap.add_argument("--fuzz-herhalingen", type=int, default=1)
    ap.add_argument("--report", default="out", help="uitvoermap voor rapport + replays")
    ap.add_argument("--replay", help="speel een replay-bestand af i.p.v. een sessie")
    args = ap.parse_args()
    if args.replay:
        return run_replay(args)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
