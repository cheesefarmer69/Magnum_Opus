# Klokslag — spelregels (geïmplementeerd)

Tweede game-mode binnen Magnum Opus, naast Plates of Fate. Dit document beschrijft de
**geïmplementeerde** regels (v1). De engine staat in `pi/node-red/blokken/07_klokslag/`, de
simulator-weergave in `pi/simulator/`. Oorspronkelijk concept: `Klokslag_spelregels_v0.1.md`.

## Kernidee

Spelers zijn in **teams** verdeeld en bewegen rond de cirkel van **24 palen** (uren 1..24). Je
neemt een uur in door er met je team op te gaan staan: de paal-klok telt af gelijk aan het
**uurnummer** (`H` seconden). Wie aan het einde van de speeltijd de **meeste uren** bezit, wint.

## Inname-mechaniek (§4)

Per paal, elke tick, telt de engine de spelers per team:

- **leider** = team met de meeste spelers; **voorsprong** = `aantal(leider) − aantal(tweede)`.
- **snelheid** = `1,0 + 0,1 × min(voorsprong − 1, 3)` (×1,0 / 1,1 / 1,2 / 1,3 bij voorsprong 1/2/3/4+).
- **magnitude + controller** (werkt voor 2 én 3 teams): elke paal heeft `P` (0..H), een
  `controller` (team dat `P` opbouwt) en een `eigenaar` (zodra `P = H`). De inname-tijd `H` is het
  **uurnummer met een ondergrens**: `H = max(uur, min_inname_s)` (default 5 s). Zonder ondergrens zou
  paal 1 in 1 s ingenomen zijn — ruim binnen de positie-settle-latentie (4–8 s) — waardoor lage uren
  ruis-flippers werden (G8). De **score** blijft het uurnummer, niet `H`.
  - controller == leider → `P` stijgt richting `H`; bij `P = H` is de paal **ingenomen**.
  - leider ≠ controller → `P` daalt richting 0 (een vijandelijk uur overnemen kost dus `2H`).
  - `P = 0` → neutraal; de leider wordt controller en `P` stijgt.
- **gelijkspel of leeg vóór inname** → `P` vervalt met `verval_per_sec` (1/s) richting 0.
- **verlaten ingenomen paal** (`P = H`, niemand meer aanwezig) → **vergrendeld**: blijft van de
  eigenaar tot een tegenstander hem actief overneemt. *(Beslist met Nic; past bij de score-balans:
  dure uren zijn "sticky".)*

## Score & winnaar (§7)

- **Score = aantal bezeten uren** (1 punt per uur), config-omschakelbaar naar `som_uurnummers`.
- **Tiebreak**: som van de uurnummers.
- Dure uren (hoog nummer) zijn moeilijker over te nemen (`2H`) → de spanning breedte (veel goedkope
  uren) vs. diepte (weinig dure, vergrendelde uren) ontstaat vanzelf.

## Teams (config)

Teams worden **handmatig** toegewezen per speler in `[CONFIG] Teams` (flow 07): elk team heeft een
`id`, `naam`, `kleur` (preset) en een ledenlijst van spelernamen. Aantal teams (2 of 3) en de
indeling zijn vrij. Teamkleur-presets (§6): blauw, rood, groen, geel, paars, wit, oranje, cyaan.

## LED-gedrag (§6)

- **rust/neutraal**: zacht ademend dim wit.
- **inname (capturing)**: kaarsflikker in de controller-kleur; helderheid schaalt met `P/H`.
- **ingenomen (owned)**: constant fel in de eigenaar-kleur.
- **gelijkspel/verval (frozen)**: bevroren op de laatste controller-kleur, helderheid volgt `P`.

In de **simulator** wordt dit rechtstreeks gerenderd uit `klokslag/palen`. Op **echte hardware**
stuurt de engine `actie 16` (`MSG_KLOKSLAG`, teamkleur + helderheid + modus) naar de slaves; die
renderen de flikker/ademing zelf (`docs/protocol.md` §0/§2).

## Timing & feedback

- **Speeltijd** instelbaar in minuten (`speeltijd_min`); eindsignaal bij 0. Geen aftelfase: direct starten.
- **Geluid** bij mijlpalen (inname-voltooid + einde) via `audio/afspelen`.
- **Engine-tick**: 4 Hz (250 ms), afgestemd op de sim-publish (250 ms) en BLE-update (~1 s).

## Configparameters (`global.klokslagConfig`)

| Parameter | Default | Beschrijving |
|---|---|---|
| `aantal_teams` | 2 | aantal teams |
| `speeltijd_min` | 10 | rondeduur in minuten |
| `aantal_palen` | 24 | normaal 24 |
| `snelheid_max` | 1,3 | plafond snelheidsbonus |
| `verval_per_sec` | 1,0 | tempo waarmee `P` naar 0 zakt bij gelijkspel/leeg |
| `score_methode` | `aantal_uren` | `aantal_uren` of `som_uurnummers` |
| `tick_hz` | 4 | engine-resolutie |
| `min_inname_s` | 5 | ondergrens inname-tijd: `H = max(uur, min_inname_s)` (G8 — lage uren niet sub-sensing) |
| `verlaten_paal` | `vergrendeld` | gedrag verlaten ingenomen paal |
| `geluid_mijlpalen` | `true` | geluid bij inname-voltooid + einde |
| `rust_led` | `ademend` | neutrale LED-staat |

## Sensing: overdrive-modus (snellere positie)

Klokslag (en Infected) draaien **automatisch in overdrive-sensing**: zodra `spelType` op Klokslag of
Infected staat, verkort Node-RED (node "Sla spelType op") het locatie-`venster` (→ 2500 ms) en de
`grace` (→ 1500 ms) en zet de BLE-scan op **400 ms** op alle palen — versere positie in ruil voor iets
meer ruis. Bij terugkeer naar Plates of Fate worden je normale, gekalibreerde `locParams` en scan-duur
hersteld (snapshot `locParamsNormaal`/`scanNormaalMs`). Je **RSSI-vloer en hysterese** blijven in
overdrive behouden. In de **simulator** heeft dit geen effect (sim gebruikt exacte posities, geen BLE).
Details: `docs/locatiebepaling.md`.

Zie ook: `pi/node-red/blokken/07_klokslag/README.md` (engine), `docs/spel/spel.md` (speltypes).
