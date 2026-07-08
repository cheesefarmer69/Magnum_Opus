# Drukknop-events-modus (ontwerp + compact eventformaat)

> **Status: ontwerp.** De firmware-feedback is klaar (acties **22 `ACTIE_KNOP_GOED`** / **23
> `ACTIE_KNOP_FOUT`**, zie `docs/protocol.md §2`). De **Node-RED-engine** van deze modus wordt pas
> gebouwd zodra (a) Nic de **event-catalogus** aanlevert en (b) de open modus-vragen onderaan beslist zijn.
> Dit document legt het **compacte eventformaat** vast zodat de catalogus daarop kan worden opgesteld.

## Wat het is

Een **aparte spelmodus** (`spelType = "drukknop"`), naar model van Klokslag/Infected: je schakelt ernaartoe
en het gewone Plates-of-Fate-spel **pauzeert** (de PoF-engine ligt stil, net als bij Klokslag). Het spel
draait dan rond de **fysieke drukknoppen** op de knop-palen (`config/drukknoppen`).

## Werking (uit de opdracht)

- Keuze-events verschijnen **alleen op knop-uren** (`config/drukknoppen`, bv. `[3,4,7,9,11,13,15,17,19,21,22]`).
- Een event geeft mee **op hoeveel uren** het zich tegelijk voordoet (`aantalUren`).
- Per uur is er **hoogstens één** geldig drukknop-event tegelijk.
- Een keuze blijft **3 rondes** drukbaar; wordt er niet gedrukt, dan **verdwijnt** ze.
- **Drukken** zet de keuze in gang en laat ze meteen **verdwijnen**.
- Uitkomst-kans: **overdag 50/50**, **'s nachts 60/40** (60 % slecht, 40 % goed).
- **Positief** → paal-LED knippert kort **groen** (actie 22) + **positief** zoemerdeuntje.
  **Slecht** → kort **rood** (actie 23) + **negatief** zoemerdeuntje.
- Een drukknop-event geldt voor **iedereen die op dat moment op dat uur staat**.
- Zijn er **geen vrije knoppen**, dan kan er geen drukknop-event voorkomen.

## Compact eventformaat (voorstel)

Compacter dan het standaard PoF-event; bedoeld voor een eigen `drukknopEvents`-lijst (niet in `pofEvents`).

```json
{
  "id": "voorbeeld",
  "naam": "Voorbeeld",
  "aantalUren": 2,
  "kansGoedDag": 0.5,
  "kansGoedNacht": 0.4,
  "goed":   { "effect": "<nog te definiëren>", "tekst": "..." },
  "slecht": { "effect": "<nog te definiëren>", "tekst": "..." },
  "audioVoor": "<optioneel>.wav"
}
```

| Veld | Betekenis |
|---|---|
| `id` / `naam` | unieke sleutel + weergavenaam |
| `aantalUren` | op hoeveel knop-uren het event tegelijk verschijnt (begrensd door de vrije knoppen) |
| `kansGoedDag` | kans op de **goede** uitkomst overdag (default 0,5 → 50/50) |
| `kansGoedNacht` | kans op de **goede** uitkomst 's nachts (default 0,4 → 60 % slecht) |
| `goed` / `slecht` | de twee uitkomsten; `effect` = wat het doet (uit Nic's lijst), `tekst` = afroep/log |
| `audioVoor` | optionele afroep-clip |

De engine rolt bij een **druk** de uitkomst (`goed`/`slecht`) volgens de dag/nacht-kans, stuurt actie **22/23**
naar die paal, en past `effect` toe op **iedereen op dat uur** op het drukmoment.

## Open modus-vragen (te beslissen vóór de engine-bouw)

1. **Dag vs. nacht**: waaraan gekoppeld? (avondmodus-schakelaar? een tijd? een uurbereik zoals bij de
   tijdbom-ontmanteling `paal 7–18 = dag`?)
2. **Effecten**: wat doen de `goed`/`slecht`-effecten precies — levensuren erbij/eraf, sterfte, een status?
   (komt met Nic's catalogus.)
3. **Scoring**: heeft deze modus **eigen** puntentelling of leunt hij op de PoF-stats (`spelerStats`)?
4. **Win-/eindconditie**: hoe eindigt de modus, en wie "wint"?
5. **Posities**: worden spelerposities in deze modus op dezelfde manier gevolgd als in PoF/Klokslag
   (BLE + locatiebepaling), incl. de overdrive-sensing die Klokslag/Infected gebruiken?

## Zie ook

- Firmware-feedback: `docs/protocol.md §2` (acties 22/23), `firmware/Slave/src/main.cpp`.
- Bestaande knop-infrastructuur: `docs/locatiebepaling.md` (drukknop), `config/drukknoppen`,
  node "Knop-verwerking" in `pi/node-red/flows.json`, drukknop-test-pagina.
- Model voor een aparte modus: `docs/spel/klokslag.md` (spelType, engine, teams, sensing-overdrive).
