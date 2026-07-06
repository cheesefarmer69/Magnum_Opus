# Avondspel

Het **avondspel** is de tegenhanger van het middagspel (zie `docs/spel/spel.md`). In de middag
**verdien** je levensuren; in de avond kan je ze enkel nog **verliezen** — je overleeft op wat je
opbouwde. In het donker komen de LED's beter tot hun recht; de minigames zijn actiever en visueler.

> **Status (v1).** Geïmplementeerd: de **avond-modus** (omgekeerde scoring, negatief toegestaan),
> de **onmiddellijke-dood** (de start-gimmick) en de **avond/middag-toggle** in de simulator. De
> reguliere avond-events (gekopieerde/aangepaste middag-events) en een formeel "minst negatief"-doel
> komen later. Voorlopig bevat de avond enkel het onmiddellijke-dood-event.

## Avond = een modus op het lopende spel

De avond is **geen** verse start: je neemt de middag-`totaalUren` én -statistieken (`sterftes`,
`valsspeelpunten`) mee. Daarom is het een **modus** (`global.avondModus`) op hetzelfde lopende spel,
niet een aparte `spelType`. Je zet ze aan met de checkbox **"Avondspel"** in de Spelinstellingen van
de simulator (retained topic `sim/avond-modus`, node "Sla avond-modus op"). Stop je het spel eerst
(`transferStats` → globaleStats, `zeroHuidig` nult de huidige stats), dan verlies je de karma-basis
voor de loterij — schakel de avond dus in **zonder** eerst te stoppen.

## Omgekeerde scoring (hoofdspel)

Simpel: **elke verplaatsing kost 1 levensuur per uur** (24 uur = 1 levensdag). Voorbeeld: 4 levensdagen
+ 3 levensuur = 99 uur; een stap van 5 uren → 94 uur = **3 levensdagen + 22 uur**.

- In "Verifieer beweging" wordt in `avondModus` een positieve winst een **kost** (`delta = -delta`),
  en de 0-clamp-met-sterfte wordt overgeslagen: `totaalUren` mag **negatief** worden. Onder 0 spelen
  spelers gewoon door; het **doel** is om **zo min mogelijk negatief** te eindigen.
- Negatieve levensdagen/uren worden getoond met `Math.trunc` (i.p.v. `Math.floor`) zodat het teken klopt.

## Onmiddellijke dood (de start-gimmick)

Bij het begin van de avond valt een dramatisch event: er wordt afgeroepen **"Een speler zal onmiddellijk
sterven"**, en een **paarsrode LED** loopt vloeiend van paal naar paal (lijkt rond het veld te cirkelen)
en **stopt op de paal van het gekozen slachtoffer**, dat ook wordt afgeroepen. Dat slachtoffer verliest
**alles** (uren + dagen → 0) en wordt **`gestorven`**.

- **`gestorven`-effect:** een gestorven speler kan door **verplaatsing** niet negatief gaan (beweging
  floort op 0) — alleen **events** kunnen hem verder omlaag duwen. Hij doet gewoon verder mee.
- **Loterij (karma):** het slachtoffer wordt **geloot** onder de nog niet-gestorven spelers; het aantal
  lootjes per speler = **`sterftes + valsspeelpunten`** (uit het middagspel). Wie veel stierf of vals
  speelde, heeft dus meer kans. **0 lootjes = immuun** (schone spelers zijn veilig); is de som 0 (iedereen
  schoon) → uniform getrokken.

### Implementatie

- **Event** (in `[CONFIG] Wereld-events`): `id: onmiddellijke_dood`, `categorie: wereld`, **`fase: avond`**,
  `doelwit: {type:"geen"}`, `gevolgen: [{type:"onmiddellijke_dood"}]`, `tier: legendary`, `max: 1`.
  Afvuren in de simulator via de **"→ wachtrij"-knop** op de event-kaart (avond-events-tab).
- **"Voer gevolg uit"** herkent het gevolg-type `onmiddellijke_dood` en triggert de node
  **"Onmiddellijke-dood-animatie"** (3e output).
- **"Onmiddellijke-dood-animatie"** doet de loterij, de cirkel-animatie (getimede `setTimeout`-stappen,
  **Stop-veilig** via het `pofGeneration`-token) en bij het einde de kill (`totaalUren=0`, `+1 sterfte`,
  `gestorven=true`) + de naam-afroep. Publiceert per stap **`pof/dood-anim`** `{fase, paal, speler}` voor
  de simulator (de bewegende paarsrode LED + slachtoffer-highlight).

### Hardware-kanttekening

Een **vloeiende** rondlopende LED is op **hardware** niet haalbaar zonder firmware-support: de
commando-FIFO heeft ~1 s ACK-latentie per paal (de slave verwerkt aan het einde van zijn scan-cyclus),
dus een snelle chase zou de FIFO overspoelen. **Nu:** hardware krijgt een **oogst-achtige ring-strobe**
(actie 11 op alle palen) tijdens de animatie en dooft daarna; de smooth cirkel is een **simulator**-effect.
Een echte firmware-chase (een gebroadcaste cursor-positie die elke slave zelf rendert) is een latere
hardware-uitbreiding.

## Event-fase (`fase`-veld)

Events krijgen een optioneel veld **`fase`**: `"middag"` (default), `"avond"` of `"beide"`. Zowel de
engine ("Kies event") als de simulator ("renderEvents") filteren hierop volgens `avondModus`: in de avond
verschijnen enkel `avond`/`beide`-events, in de middag verdwijnt `avond`. Zo kunnen middag- en avond-events
naast elkaar bestaan in dezelfde config-injects.

## Simulator

- **Checkbox "Avondspel"** (Spelinstellingen) → retained `sim/avond-modus` → `global.avondModus`.
- De **events-tab** toont in de avond enkel de avond-events (nu: onmiddellijke dood).
- **`pof/dood-anim`** rendert de cirkelende paarsrode LED (kleur `#b4003c`) + de slachtoffer-strobe.

## Verwant

- `docs/spel/spel.md` (middag-/avondspel-overzicht), `docs/spel/events.md` (`fase` + gevolg-type),
  `docs/invarianten.md` (avond-scoring), `docs/protocol.md` (topics `sim/avond-modus`, `pof/dood-anim`).
