# Magnum Opus — documentatie-index

Overzicht van alle docs en **waarom ze zo gegroepeerd zijn**. Onderaan: **welke files je
meegeeft aan Claude in een nieuwe chat**.

## Structuur

```
docs/
├── README.md            ← dit overzicht
├── invarianten.md       ← alle invarianten van het systeem op één plek
├── protocol.md          ← communicatieprotocol (ESP-NOW / serial / MQTT)
├── locatiebepaling.md   ← RSSI-locatie-algoritme + tuning
├── versions.md          ← exacte versies van alle libraries/tools
├── todo.md              ← openstaande taken
├── spel/                ← SPELONTWERP (horen sterk samen, kruisverwijzen elkaar)
│   ├── spel.md              ← oriëntering + mechanieken (middag-/avondspel) + speltypes
│   ├── klokslag.md          ← Klokslag-minigame (teamgebaseerde inname; tweede game-mode)
│   ├── infected.md          ← Infected-minigame (besmetting + bestrijders; derde game-mode)
│   ├── event-systeem.md     ← LEIDEND: verplaatsingscontrole, STAP/TELEPORT, scoring
│   ├── event-catalogus.md   ← per-categorie overzicht van alle events
│   ├── events.md            ← schema-referentie om een event-object op te stellen
│   └── nieuw-event-toevoegen.md ← korte checklist: wat lever je aan voor een nieuw event
├── hardware/            ← FYSIEKE HARDWARE
│   ├── pinout.md            ← GPIO-toewijzing slave/master (single source of truth)
│   ├── hardware-info.md     ← voeding & batterij (drempels + hot-swap), weersbestendigheid, aandachtspunten/rev-B (H4-H7)
│   └── playfield.md         ← geometrie van het 24-hoekig speelveld
└── handleidingen/       ← OPERATIONELE HOW-TO's per component
    ├── master.md · slave.md · serial-bridge.md · audio-player.md
    ├── dashboards.md        ← de Node-RED Dashboard 2.0-pagina's (functie/opbouw)
    ├── hub-noodherstel.md   ← "hub vervangen in 10 min"-runbook (H10: SPOF, reserve-SD/powerbank)
    └── spel-testen.md       ← autonoom AI-agent testen: pre-flight + scripted + live-agent
```

## Waarom deze groepering

- **`spel/`** bundelt het spelontwerp. Deze documenten verwijzen voortdurend naar elkaar
  (regels → catalogus → schema → scoring) en moeten **samen consistent** blijven; daarom
  staan ze in één map. `event-systeem.md` is hierbij **leidend** voor de regels.
- **`hardware/`** bevat wat fysiek/elektrisch is (pinout, speelveld-geometrie) — los van de
  spellogica, want het verandert om andere redenen (PCB-revisies, bedrading).
- **`handleidingen/`** zijn praktische how-to's per draaiend component (één per service).
- **Top-level** staat het overkoepelende dat door alles heen snijdt: `invarianten.md`
  (eigenschappen die altijd gelden), `protocol.md` (het contract tussen de lagen),
  `locatiebepaling.md`, `versions.md` en `todo.md`.

---

## Nieuwe chat met Claude — welke files meegeven?

Claude laadt **`CLAUDE.md`** automatisch (projectcontext). Geef daarnaast, afhankelijk van
het onderwerp:

**Altijd nuttig (kern):**
- `Design_rules.md` — vaste ontwerp- en werkregels.
- `docs/invarianten.md` — alle invarianten op één plek.

**Bij spellogica / events / Node-RED:**
- `docs/spel/spel.md`, `docs/spel/event-systeem.md`, `docs/spel/event-catalogus.md`,
  `docs/spel/events.md` (+ `docs/spel/nieuw-event-toevoegen.md` als je een event toevoegt).
- `pi/node-red/flows.json` + de relevante `pi/node-red/blokken/*/README.md`.

**Bij hardware / firmware / communicatie:**
- `docs/protocol.md` + `docs/hardware/pinout.md`.
- `firmware/Slave/src/main.cpp` of `firmware/Master/src/main.cpp` (+ hun `platformio.ini`).

**Bij de Pi-services:**
- `pi/serial-bridge/bridge.py` (+ `pi/deploy.sh`), `pi/audio-player/player.py`,
  of `pi/simulator/sim.js` — naargelang wat je aanpakt.

**Bij autonoom testen (bugs/crashes/exploits):**
- `tools/speltest/` — AI-agent testharnas dat het Plates-of-Fate-spel via MQTT speelt en
  elke ronde tegen een orakel toetst. **How-to (begin hier): `docs/handleidingen/spel-testen.md`**
  — pre-flight-checklist + scripted + live-agent in één flow. Referentie: `tools/speltest/README.md`
  (architectuur) en `tools/speltest/AGENT.md` (live-agent-modus). Steunt op
  `docs/spel/event-systeem.md`, `docs/invarianten.md` en het `sim/bediening`-topic
  (`docs/protocol.md` §5).

> Tip: je hoeft zelden álles te geven. `CLAUDE.md` + `Design_rules.md` +
> `docs/invarianten.md` + de 1–3 files van het onderwerp volstaan meestal. Claude leest
> de rest zelf bij via de verwijzingen in die documenten.
