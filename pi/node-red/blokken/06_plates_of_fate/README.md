# Flow 06 — Plates of Fate engine

## Doel

De centrale spel-engine van het middagspel (zie `docs/spel.md`). De engine kiest
periodiek een **event**, leest het voor (audio), kiest **wie/wat** het beïnvloedt
en voert de **gevolgen** uit. Sommige gevolgen plaatsen **blijvende effecten** die
over een aantal rondes aflopen.

> Het **event-formaat en hoe je events opstelt** staat in `docs/events.md`.
> Dit document beschrijft de engine zelf.

## Engine-loop (timer-toestandsmachine)

De engine is een toestandsmachine, aangedreven door een **1s-ticker** (`Engine tick`).
Globals: `pofActief`, `pofManueel`, `pofFase` (`idle|aanloop|bezig|reactie|wacht`),
`pofTeller`. Per seconde telt de tick af en stuurt bij 0 de volgende fase.

```
Start POF → aanloop (5s, zichtbaar aftellen)
          → CHOOSE  (Kies event → Verouder effecten → Kies doelwit → Voer gevolg uit)
          → reactie (reactietijd_s, zichtbaar aftellen)
          → VERIFY  (Verifieer beweging → tabel Controle)
          → aanloop … (automatisch)
Manueel:  geen automatische timers. Knop Volgende event → CHOOSE (event komt direct);
          daarna fase "wacht_controle" → de controle gebeurt PAS bij de knop Controle
          (zo heb je tijd om fysiek te testen). Na de controle → wachten op Volgende event.
Stop POF → pofActief = false, fase idle.
```

- **CHOOSE** (instant): `Kies event` rolt zo nodig `getal` en vult `x` in de tekst;
  `Kies doelwit` rolt het `aantal`-optie, selecteert doelwitten en **snapshot de
  beginposities van alle spelers**; `Voer gevolg uit` voert de gevolgen uit en zet
  de fase op `reactie` (×0.5 bij wereld-effect `events_sneller`).
- **VERIFY**: `Verifieer beweging` vergelijkt elke speler zijn netto-vooruit met het
  gerolde getal (doelwit: min/max; niet-doelwit: moet stil staan) → tabel **Controle**.

Besturing (Start/Stop POF, **Manueel**-switch, **Volgende event**-knop,
**Controle**-knop, **Timer**, **Huidig event**, **Controle**-tabel) staat op de
**Bediening**-pagina. De engine draait alleen als `pofActief` AAN staat én
`spelToestand` op `"lopend"`.

> **Controle-knop**: alleen relevant in manueel-modus. Daar telt er geen
> reactietimer af; je drukt zelf **Controle** wanneer de spelers klaar zijn,
> waarna de beweging-controle draait. In automatische modus gebeurt de controle
> vanzelf na de reactietijd.

## Events configureren

Events staan in `global.pofEvents`, gevuld door **drie** config-injects —
`[CONFIG] Speler-events`, `[CONFIG] Uur-events`, `[CONFIG] Wereld-events` — die elk
hun categorie in `pofEvents` mergen (`Sla pofEvents op (merge per categorie)`). Zo
houd je events overzichtelijk per categorie. Schema + uitleg: `docs/events.md`.

## Doelwitkeuze (C2)

`Kies doelwit` ondersteunt drie selectiewijzen (per event ingesteld in `doelwit`):
- `willekeurig` — N willekeurige spelers/uren.
- `alle` — alle spelers/uren.
- `rang` — sorteer op een veld en neem de top N:
  - **speler**: `levensuren` (`totaalUren`) of `achterstand`.
  - **uur**: `nummer` (klokstand) of `bezetting` (aantal spelers op dat uur via
    `spelerLocaties`).

Volledige veldreferentie + voorbeelden: `docs/events.md`.

## Gevolgen (C3)

`Voer gevolg uit` loopt over `event.gevolgen[]` — één event kan dus meerdere
gevolgen hebben. Types (uitbreidbaar: nieuw type = extra tak):

| `type`     | Effect                                                                 |
|------------|------------------------------------------------------------------------|
| `commando` | `{paal, actie}` op `commando/master1` voor elk doel-uur (bij speler-doelwit: hun `huidigePaal`). |
| `score`    | directe wijziging van `totaalUren` van doel-spelers (clamp ≥ 0).      |
| `effect`   | plaatst een **blijvend effect** in een register (zie hieronder).      |

