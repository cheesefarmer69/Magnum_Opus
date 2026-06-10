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
