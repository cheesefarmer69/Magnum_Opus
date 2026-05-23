# Node-RED blokken — architectuur

De Pi is het brein van Magnum Opus. Alle spellogica draait in Node-RED,
opgedeeld in **blokken**. Deze map beschrijft hoe die blokken georganiseerd zijn.

## Wat is een blok?

Een blok = één Node-RED **tab** (flow) met één duidelijke verantwoordelijkheid.
Elk blok kan apart ontwikkeld en getest worden. Pas later wordt beslist welke
blokken in serie of parallel draaien.

Voordeel: je kent de volledige spelflow nog niet, maar je kan wel per blok
vastleggen aan welke functies het moet voldoen, en het geïsoleerd bouwen en
testen.

## Bron van waarheid

`pi/node-red/flows.json` is het **enige** bestand met de echte flows. Dat is wat
je in Node-RED importeert/deployt en wat je als backup exporteert.

Elke map hieronder bevat een `README.md` — de **handleiding** van dat blok
(wat het doet, hoe je het gebruikt, hoe je test). Er staan bewust geen losse
flow-exports per blok: twee kopieën van dezelfde flow raken onvermijdelijk uit
sync.

```
pi/node-red/
├── flows.json                    # alle flows samen — bron van waarheid
└── blokken/
    ├── README.md                 # dit bestand
    ├── 00_configuratie/README.md
    ├── 01_locatiebepaling/README.md
    ├── 02_spelstatus/README.md
    ├── 03_bediening/README.md
    ├── 04_puntensysteem/README.md
    └── 05_admin/README.md
```

## De flows

| #  | Flow            | Verantwoordelijkheid                                                  |
|----|-----------------|-----------------------------------------------------------------------|
| 00 | Configuratie    | Eén centrale plek voor de spelers- en paaltjeslijst.                  |
| 01 | Locatiebepaling | Bepaalt op welke paal elke speler zich bevindt.                       |
| 02 | Spelstatus      | Toont de gezondheid van het spel; moet OK zijn vóór de start.         |
| 03 | Bediening       | Interactief dashboard om het spel te starten/pauzeren/herstarten.     |
| 04 | Puntensysteem   | Houdt levensuren/levensdagen per speler bij (score middagspel).       |
| 05 | Admin           | Beheerpaneel: levensjaren resetten achter twee-staps verificatie.     |

## Communicatie tussen blokken

Blokken wisselen op drie manieren gegevens uit:

1. **Global context** — gedeelde toestand via `global.get` / `global.set`.
   Losjes gekoppeld: een blok leest wat het nodig heeft zonder de andere
   blokken te kennen. Dit is de hoofdmanier.
2. **MQTT** — voor communicatie met de hardware (master/slaves), via de
   bestaande topics `plaatjes/data` en `commando/master1`. Zie `docs/protocol.md`.
3. **Link nodes** — directe Node-RED-berichten tussen tabs (`link out` →
   `link in`), als je een blok expliciet door een ander wil laten triggeren.

### Overzicht global-variabelen

| Variabele             | Type              | Gezet door         | Gelezen door                       |
|-----------------------|-------------------|--------------------|------------------------------------|
| `spelersLijst`        | `{mac: naam}`     | 00 Configuratie    | 01 Locatiebepaling, 02 Spelstatus  |
| `paaltjesLijst`       | `[id, ...]`       | 00 Configuratie    | 02 Spelstatus                      |
| `spelerLocaties`      | `{naam: paalId}`  | 01 Locatiebepaling | 04 Puntensysteem, toekomstige flows |
| `spelerStats`         | `{naam: {totaalUren, tijdTerug, huidigePaal}}` | 04 Puntensysteem | 04 Puntensysteem, 05 Admin, 01 (radar-tabel) |
| `admin_unlocked`      | `boolean`         | 05 Admin           | 05 Admin                           |
| `status_lastSeenMac`  | `{mac: ts}`       | 02 Spelstatus      | 02 Spelstatus                      |
| `status_lastSeenPaal` | `{id: ts}`        | 02 Spelstatus      | 02 Spelstatus                      |
| `status_lastDataTs`   | `ts`              | 02 Spelstatus      | 02 Spelstatus                      |
| `status_ok`           | `boolean`         | 02 Spelstatus      | 03 Bediening                       |
| `status_fouten`       | `[{Code, ...}]`   | 02 Spelstatus      | toekomstig error-blok              |
| `spelToestand`        | `string`          | 03 Bediening       | 04 Puntensysteem, toekomstige flows |

## Serie vs. parallel

- **Parallel** — blokken draaien onafhankelijk; geen `link`-nodes ertussen.
  Elk blok reageert op zijn eigen triggers (MQTT-input, timers, knoppen).
  Zo draaien de huidige vier flows.
- **Serie** — de uitgang van blok A gaat via een `link out`-node naar de
  `link in`-node van blok B. Zo bepaal je een volgorde zonder de blokken zelf
  te wijzigen.

Omdat blokken via global context losjes gekoppeld zijn, kan je de
serie/parallel-keuze later maken zonder de interne logica aan te passen.

## Volgorde bij opstarten

`00 Configuratie` moet als eerste draaien — die zet `spelersLijst` en
`paaltjesLijst` klaar waar de andere flows op steunen. De config-inject-nodes
vuren automatisch bij elke deploy/herstart van Node-RED.
