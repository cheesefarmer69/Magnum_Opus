# Flow 06 — Plates of Fate engine

## Doel

De centrale spel-engine van het middagspel (zie `docs/spel.md`). De engine kiest
periodiek een **event**, leest het voor (audio), kiest **wie/wat** het beïnvloedt
en voert de **gevolgen** uit. Sommige gevolgen plaatsen **blijvende effecten** die
over een aantal rondes aflopen.

> Het **event-formaat en hoe je events opstelt** staat in `docs/events.md`.
> Dit document beschrijft de engine zelf.

## Engine-loop

```
Start POF (knop) → pofActief = true → Kies event
Kies event       → guard (pofActief && spel "lopend"); evt. filter op msg.categorie;
                   nieuwe ronde: pofRegels gewist, verplaatstRonde gereset; event gekozen
                   → audio "event voorlezen"  +  dashboard "Huidig event"
Verouder effecten→ tel alle blijvende effecten 1 ronde af; verlopen effecten weg
                   → herbouw "Actieve effecten"-tabel
Kies doelwit     → rang / willekeurig / alle (zie docs/events.md) → audio "doelwit"
Voer gevolg uit  → lus over event.gevolgen[] (commando / score / effect)
                   → commando/master1 + effect-registers
                   → reactietijd (Delay, ×0.5 bij wereld-effect 'events_sneller')
                   → terug naar Kies event
Stop POF (knop)  → pofActief = false (loop stopt bij volgende guard)
```

De besturingsknoppen (Start/Stop POF, Huidig event) staan op de **Bediening**-pagina
van het dashboard (samen met de speltoestand). De engine draait alleen als
`pofActief` AAN staat én `spelToestand` op `"lopend"`.

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
| `pofEvents`      | array   | 06 (config-inject)    | 06 Kies event                      |
| `pofActief`      | boolean | 06 (Start/Stop)       | 06 (loop-guard)                    |
| `pofHuidigEvent` | object  | 06 Kies event         | 06 dashboard                       |
| `pofRegels`      | object  | 06 (ronde/gevolg)     | 04 Bereken levensuren              |
| `bordStaat`      | object  | 06 Voer gevolg/Verouder | 06 Bouw effecten-tabel           |
| `spelerEffecten` | object  | 06 Voer gevolg/Verouder | 06, 04 (mag_niet_bewegen)        |
| `wereldEffecten` | array   | 06 Voer gevolg/Verouder | 06 (events_sneller)              |

## Testen

1. Flow 00 gedraaid + **spel gestart** (flow 03 → `lopend`).
2. Forceer per categorie een event met de injects **[TEST] Speler-event** /
   **[TEST] Uur-event** / **[TEST] Wereld-event** (flow-editor).
3. Volg de keuze in de debug-sidebar; abonneer op audio met
   `mosquitto_sub -h 192.168.1.43 -t audio/afspelen`.
4. Een `effect`-gevolg verschijnt in de "Actieve effecten"-tabel en telt per ronde
   af. `mag_niet_bewegen` → die speler scoort niet bij beweging. `events_sneller`
   → kortere reactietijd. **[BEHEER] Wis alle effecten** leegt de tabel.
5. **Start/Stop POF** op de Bediening-pagina start/stopt de doorlopende lus.
