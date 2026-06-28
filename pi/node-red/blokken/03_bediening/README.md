# Flow 03 — Bediening

## Doel

Een interactief dashboard om het spel manueel te bedienen — zowel tijdens het
testen als tijdens een echt spel. In deze eerste versie beheert het de
**speltoestand**: starten, pauzeren, hervatten en herstarten.

## Wat de flow doet

| Node                      | Functie                                                     |
|---------------------------|-------------------------------------------------------------|
| 4 knoppen                 | `Start spel`, `Pauzeer`, `Hervat`, `Herstart`               |
| `Verwerk bediening`       | past de speltoestand aan volgens de ingedrukte knop         |
| `Huidige speltoestand`    | tekstveld dat de actuele toestand + laatste melding toont   |
| `Toon toestand bij opstart` | inject die de weergave vult bij het starten van Node-RED  |
| Plates of Fate-besturing  | Start/Stop POF + "Huidig event" (flow 06) staan op deze pagina |

> De ruwe `[TEST] Commando ...`-injects en de losse `commando/master1` MQTT-out
> zijn verwijderd; de Plates-of-Fate engine stuurt commando's nu zelf. De
> POF-besturingsgroep is naar deze Bediening-pagina verplaatst.

## Speltype kiezen (Plates of Fate / Klokslag)

In de **Speltoestand**-groep staat een **Speltype**-dropdown (Plates of Fate / Klokslag) plus een
**Actief spel**-uitlezing. De keuze zet `global.spelType` én publiceert retained op **`spel/type`**
— één bron van waarheid, gedeeld met de simulator (die er ook op abonneert en publiceert). De
`spel/type (in)`-mqtt-node synchroniseert omgekeerd een keuze uit de simulator terug naar Node-RED.

Beide engines lezen `global.spelType` en draaien enkel als het hún spel is: **Plates of Fate**
(flow 06) of **Klokslag** (flow 07). De gewone **Spel-schakelaar** start/stopt beide; in
Klokslag-modus laat "Spel aan/uit" de PoF-engine bewust uit (`pofActief = false`). Zie
`pi/node-red/blokken/07_klokslag/README.md`.

## Eén dashboard: monitor of simulatie

De aparte **Simulatie**-pagina is verwijderd. Er is nu **één Bediening-pagina** met een
**monitor/sim-schakelaar** (groep Speltoestand): die publiceert retained `sim/modus` →
"Sim-veld instellen" zet `global.simVeld24` + `palenActief`. De browser-simulator abonneert ook op
`sim/modus`, dus zijn modus-radio schuift mee (en omgekeerd).

Bovenaan staat de **spel-teller** (`Spel #`, `global.spelNummer`, +1 per Start). De **Pauze**-knop
(switch + status-tekst) staat in een eigen groep rechts.

## Stats: Huidig spel vs. Globaal

- **Tabel Huidig spel** (`spelerStats`): levensdagen/uren/sterftes van de **lopende** partij.
- **Tabel Globaal (cumulatief)** (`globaleStats`): som over alle gestopte partijen.
- Bij **Stop** worden de huidig-spel-cijfers opgeteld bij Globaal en wordt het huidig-spel gewist
  (in "Spel aan/uit", "Verwerk noodstop" en de stop-tak van "Verwerk bediening").
- Wissen: Admin "Reset ALLES" of "[BEHEER] Wis globale stats" (wist ook `spelNummer`).

## PoF-doelen (doel + aantal + auto-einde)

In de groep Speltoestand kies je **Doel** (`Verplaats X uur` of `Inhalen (alfabet)`), **Aantal uur
(X)**, **Aantal spelers** dat het doel moet halen, en **Auto-einde bij doel**. De node
**Doel-controle** berekent per speler of het doel bereikt is, publiceert `pof/doelstatus` (voor de
simulator-zijbalk) en stuurt bij auto-einde een Stop. Geslaagde spelers komen in de **historiek**.
Doel-detectie: `verplaatstSpel` (doel 1) wordt opgehoogd in "Verifieer beweging"; doel 2 (inhalen)
wordt daar gelatcht op `spelerStats[naam].doelBereikt`. Zie
`pi/node-red/blokken/06_plates_of_fate/README.md` en `docs/spel/spel.md`.

