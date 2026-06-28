# Flow 06 — Plates of Fate engine

## Doel

De centrale spel-engine van het middagspel (zie `docs/spel/spel.md`). De engine kiest
periodiek een **event**, leest het voor (audio), kiest **wie/wat** het beïnvloedt
en voert de **gevolgen** uit. Sommige gevolgen plaatsen **blijvende effecten** die
over een aantal rondes aflopen.

> Het **event-formaat en hoe je events opstelt** staat in `docs/spel/events.md`.
> Dit document beschrijft de engine zelf.

## Engine-loop (timer-toestandsmachine)

De engine is een toestandsmachine, aangedreven door een **1s-ticker** (`Engine tick`).
Globals: `pofActief`, `pofManueel`, `pofFase` (`idle|aanloop|bezig|reactie|wacht`),
`pofTeller`. Per seconde telt de tick af en stuurt bij 0 de volgende fase.

```
Spel AAN  → start spel + POF (één schakelaar) → aanloop (5s, zichtbaar aftellen)
          → CHOOSE  (Kies event → Verouder effecten → Kies doelwit → Voer gevolg uit)
          → reactie (reactietijd_s, zichtbaar aftellen)
          → VERIFY  (Verifieer beweging → tabel Controle)
          → aanloop … (automatisch)
Manueel:  geen automatische timers. Knop Volgende event → CHOOSE (event + reveal +
          gevolgen) → fase "wacht_controle". ÉÉN druk op Controle verifieert en scoort.
          Daarna → wachten op Volgende event.
Spel UIT  → stop + partij-reset (globale stats blijven) + alle paal-LED's uit.
```

- **CHOOSE** (instant): `Kies event` filtert eerst events weg die hun `max` bereikt
  hebben (max-engine), rolt zo nodig `getal`/`x` (en `getal2`/`y` bij een `voorwaarde:"of"`-event —
  `rol()` accepteert zowel een optie als een `[min,max]`-bereik). Bij `doelwit.type:"groep"` kiest het
  één eigenschap-waarde (`veld` `kleur`/`jaar`, uit `global.spelerEigenschappen`) en richt het event op
  alle actieve spelers met die waarde; de afroep krijgt prefix "een groep" + label `veld: waarde`.
  Daarna **selecteert het meteen de doelwitten**
  (zodat het aantal vooraan in de afroep kan, bv. "3 spelers …") en bouwt de event-audio
  met die aantal-prefix; `Kies doelwit` **snapshot de beginposities** en bouwt de
  doelwit-reveal-audio; `Voer gevolg uit` voert de gevolgen uit en zet de fase op
  `reactie` (×0.5 bij wereld-effect `events_sneller`).
- **VERIFY = scoren (pad-gebaseerd)**: `Verifieer beweging` kent **hier** de levensuren toe
  (niet live), op basis van het **opgenomen pad** `pofPad[speler]` — een geordende reeks hops die
  `Bereken levensuren` (flow 04) tijdens de reactietijd verzamelt. Elke hop is een **STAP**
  (1 vooruit, nooit achteruit) of een **TELEPORT** (sprong tussen twee actieve portaal-palen, 0
  stappen, richting-agnostisch, max 1×/portaal). Géén netto begin/eind-vergelijking. Legaal
  vooruit `+voor` (×2 op happy-hour-eindpaal); te veel/te weinig/achteruit/niet-doelwit-dat-beweegt
  trekt af; onder 0 → 0 + **+1 sterfte**. Volledige spec: `docs/spel/event-systeem.md`. Resultaat →
  tabel **Controle** + globale stats.
- **ZIEKTE**: het gevolg `{type:"ziekte"}` (in `Voer gevolg uit`) maakt de doelwit-spelers ziek
  (`global.ziekeSpelers`) en plaatst `medicijn`-effecten (felroze) op evenveel vrije uren. De node
  **`Ziekte-beheer`** (na `Verifieer beweging`) regelt elke ronde de lifecycle: genezen op een
  medicijn-uur, **medicijn-verbruik** (elk medicijn waar deze ronde iemand genas verdwijnt — felroze LED
  uit, ook bij één genezer), aftellen voor **álle** resterende zieken, **dood** bij 0 (uren 0 + sterfte),
  hartslag-waarschuwing (`commando/master1` acties 5/6/7) vanaf ≤ 3 events, en opschoning van de
  medicijnen als er geen zieken meer zijn. Publiceert `pof/ziekte` voor de simulator. Genezen gebeurt
  enkel bij een **wettelijke** zet (lijst `global.pofGenezen`, gevuld in "Verifieer beweging").