## Blijvende effecten (C4)

Drie registers (global context):

| Register          | Vorm                                                                    | Niveau  |
|-------------------|-------------------------------------------------------------------------|---------|
| `bordStaat`       | `{ <uur>: { effecten: [ {id, effect, naam, resterendeRondes, data} ] } }` | uur   |
| `spelerEffecten`  | `{ <naam>: [ {id, effect, naam, resterendeRondes, data} ] }`            | speler  |
| `wereldEffecten`  | `[ {id, effect, naam, resterendeRondes, data} ]`                        | wereld  |

**Aftellen/aflopen:** `Verouder effecten` draait bij elke nieuwe ronde, verlaagt
`resterendeRondes` van álle effecten met 1 en verwijdert wat ≤ 0 is. Nieuwe
effecten (toegevoegd door `Voer gevolg uit`) starten op hun volle `duurRondes`.

**Afdwinging (twee werkende voorbeelden):**
- speler `mag_niet_bewegen` → `Bereken levensuren` (flow 04) negeert een beweging
  van die speler (geen punten; positie wel bijgewerkt).
- wereld `events_sneller` → `Voer gevolg uit` halveert de reactietijd.
- uur-effecten (bv. `gevaarlijk`) worden voorlopig alleen opgeslagen en getoond.

**Beheer:** `[BEHEER] Wis alle effecten` (inject) leegt de drie registers — handig
bij testen of bij het herstarten van een spelcyclus.

## Audio-abstractie

Audio-verzoeken worden gepubliceerd op MQTT-topic **`audio/afspelen`**
(`{tekst, fase, prioriteit}`). De afspeel-consument op de geluidsbox bestaat nog
niet — zie `docs/todo.md`.

## Visualisatie

Op de **Live Radar**-pagina staat onder de spelerstabel een tabel **Actieve
effecten** (`Niveau | Doel | Effect | Rondes resterend`), gevoed door
`Bouw effecten-tabel` (periodieke refresh + na elke `Verouder effecten`).

## Globale variabelen

| Variabele        | Type    | Gezet door            | Gelezen door                       |
|------------------|---------|-----------------------|------------------------------------|
| `pofEvents`      | array   | 06 (3 config-injects) | 06 Kies event                      |
| `pofActief`      | boolean | 06 (Start/Stop)       | 06 Engine tick                     |
| `pofManueel`     | boolean | 06 (Manueel-switch)   | 06 Engine tick / Verifieer         |
| `pofFase`        | string  | 06 (engine)           | 06 Engine tick                     |
| `pofTeller`      | number  | 06 (engine)           | 06 Engine tick                     |
| `pofHuidigEvent` | object  | 06 Kies event         | 06 dashboard                       |
| `pofVerificatie` | object  | 06 Kies doelwit       | 06 Verifieer                       |
| `pofRegels`      | object  | 06 (ronde/gevolg)     | 04 Bereken levensuren              |
| `bordStaat`      | object  | 06 Voer gevolg/Verouder | 06 Bouw effecten-tabel           |
| `spelerEffecten` | object  | 06 Voer gevolg/Verouder | 06, 04 (mag_niet_bewegen)        |
| `wereldEffecten` | array   | 06 Voer gevolg/Verouder | 06 (events_sneller)              |

## Testen

1. Flow 00 gedraaid + **spel gestart** (flow 03 → `lopend`).
2. **Start POF** op de Bediening-pagina → de **Timer** telt 5→0 af, dan wordt een
   event gekozen, `x` ingevuld, de tekst voorgelezen en de getroffen spelers
   bepaald. Volg dit in de debug-sidebar; abonneer op audio met
   `mosquitto_sub -h 192.168.1.43 -t audio/afspelen`.
3. Na de reactietijd (15→0) toont de tabel **Controle** ✅/❌ per speler.
4. **Manueel**-switch aan → na de controle blijft het op "Wacht…"; met **Volgende
   event** start je het volgende event handmatig.
5. Een `effect`-gevolg verschijnt in de "Actieve effecten"-tabel en telt per ronde
   af. **[BEHEER] Wis alle effecten** leegt de registers.
