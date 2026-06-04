# woorden/

Losse zelfstandige naamwoorden voor de **aantal-prefix** die vóór elke event-tekst
wordt afgeroepen (bv. *"3 spelers maximum 3 uur"*). Node-RED kiest enkel- of meervoud
op basis van het aantal getroffen doelwitten.

Leg hier de volgende WAV's klaar (mono/stereo, 44.1 kHz):

| Bestand | Spreek in |
|---------|-----------|
| `speler.wav`  | "speler"  |
| `spelers.wav` | "spelers" |
| `uur.wav`     | "uur"     |
| `uren.wav`    | "uren"    |

Ontbrekende bestanden worden door de audio-player overgeslagen (met een logregel),
dus de afroep blijft werken zolang ze nog niet opgenomen zijn — enkel het woordje valt
dan weg.
