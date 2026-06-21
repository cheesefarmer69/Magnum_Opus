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

## Op te nemen WAV's

`speler.wav`, `spelers.wav`, `uur.wav`, `uren.wav`, `groep.wav`, `groepen.wav`
(44,1 kHz, mono, 16-bit; ontbrekende bestanden worden gewoon overgeslagen).

> Het connector-woord `of` (bij of-events met twee getallen) blijft in `woorden/of.wav`.
