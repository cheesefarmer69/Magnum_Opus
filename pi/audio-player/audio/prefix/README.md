# Aantal-prefix

De gesproken **prefix** vóór de event-tekst: het aantal getroffen doelwitten + het juiste
zelfstandig naamwoord (enkelvoud/meervoud), afhankelijk van het doelwit-type én het aantal.

De engine ("Kies event") bouwt:
`getallen/<aantal>.wav` → **`prefix/<woord>.wav`** → … event …

| Doelwit-type | aantal = 1 | aantal > 1 |
|--------------|-----------|------------|
| speler       | `prefix/speler.wav` | `prefix/spelers.wav` |
| uur          | `prefix/uur.wav`    | `prefix/uren.wav`    |
| groep        | `prefix/groep.wav` (geen getal) | `prefix/groepen.wav` (toekomstig, multi-groep) |

Voorbeeld: event "maximum 5 uur" dat **2 spelers** raakt →
`getallen/2.wav` ("twee") → `prefix/spelers.wav` ("spelers") → `events/verplaatsingen/...` →
`getallen/5.wav` ("vijf") → … = **"twee spelers maximum vijf uur"**.

## Etenstijd (wolf) — aparte afroep

Het **etenstijd**-event roept géén groep vooraf af. In plaats daarvan roept "Kies doelwit" af in de
volgorde **"wolf: <naam speler>"** dan **"schaapjes: groep (kleur)"**:
`prefix/wolf.wav` → `spelers/<naam>.wav` → `prefix/schaapjes.wav` → `groepen/kleur/kleur_<kleur>.wav`.

## Op te nemen WAV's

`speler.wav`, `spelers.wav`, `uur.wav`, `uren.wav`, `groep.wav`, `groepen.wav`,
**`wolf.wav`** ("wolf"), **`schaapjes.wav`** ("schaapjes")
(44,1 kHz, mono, 16-bit; ontbrekende bestanden worden gewoon overgeslagen).

> Het connector-woord `of` (bij of-events met twee getallen) blijft in `woorden/of.wav`.
> Bij een **uur-doelwit** wordt elk getal (`getallen/N.wav`) nu **per uur** afgeroepen vanuit de
> doelwit-reveal, zodat het uitgesproken uur samenvalt met de zoemer op die paal (geen bulk-afroep meer).