## De speltoestand

De flow houdt één globale variabele bij: `global.spelToestand`.

| Toestand     | Betekenis                                  |
|--------------|--------------------------------------------|
| `gestopt`    | Beginwaarde; er loopt geen spel.           |
| `lopend`     | Het spel is bezig.                         |
| `gepauzeerd` | Het spel is tijdelijk stilgelegd.          |

De knoppen volgen logische overgangen:

| Knop       | Effect                                                              |
|------------|---------------------------------------------------------------------|
| `Start spel` | `gestopt` → `lopend`. Meldt of `status_ok` op dat moment OK was.   |
| `Pauzeer`  | `lopend` → `gepauzeerd`. Werkt enkel terwijl het spel loopt.         |
| `Hervat`   | `gepauzeerd` → `lopend`. Werkt enkel vanuit pauze.                   |
| `Herstart` | zet de toestand altijd terug op `lopend` (begin van het spel).      |

Toekomstige spel-flows lezen `global.spelToestand` en gedragen zich ernaar
(bv. geen events afspelen terwijl `gepauzeerd`). Die flows bestaan nog niet —
zie `docs/todo.md`.

## Koppeling met de Spelstatus

De Spelstatus-flow (02) moet OK zijn vóór een spel begint. De knop `Start spel`
leest `global.status_ok` en `global.status_override`:

| `status_ok` | `status_override` | Resultaat                                                              |
|-------------|-------------------|------------------------------------------------------------------------|
| `true`      | (n.v.t.)          | Spel start, melding "Spel gestart."                                    |
| `false`     | `false`           | Spel start **NIET**, melding "NO-GO - kan niet starten…"               |
| `false`     | `true`            | Spel start tóch, melding "Spel GESTART met OVERRIDE - spelstatus was NIET OK" |

De override-switch staat in de Spelstatus-pagina van het dashboard. Het is
een bewuste actie van de operator: aanzetten als je weet wat je doet, en
weer uitzetten als je klaar bent. Zolang hij aanstaat verschijnt
`OVERRIDE actief` in de spelstatus-tekst.

## Outputs

| Bestemming            | Beschrijving                                          |
|-----------------------|-------------------------------------------------------|
| `global.spelToestand` | Huidige toestand, gelezen door toekomstige spel-flows.|
| Dashboard "Bediening" | Knoppen + tekstveld + Plates-of-Fate besturing.       |

> De ruwe `[TEST] Commando ...`-injects zijn verwijderd. Wil je los een commando
> naar een paal testen, gebruik dan vanaf de Pi:
> `mosquitto_pub -h 192.168.1.43 -t commando/master1 -m '{"paal":1,"actie":1}'`
> (actie: 0 uit · 1 rood · 2 groen · 3 buzzer aan · 4 buzzer uit, zie `docs/protocol.md`).

## Testen

1. Open dashboard → pagina **Bediening**.
2. Het tekstveld toont bij opstart `Toestand: GESTOPT`.
3. Klik `Start spel` → toestand wordt `LOPEND`. Heb je flow 02 nog niet OK
   gekregen, dan zie je de waarschuwing erbij.
4. Klik `Pauzeer` → `GEPAUZEERD`; `Hervat` → terug `LOPEND`.
5. Probeer `Hervat` terwijl het spel loopt → je krijgt de melding dat hervatten
   enkel vanuit pauze kan. De toestand verandert niet.
6. Controleer in Node-RED via menu (≡) → **Context Data** → **Global** dat
   `spelToestand` mee verandert.
7. Plates of Fate: met **Start POF** start de engine (alleen terwijl het spel
   loopt); **Huidig event** toont het lopende event. **Stop POF** stopt de loop.
