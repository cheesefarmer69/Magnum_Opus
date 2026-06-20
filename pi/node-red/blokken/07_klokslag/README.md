# Flow 07 — Klokslag (minigame-engine)

## Doel

De engine van de tweede game-mode **Klokslag** (spelregels: `docs/spel/klokslag.md`). Teams nemen
de 24 palen (uren 1..24) in door erop te gaan staan; een paal-klok telt af gelijk aan het
**uurnummer** (`H`). Wie aan het einde van de speeltijd de **meeste uren** bezit, wint.

Klokslag draait **náást** Plates of Fate (flow 06). Beide engines lezen `global.spelType`
(`"plates_of_fate"` | `"klokslag"`) en doen niets tenzij het hún spel is. De keuze komt uit het
retained MQTT-topic **`spel/type`** (zie flow 03 Bediening + de simulator). De gewone
**Spel-schakelaar** (`spelToestand` lopend/gestopt) start/stopt beide spellen; in Klokslag-modus
laat "Spel aan/uit" de PoF-engine bewust **uit** (`pofActief = false`).

## Engine-loop

`Engine tick (250ms)` (4 Hz) → **`Klokslag engine`** (toestandsmachine). Per tick (Δt = 0,25 s),
enkel als `spelType === "klokslag"` én `spelToestand === "lopend"`:

1. **Init** bij (her)start: speeltijd-timer, alle palen `P=0`, neutraal.
2. **Tellen** — uit `global.spelerLocaties` (mac→paal, gevuld door locatiebepaling óf de sim) +
   `global.klokslagTeams` (naam→team): per paal het aantal spelers per team.
3. **Inname (§4 spelregels)** — magnitude + controller-model:
   - leider + `voorsprong = aantal(leider) − aantal(tweede)`;
   - `snelheid = 1,0 + 0,1 × min(voorsprong−1, 3)` (plafond `snelheid_max`);
   - controller == leider → `P` stijgt naar `H`; bij `P=H` **ingenomen** (`eigenaar`);
   - leider ≠ controller → `P` daalt (overname kost dus `2H`); bij `P=0` neutraal;
   - **gelijkspel/leeg vóór inname** → `P` vervalt `verval_per_sec`/s richting 0;
   - **verlaten ingenomen paal** (`P=H`, leeg) = **vergrendeld** (geen verval).
4. **Score** — aantal bezeten uren per team (`score_methode`); tiebreak = som van uurnummers.
5. **Einde** — bij `resterend_s ≤ 0`: winnaar bepalen, engine stopt.

## Outputs

| Output | Topic | Inhoud |
|--------|-------|--------|
| 1 | `klokslag/palen` (retained) | per-paal `{P,H,controller,eigenaar,modus}` — voedt de sim-LED-render |
| 2 | `klokslag/status` (retained) | `{actief,fase,resterend_s,speeltijd_s,winnaar}` |
| 3 | `klokslag/score` (retained) | `{teams:[{id,naam,kleur,score,somUren,uren}],winnaar}` |
| 4 | `audio/afspelen` | mijlpaal-geluid (inname-voltooid + einde), indien `geluid_mijlpalen` |

Daarnaast bouwt **`Klokslag LED-commando's`** (gevoed door dezelfde tick) per paal een
firmware-commando `{"paal":N,"actie":16,"r":..,"g":..,"b":..,"helderheid":..,"modus":..}` en stuurt
dat naar `commando/master1|2|3` (op paal-bereik). Actie 16 = `MSG_KLOKSLAG` (zie `docs/protocol.md`):
de slave rendert de teamkleur met meeschalende helderheid + kaarsflikker. Alleen wijzigingen worden
gestuurd (cache + gekwantiseerde helderheid) om airtime te sparen.

## Configuratie

Twee `[CONFIG]`-inject-nodes (inject once bij deploy):

- **`[CONFIG] Teams`** → `global.klokslagTeams`: lijst `{id, naam, kleur, leden:[spelernaam,…]}`.
  **Handmatige** toewijzing per speler; `kleur` uit de preset-lijst (zie hieronder). Spelers
  zonder team doen niet mee. De node publiceert meteen een leeg `klokslag/score` zodat de sim de
  teams al toont vóór de start.
- **`[CONFIG] Klokslag-instellingen`** → `global.klokslagConfig`:
  `aantal_teams`, `speeltijd_min`, `aantal_palen` (24), `snelheid_max` (1,3),
  `verval_per_sec` (1,0), `score_methode` (`aantal_uren` | `som_uurnummers`), `tick_hz` (4),
  `verlaten_paal` (`vergrendeld`), `geluid_mijlpalen`, `rust_led` (`ademend`).

**Teamkleur-presets** (gedeeld met de simulator, spelregels §6): `blauw`, `rood`, `groen`, `geel`,
`paars`, `wit`, `oranje`, `cyaan`.

## Globale variabelen

| Variabele             | Gezet door            | Gelezen door                 |
|-----------------------|-----------------------|------------------------------|
| `spelType`            | 03 (selector/spel/type) | 06 + 07 engine-ticks       |
| `klokslagTeams`       | 07 `[CONFIG] Teams`   | 07 engine + LED-node         |
| `klokslagConfig`      | 07 `[CONFIG]` instell. | 07 engine                   |
| `klokslagPalen`       | 07 engine             | 07 LED-node                  |
| `klokslagStart/Einde/Winnaar` | 07 engine     | 07 engine                    |
| `spelerLocaties`      | 01 locatiebepaling / sim | 07 engine                 |

## Testen

1. Deploy via `pi/node-red/deploy-flows.ps1`; injecteer `[CONFIG] Teams` + `[CONFIG] Klokslag-instellingen`.
2. Kies **Klokslag** (simulator-header of Bediening-dashboard) → `spel/type` retained.
3. Simulator op **Simulatie**, Spel-schakelaar AAN; sleep team-spelers op een paal → `P` loopt op,
   LED flikkert in teamkleur met helderheid ∝ `P/H`; bij `P=H` constant fel + scorebord telt mee.
4. Test gelijkspel (verval), verlaten ingenomen paal (vergrendeld), overname (2H), eindsignaal bij 0.
5. Zet terug op **Plates of Fate** → PoF draait ongewijzigd; Klokslag-engine ligt stil.
