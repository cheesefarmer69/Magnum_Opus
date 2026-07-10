"""Gedeelde constanten voor het testharnas.

Bron-van-waarheid voor MAC's en topics: pi/simulator/sim.js (DEFAULT_SPELERS) en
docs/protocol.md §5. Houd deze in sync als de simulator-spelerset wijzigt.
"""
from __future__ import annotations

# --- Broker (TCP, niet de browser-WebSocket 9001) ---
DEFAULT_BROKER = "192.168.1.43"
DEFAULT_PORT = 1883

# --- Speelveld ---
RING = 24  # de klok loopt rond: na 24 -> 1

# --- Spelerset (overgenomen uit pi/simulator/sim.js DEFAULT_SPELERS) ---
# naam -> mac (lowercase; invariant C1)
SPELERS: dict[str, str] = {
    "Lilou": "48:87:2d:9d:bb:7d",
    "Zoë": "48:87:2d:9d:ba:5c",
    "Louisa": "48:87:2d:9d:ba:cc",
    "Lola": "48:87:2d:9d:ba:5f",
    "Maud": "48:87:2d:9d:bb:0b",
    "Mien": "48:87:2d:9d:ba:a5",
}
MAC_NAAR_NAAM: dict[str, str] = {mac: naam for naam, mac in SPELERS.items()}


def mac_van(naam: str) -> str:
    return SPELERS[naam]


# --- Topics die de harness publiceert (client -> Node-RED) ---
T_MODUS = "sim/modus"
T_LOCATIE = "sim/locatie"
T_BEDIENING = "sim/bediening"
T_MIDDERNACHT_CONFIG = "sim/middernacht-config"
T_EVENTS_CONFIG = "sim/events-config"
T_SYSTEEM_CONFIG = "sim/systeem-config"   # {toestandExclusief, tempo}
T_KNOP = "plaatjes/data"                  # drukknop: {"paal":N,"knop":1} (zelfde topic als hardware)
T_WACHTRIJ_WEG = "sim/wachtrij-weg"        # {"index":N} — verwijder aankomend event uit pofWachtrij
T_SPEL_CONFIG = "sim/spel-config"          # {"badAura":bool} — spelinstelling slechte aura
T_TIERS_CONFIG = "sim/tiers-config"        # {id: tier} — per-event tier-override
T_TIJD_TERUG = "sim/tijd-terug"            # trigger: één ronde terug in de tijd

# --- Topics waarop de harness luistert (Node-RED -> client) ---
T_STATUS = "pof/status"
T_CONTROLE = "pof/controle"
T_PORTALEN = "pof/portalen"
T_TOESTANDEN = "pof/toestanden"
T_ZIEKTE = "pof/ziekte"
T_MIDDERNACHT = "pof/middernacht"
T_DIENAARS = "pof/dienaars"
T_EVENTS = "pof/events"
T_TIJDBOM = "pof/tijdbom"
T_ANIMATIE = "pof/animatie"
T_DRUKKNOPPEN = "config/drukknoppen"
T_LOCATIE_SPELERS = "locatie/spelers"
T_HISTORIE = "spel/historie"

SUB_TOPICS = [
    T_STATUS, T_CONTROLE, T_PORTALEN, T_TOESTANDEN, T_ZIEKTE,
    T_MIDDERNACHT, T_DIENAARS, T_EVENTS, T_TIJDBOM, T_ANIMATIE, T_DRUKKNOPPEN,
    T_LOCATIE_SPELERS, T_HISTORIE,
]

# --- Opties -> bereik van het AFROEPGETAL `getal` (docs/spel/events.md) ---
# LET OP: dit is NIET doelwit.aantal. Dat laatste schaalt sub-lineair met N (EV6):
#   aantal = clamp(round(mult * sqrt(N) * (dichtheid / 0.25)), 1, min(N, 6))
#   met mult 0.35 (laag) / 0.55 (midden) / 0.90 (hoog).
OPTIE_BEREIK = {
    "enkel": (1, 1),
    "laag": (1, 3),
    "midden": (4, 6),
    "hoog": (7, 10),
}

# De statussen die de engine als "fout" op pof/controle doorlaat (rest -> "OK").
# Zie "Verifieer beweging" in flows.json: de FOUT-set.
FOUT_STATUSSEN = {
    "TERUG IN TIJD", "PENDELEN", "TE VEEL", "TE WEINIG",
    "ONGELDIGE KEUZE", "BEWOOG (mocht niet)", "ONGELDIGE TELEPORT",
    "NIET GEWISSELD", "TE WEINIG SAMEN",
}

# Statussen die met "OK" beginnen zijn ALTIJD legaal -- ook de fase-2-varianten
# "OK (poort blokkeert)" (M10), "OK (stil)", "OK (gewisseld)", "OK (polonaise +N)".
# De wolf-vangst (ET2b) en de tweeling-winst (TW2) hangen aan diezelfde OK-prefix.
