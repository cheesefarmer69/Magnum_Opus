"""Protocol-fuzzer — stuurt misvormde/extreme payloads en bewaakt de liveness.

Doel: kijken of de Node-RED-engine blijft draaien (geen crash, geen vastgelopen
fase) als de sim-topics rommel krijgen. Na elke fuzz-burst volgt een **liveness-check**:
een geldige sim/locatie publiceren en verifiëren dat locatie/spelers nog antwoordt.
Geen antwoord = crash/stall-bevinding.

Veilig op de live Pi maar herstelbaar: bij een hang is een `docker restart` van de
Node-RED-container nodig (staat in het rapport).
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass
from typing import Any

from . import config
from .mqtt_client import MqttBus


@dataclass
class FuzzBevinding:
    categorie: str          # "liveness" of "uitzondering"
    payload_omschrijving: str
    detail: str


# Een mix van type-verwarring, grenswaarden en structuurfouten.
def _locatie_payloads() -> list[tuple[str, Any]]:
    mac = list(config.SPELERS.values())[0]
    groot = [{"mac": f"00:00:00:00:00:{i % 100:02x}", "paal": (i % 24) + 1} for i in range(10000)]
    return [
        ("paal als string", [{"mac": mac, "paal": "zeven"}]),
        ("paal negatief", [{"mac": mac, "paal": -1}]),
        ("paal te hoog", [{"mac": mac, "paal": 999}]),
        ("paal float", [{"mac": mac, "paal": 7.5}]),
        ("paal null", [{"mac": mac, "paal": None}]),
        ("mac null", [{"mac": None, "paal": 7}]),
        ("mac niet-string", [{"mac": 12345, "paal": 7}]),
        ("ontbrekende velden", [{"foo": "bar"}]),
        ("lege array", []),
        ("object i.p.v. array", {"mac": mac, "paal": 7}),
        ("dubbele macs", [{"mac": mac, "paal": 3}, {"mac": mac, "paal": 9}]),
        ("enorme array (10k)", groot),
        ("diep geneste rommel", [{"mac": mac, "paal": {"x": [1, 2, {"y": 3}]}}]),
    ]


def _ruwe_payloads() -> list[tuple[str, str, bytes]]:
    # (topic, omschrijving, ruwe bytes) — niet-JSON / kapotte JSON
    return [
        (config.T_LOCATIE, "niet-JSON tekst", b"dit is geen json"),
        (config.T_LOCATIE, "kapotte JSON", b'[{"mac":"x","paal":}]'),
        (config.T_LOCATIE, "lege payload", b""),
        (config.T_MODUS, "modus zonder veld", b"{}"),
        (config.T_MODUS, "modus verkeerd type", b'{"sim24":"ja"}'),
        (config.T_MIDDERNACHT_CONFIG, "middernacht verkeerd type", b'{"aan":"misschien"}'),
        (config.T_BEDIENING, "onbekende actie", b"doe-iets-raars"),
        (config.T_BEDIENING, "lege bediening", b""),
    ]


def _liveness(bus: MqttBus, timeout: float = 6.0) -> bool:
    """Publiceer een geldige detectie en kijk of locatie/spelers reageert."""
    naam, mac = next(iter(config.SPELERS.items()))
    paal = 5
    bus.publish_json(config.T_MODUS, {"sim24": True})
    time.sleep(0.3)
    bus.publish_json(config.T_LOCATIE, [{"mac": mac, "paal": paal}])
    res = bus.wacht_op(config.T_LOCATIE_SPELERS,
                       lambda p: isinstance(p, dict) and p.get(naam) == paal,
                       timeout=timeout, vanaf_nu=True)
    return res is not None


def fuzz(bus: MqttBus, herhalingen: int = 1) -> list[FuzzBevinding]:
    bevindingen: list[FuzzBevinding] = []
    bus.subscribe(config.SUB_TOPICS)
    time.sleep(0.5)

    if not _liveness(bus):
        bevindingen.append(FuzzBevinding("liveness", "baseline vóór fuzzing",
                                         "engine reageerde al niet vóór de fuzz — controleer Node-RED"))
        return bevindingen

    for ronde in range(herhalingen):
        # 1) gestructureerde maar foute JSON
        for oms, payload in _locatie_payloads():
            try:
                bus.publish_json(config.T_LOCATIE, payload)
            except Exception as e:
                bevindingen.append(FuzzBevinding("uitzondering", oms, f"publish-fout: {e}"))
            time.sleep(0.05)
        # 2) ruwe / kapotte payloads
        for topic, oms, ruw in _ruwe_payloads():
            bus.publish_raw(topic, ruw)
            time.sleep(0.05)
        # 3) snel-knipperende bediening (start/stop race)
        for _ in range(20):
            bus.publish_str(config.T_BEDIENING, "start")
            bus.publish_str(config.T_BEDIENING, "stop")
        time.sleep(0.3)

        # liveness na de burst
        if not _liveness(bus):
            bevindingen.append(FuzzBevinding(
                "liveness", f"na fuzz-burst {ronde + 1}",
                "locatie/spelers reageert niet meer — mogelijke crash/stall. "
                "Herstel: docker restart van de Node-RED-container op de Pi."))
            break  # geen zin verder te fuzzen op een dode engine

    # herstel sim naar een propere staat
    bus.publish_str(config.T_BEDIENING, "stop")
    return bevindingen


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Magnum Opus speltest — fuzzer")
    ap.add_argument("--broker", default=config.DEFAULT_BROKER)
    ap.add_argument("--port", type=int, default=config.DEFAULT_PORT)
    ap.add_argument("--herhalingen", type=int, default=1)
    args = ap.parse_args()
    bus = MqttBus(args.broker, args.port)
    bus.connect()
    try:
        bv = fuzz(bus, args.herhalingen)
    finally:
        bus.close()
    print(json.dumps([vars(b) for b in bv], ensure_ascii=False, indent=2))
    print(f"\n{len(bv)} bevinding(en).")
    raise SystemExit(1 if bv else 0)