- **TIJDBOM** (`{type:"tijdbom"}`): maakt doelwit-spelers een tikkende tijdbom (`global.tijdbomSpelers`,
  `duratie` events) en kiest evenveel **ontmantel-palen** uit `global.drukknopPalen` (palen mét drukknop) →
  uur-effect `tijdbom` (LED `ACTIE_TIJDBOM` 13). De node **`Knop-verwerking`** (op `plaatjes/data`
  `{paal,knop:1}`, werkt in **elke** fase) ontmantelt een bom-speler op zo'n paal: **dag** (7–18) 80% /
  **nacht** (19–6) 50%. Mislukt het of loopt de klok af (node **`Tijdbom-beheer`**, elke ronde) → iedereen
  op die paal verliest `uur` levensuren. Publiceert `pof/tijdbom`. Knopdrukken zijn ook zichtbaar via `pof/knop`.
- **TORNADO** (`{type:"tornado"}`): één-shot uur-toestand. "Voer gevolg uit" zet `global.tornadoActief`
  (1–2 centers + hun buururen; uur-keuze met `event.minAfstand` 3 zodat ze niet overlappen). "Sync toestanden
  + LEDs" overschrijft de center-LED (`ACTIE_TORNADO` 14) + buururen (`ACTIE_TORNADO_RAND` 15). De **tornado-tak**
  in "Verifieer beweging" dwingt af dat rand-spelers naar het center bewegen — zo niet, **alle** levensuren kwijt
  (geen sterfte, `WEGGEZOGEN`); daarna wist het `tornadoActief` + forceert een LED-rebuild (LED's terug naar origineel).
- **NUKE** (wereld-event `{type:"nuke"}`): "Verifieer beweging" detecteert het en laat **elke nog
  gedetecteerde speler** (`spelerLocaties`) ontploffen (uren 0 + sterfte); wie ontkomen is overleeft.
  Een nuke **wist de wereld**: ze ruimt ook de lopende ziekte-episode (`ziekeSpelers` + medicijn-effecten)
  en alle `dienaars` op; "Ziekte-beheer" (via output 1) herpubliceert dan lege `pof/ziekte`/`pof/dienaars`.
  De groene nuke-lichtshow (actie 8 in "Sync toestanden + LEDs") dekt alle palen **behalve de
  middernacht-poort-paal** (hoogste paal); na de ontploffing forceert `paalLedForceRebuild` elke paal terug
  naar zijn juiste kleur (geen blijvend-groene palen).
  Daarna zet het de fase op **`regroup`** (`regroup_s` s) — de "Engine tick" telt die af en keert daarna
  terug naar `aanloop`. Reset (`Stop`/`Herstart` in "Verwerk bediening") wist `ziekeSpelers`/`pofGenezen`
  en triggert "Ziekte-beheer" om `pof/ziekte` leeg te publiceren.
- **MIDDERNACHT aan/uit**: de simulator-checkbox "Middernacht actief" publiceert (retained) op
  **`sim/middernacht-config`** `{aan: bool}`; de node **`Sla middernachtAan op`** zet `global.middernachtAan`.
  Staat ze uit, dan zet de node **`Middernacht`** `middernachtActief=false` en behandelt de hoogste paal als
  een **gewoon uur** (geen poort-LED, geen oversteek-blokkade in "Verifieer beweging", geen oogst). De
  π-stand (`midnightIndex/Open/Remaining`) blijft bevroren en loopt verder bij heractivering.
  Bij een **dichte** poort is oversteken verboden: wie over de poort heen stapt (de voorwaartse
  hoogste→laagste-paal-wrap) krijgt `MIDDERNACHT DICHT` → **alle levensuren kwijt + 1 sterfte**; tot aan
  de poort lopen zonder oversteken mag wél. De straf wordt toegepast door de **`Middernacht poort-bewaker`**
  (flow 04), die op **elke** settled paalwissel reageert — ook **tussen events** — zolang de poort dicht
  is. "Verifieer beweging" markeert een oversteek nog wel (status `MIDDERNACHT DICHT`, `delta 0`) maar
  straft niet dubbel. **Oogst/dienaars:** bij een 0 in π wordt elke meester hoogstens **één** dienaar
  toegewezen (armste-eerst, willekeurige volgorde bij gelijktijdig oogsten). De admin-knop "Middernacht-klok → start" (topic `reset_klok`, ook in "Reset
  ALLES") zet de π-stand terug naar de startstand en triggert de "Middernacht"-node om te herpubliceren.
- **TEMPO** (`{type:"tempo", richting:"sneller"|"trager"}`, wereld-events `sneller_events`/`trager_events`):
  stapt `global.spelTempoFactor` (sneller −0,1 min 0,6; trager +0,1 max 1,3) — die factor schaalt de
  reactietijd in "Voer gevolg uit". Reset naar 1,0 bij Stop; uitgelezen in `pof/status.spelTempo`.
- **SLECHTE AURA** (spelinstelling): events met `slechteAura: true` (ziekte, tijdbom) kiezen hun
  speler-doelwit in "Kies event" **gewogen** naar regio (avond ×1,10, middernacht ×1,15, dag ×1,00) wanneer
  `global.badAuraAan !== false` (Spelinstellingen-tab → `sim/spel-config`, node "Sla spel-config op").
- **EVENT-TIERS**: elk event heeft een `tier` (gewicht common 50 / uncommon 25 / rare 15 / epic 8 /
  legendary 2). "Kies event" (fallback) en "Bouw pof/status" (wachtrij) kiezen **gewogen**, zodat
  ingrijpende events zeldzaam blijven. Override per event via de events-tab → `sim/tiers-config` →
  `global.eventTiers`.
- **TIJD TERUG**: "Kies event" snapshot de spelstaat (`global.pofSnapshots`); de node **"Tijd terug"**
  (op `sim/tijd-terug`) herstelt de laatste snapshot, herpubliceert de states + `pof/herstel-posities`.
- **DRAMATISCHE ANIMATIE**: "Sync toestanden + LEDs" publiceert nuke/oogst/tornado óók als één retained
  **`pof/animatie`**-bericht (robuust voor de sim; firmware blijft op de per-paal acties via de FIFO).
- **SPELER-TOESTANDEN-TABEL**: het Bediening-dashboard toont ziekte- + tijdbom-spelers met resterende
  rondes (builder gevoed door de 2s-ververs-inject; werkt voor sim én echt spel).
- **MIDDERNACHT gate-block**: een doelwit dat tegen de dichte poort wordt tegengehouden (eindigt op de
  poort-paal) wordt niet bestraft voor te weinig/ongeldige keuze; écht oversteken blijft alle uren + sterfte.
- **SYSTEEMINSTELLINGEN** (simulator → `sim/systeem-config`, node "Sla systeem-config op"):
  `global.toestandExclusief` (default `true`) zorgt dat een event met `exclusiefGroep` (ziekte & tijdbom =
  `"speler-toestand"`) géén speler kiest die al in zo'n toestand zit — afgedwongen in "Kies event"
  (doelwit-keuze) met defensieve dubbelcheck in "Voer gevolg uit". `global.tempoFactor` (default `1`)
  vermenigvuldigt de reactietijd in "Voer gevolg uit".

Besturing staat in de **bovenbalk** (Speltoestand-groep, volle breedte) op de **Bediening**-
én **Simulatie**-pagina: een **Spel-schakelaar** (start/stop, node "Spel aan/uit"), een
**Pauze-schakelaar**, de **Manueel**-schakelaar, en de knoppen **Controle** + **Volgend
event** (enkel actief in manuele modus). De spel-toestand staat onderaan in de balk. De
aparte Start/Stop/Pauzeer/Hervat/Herstart- en Start/Stop POF-knoppen zijn **vervangen** door
deze schakelaars. De engine draait alleen als `pofActief` AAN staat én `spelToestand` op
`"lopend"`. **Sterftes resetten:** knop op de **Admin**-pagina (`reset_sterftes`).

> **Controle-knop**: alleen relevant in manueel-modus. Daar telt er geen
> reactietimer af; je drukt zelf **Controle** wanneer de spelers klaar zijn,
> waarna de beweging-controle draait. In automatische modus gebeurt de controle
> vanzelf na de reactietijd.

## Events configureren

Events staan in `global.pofEvents`, gevuld door **drie** config-injects —
`[CONFIG] Speler-events`, `[CONFIG] Toestand-events`, `[CONFIG] Wereld-events` — die elk
hun categorie in `pofEvents` mergen (`Sla pofEvents op (merge per categorie)`). Zo
houd je events overzichtelijk per categorie. Schema + uitleg: `docs/spel/events.md`.

> **Categorie** is `speler` / `toestand` / `wereld` (de oude waarde `uur` heet nu
> `toestand`). `doelwit.type` mag nog steeds `uur` zijn — dat kiest een uur áls doelwit.

## Doelwitkeuze (C2)

> De selectie gebeurt sinds de aantal-prefix in **`Kies event`** (niet meer in
> `Kies doelwit`), zodat het aantal doelwitten al bekend is op het moment van de afroep.
> `Kies doelwit` gebruikt dat resultaat (`msg.doelwit`) en doet enkel de snapshot +
> reveal-audio.

`Kies event` ondersteunt twee selectiewijzen (per event ingesteld in `doelwit`):
- `willekeurig` — N willekeurige spelers/uren.
- `alle` — alle spelers/uren.

Volledige veldreferentie + voorbeelden: `docs/spel/events.md`.

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

| Register          | Vorm                                                                              | Niveau  |
|-------------------|-----------------------------------------------------------------------------------|---------|
| `bordStaat`       | `{ <uur>: { effecten: [ {id, bron, instId, effect, naam, resterendeRondes, data} ] } }` | uur |
| `spelerEffecten`  | `{ <naam>: [ {id, bron, instId, effect, naam, resterendeRondes, data} ] }`        | speler  |
| `wereldEffecten`  | `[ {id, bron, instId, effect, naam, resterendeRondes, data} ]`                     | wereld  |

`bron` = het event-id; `instId` = één per event-afvuring (gedeeld door alle effecten
van diezelfde afvuring). `duurRondes` mag een getal zijn óf een optie (`kort` 2–4 /
`middel` 4–7 / `lang` 7–12) die `Voer gevolg uit` rolt. Een uur-effect op precies 2
palen krijgt `data.partner` = het andere uur (koppelt bv. de twee portaal-uren).

**Aftellen/aflopen:** `Verouder effecten` draait bij elke nieuwe ronde, verlaagt
`resterendeRondes` van álle effecten met 1 en verwijdert wat ≤ 0 is. Nieuwe
effecten (toegevoegd door `Voer gevolg uit`) starten op hun volle `duurRondes`.

**Max-engine:** een toestand-event met veld `max: N` mag hooguit `N` instanties tegelijk
actief hebben. `Kies event` telt de actieve instanties (distinct `instId` met
`bron === id`) over de drie registers en slaat het event over zolang `max` bereikt is.

**Afdwinging (werkende voorbeelden):**
- speler `mag_niet_bewegen` → `Bereken levensuren` (flow 04) negeert een beweging
  van die speler (geen punten; positie wel bijgewerkt).
- wereld `events_sneller` → `Voer gevolg uit` halveert de reactietijd.
- uur `portaal` → een sprong tussen de twee gekoppelde portaal-uren levert 0 levensuren
  op en telt niet als stap (flow 04). De `Verifieer beweging`-controle is portaal-bewust
  (een legale sprong van hoog naar laag uur geeft geen "TERUG IN TIJD").
- uur `happy_hour` → een verplaatsing die op dat uur eindigt levert dubbele levensuren
  (flow 04).

**LED-toestanden centraal (`Sync toestanden + LEDs`, node `c6a0000000000050`):** deze
node (getriggerd door de 2s-refresh én na `Verouder effecten`) leidt per paal de LED af
uit het actieve uur-effect (`portaal` → actie 1/paars, `happy_hour` → actie 2/goud, anders
0/uit) en stuurt alleen bij wijziging op `commando/master1`. Zo gaan verlopen of gestopte
toestanden vanzelf weer uit. De node publiceert ook **`pof/portalen`** (paren) en
**`pof/toestanden`** (uur-effect-rijen) voor de simulator. Toestand-events hebben dus géén
`commando`-gevolg meer nodig voor hun LED.

**Reset (`resetSpelStaat`):** **Stop spel** (in `Verwerk bediening`) én de twee-staps
**Noodstop** resetten de **partij** — events-teller, alle effect-registers, posities en
snapshots — maar **niet** de globale stats. De Herstart-knop is verwijderd; gebruik
Start/Stop/Pauzeer/Hervat.

**Globale stats (`spelerStats`):** `totaalUren`/levensdagen, `sterftes` en (later) `skills`
**blijven** over Stop heen, gedeeld door sim + echt. Ze staan in een eigen dashboard
**Globale stats** (onder Live Radar, ook op de sim-pagina). De inject **[BEHEER] Wis
globale stats** zet ze handmatig op 0.

**Beheer:** `[BEHEER] Wis alle effecten` (inject) leegt de drie registers — handig
bij testen of bij het herstarten van een spelcyclus.

## Audio-abstractie

Audio-verzoeken worden gepubliceerd op MQTT-topic **`audio/afspelen`**
(`{tekst, fase, segments, prioriteit}`); de **`audio-player`** Pi-service speelt de
WAV-segmenten sequentieel af. De **event-fase** begint met de aantal-prefix
(`getallen/<aantal>` + `woorden/<speler|spelers|uur|uren>`) zodat het aantal getroffen
doelwitten vóór de event-tekst klinkt; de **doelwit-fase** somt de doelwitten één voor
één op. Zie `docs/protocol.md` en `pi/audio-player/audio/README.md`.

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
| `bordStaat`      | object  | 06 Voer gevolg/Verouder | 06 Bouw effecten-tabel / Kies event (max) / Sync toestanden + LEDs, 04 (portaal/happy hour) |
| `spelerEffecten` | object  | 06 Voer gevolg/Verouder | 06, 04 (mag_niet_bewegen)        |
| `wereldEffecten` | array   | 06 Voer gevolg/Verouder | 06 (events_sneller)              |

## Doelen per spel + stats

- **Doel-keuze** (Bediening): `global.pofDoel` (`{type:"verplaats_uur"|"inhalen", x}`),
  `global.pofDoelAantal`, `global.pofAutoEinde`.
- **`verplaatstSpel`** (per-spel, in `spelerStats`) wordt in "Verifieer beweging" opgehoogd met `r.voor`.
  **Doel 2 (inhalen)** wordt daar gelatcht in `spelerStats[naam].doelBereikt` (rivaal = volgende in
  alfabet cyclisch; van lager uur komen, ≥1 voorbij eindigen, passeren door STAP; portaal-voorbij telt
  niet, portaal-terug-dan-lopen wel).
- **Doel-controle** (2s-refresh): bepaalt per speler `doelBereikt`, publiceert `pof/doelstatus`
  (percentage + per-speler) en stuurt bij **auto-einde** een Stop via "Spel aan/uit".
- **Stats per spel → globaal**: bij Stop telt "Spel aan/uit"/"Verwerk noodstop" `spelerStats` op bij
  `globaleStats` (cumulatief) en wist het huidig spel; `spelNummer` telt elke Start. Tabellen **Huidig
  spel** + **Globaal (cumulatief)**.

## Testen

1. Flow 00 gedraaid.
2. **Spel-schakelaar AAN** (bovenbalk) → start spel + POF; de **Timer** telt 5→0 af, dan
   wordt een event gekozen, `x` ingevuld, de tekst voorgelezen en de getroffen spelers
   bepaald. Volg dit in de debug-sidebar; abonneer op audio met
   `mosquitto_sub -h 192.168.1.43 -t audio/afspelen`.
3. Na de reactietijd (15→0) toont de tabel **Controle** ✅/❌ per speler.
4. **Manueel**-switch aan → na de controle blijft het op "Wacht…"; met **Volgende
   event** start je het volgende event handmatig.
5. Een `effect`-gevolg verschijnt in de "Actieve effecten"-tabel en telt per ronde
   af. **[BEHEER] Wis alle effecten** leegt de registers.
