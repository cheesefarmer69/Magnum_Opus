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
| `[TEST] Commando ...`     | inject-nodes die een ruw commando naar een paal sturen      |
| `commando/master1`        | MQTT-out naar de master                                     |

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
leest `global.status_ok`:

- is die `true`, dan meldt het tekstveld "Spel gestart.";
- is die `false`, dan start het spel toch, maar met de waarschuwing dat de
  spelstatus **niet** OK was. Zo blokkeert het dashboard je niet tijdens het
  testen, maar zie je het risico wel.

## Outputs

| Bestemming            | Beschrijving                                          |
|-----------------------|-------------------------------------------------------|
| `global.spelToestand` | Huidige toestand, gelezen door toekomstige spel-flows.|
| Dashboard "Bediening" | Knoppen + tekstveld met de actuele toestand.          |
| `commando/master1`    | Ruwe testcommando's naar een paal (zie hieronder).    |

## Testcommando's naar de palen

Onderaan de tab staan inject-nodes `[TEST] Commando ...`. Ze sturen een
commando rechtstreeks naar de master op `commando/master1`:

```json
{"paal": 1, "actie": 1}
```

De geldige `actie`-waarden staan in `docs/protocol.md` (0 = uit, 1 = rood,
2 = groen, 3 = buzzer aan, 4 = buzzer uit). Pas de `paal` in de payload aan om
een andere paal te testen. Dit is bewust géén dashboard-knop: het is
ontwikkel-/testgereedschap.

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
7. Testcommando's: klik een `[TEST] Commando`-node aan en controleer dat de
   master het commando ontvangt (LED/buzzer op de betrokken paal).
