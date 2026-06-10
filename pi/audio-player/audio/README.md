# Audio-bestanden

Deze map wordt als volume in de `audio-player` container gemount op `/app/audio`.
Drop hier je **WAV-bestanden** (mono of stereo, 44.1 kHz aanbevolen). Node-RED
verwijst ernaar via een vaste naamconventie; de Pi speelt de segmenten
sequentieel af over de aux-jack.

## Mapstructuur

```
audio/
├── events/        begin- en eind-segment per event
│   ├── <eventid>_voor.wav
│   └── <eventid>_na.wav
├── getallen/      één bestand per getalwaarde
│   ├── 1.wav
│   ├── 2.wav
│   └── ... t/m de hoogste mogelijke waarde (bv. 24.wav)
├── spelers/       één bestand per speler (kleine letters, spaties → _)
│   ├── lilou.wav
│   ├── zoe.wav
│   └── ...
├── uren/          één bestand per uur/paal
│   ├── 1.wav
│   └── ... t/m 24.wav
├── woorden/       losse zelfstandige naamwoorden voor de aantal-prefix
│   ├── speler.wav   ("speler")
│   ├── spelers.wav  ("spelers")
│   ├── uur.wav      ("uur")
│   └── uren.wav     ("uren")
└── doelwit/       vaste omkadering rond de doelwit-opsomming
    ├── voor.wav   ("De volgende doelwitten zijn gekozen:")
    └── na.wav     ("...dat waren de doelwitten.")
```

## Hoe een event klinkt (knip-en-plak)

Vóór de event-tekst roept de Pi eerst het **aantal getroffen doelwitten** af, gevolgd
door het zelfstandig naamwoord (enkel/meervoud):
`getallen/<aantal>.wav` → `woorden/<speler|spelers|uur|uren>.wav`

Bij een **groep-event** (`doelwit.type: "groep"`) is de prefix in plaats daarvan
`woorden/een_groep.wav` ("een groep"), en het doelwit (in de doelwit-fase) is het groep-label:
`woorden/<veld>.wav` → `groepen/<waarde>.wav` (bv. `woorden/kleur.wav` → `groepen/rood.wav`),
omsloten door `doelwit/voor.wav` … `doelwit/na.wav`. De individuele leden worden niet opgesomd.

Daarna de event-tekst zelf (met eventueel getal):
`events/<id>_voor.wav` → `getallen/<getal>.wav` → `events/<id>_na.wav`

Heeft het event een **tweede getal** (`getal2`, bij `voorwaarde: "of"`), dan komt na het
eerste getal het connector-woord `woorden/of.wav` en het tweede getal:
`events/<id>_voor.wav` → `getallen/<x>.wav` → `woorden/of.wav` → `getallen/<y>.wav` → `events/<id>_na.wav`

Voorbeeld event `verplaatsing2` dat 3 spelers raakt, met getal 3:
> "drie" + "spelers" + "Maximum" + "drie" + "uur."  → *"3 spelers maximum 3 uur."*

Voorbeeld event `of_verplaatsing` dat 2 spelers raakt, met x=2 en y=5:
> "twee" + "spelers" + "twee" + "of" + "vijf" + "uur vooruit."  → *"2 spelers 2 of 5 uur vooruit."*

Raakt het 1 speler: "één" + "speler" + … (enkelvoud).

Bij de doelwitten (zoals voorheen, één voor één):
`doelwit/voor.wav` → (`spelers/lilou.wav` of `uren/7.wav`, per doelwit) → `doelwit/na.wav`

## Een nieuw event van audio voorzien

Geef het event in de Node-RED `[CONFIG]`-inject de velden `audioVoor` en
`audioNa` met de bestandsnamen, bv.:
```json
{ "id": "verplaatsing1", "audioVoor": "verplaatsing1_voor.wav", "audioNa": "verplaatsing1_na.wav", ... }
```
Leg dan `events/verplaatsing1_voor.wav` en `events/verplaatsing1_na.wav` klaar.

Huidige toestand-events die audio verwachten (in `events/`):
`portalen_voor.wav` / `portalen_na.wav`, `happy_hour_voor.wav` / `happy_hour_na.wav` en
`ziekte_voor.wav` / `ziekte_na.wav` (afroep "… worden ziek") en `nuke.wav` (het woord "NUKE" voor het
wereld-event). De ziekte-waarschuwing (ziekenhuis-monitor + hartslag) speelt op de **slave-buzzer**
(acties 5/6/7), niet via de audio-player.
Verplaatsing-events: `verplaatsing2_voor.wav` / `verplaatsing2_na.wav` en
`of_verplaatsing_voor.wav` / `of_verplaatsing_na.wav` ("uur vooruit").
En de prefix-/connector-woorden in `woorden/`: `speler.wav`, `spelers.wav`, `uur.wav`, `uren.wav`,
`of.wav`, `een_groep.wav`, `kleur.wav`, `jaar.wav`. Groep-waarden in `groepen/`: `rood.wav`,
`zwart.wav`, `blauw.wav`, `eerste.wav`, `tweede.wav`, `derde.wav`. Plus voor het voorbeeld-event
`events/groep_verplaatsing_voor.wav` / `_na.wav`. (Ontbrekende WAV's worden gewoon overgeslagen — het
event werkt ook zonder audio.)

Ontbrekende bestanden worden gewoon overgeslagen (met een logregel) — de service
blijft draaien.

Zie `docs/handleidingen/audio-player.md` voor het volledige stappenplan.
