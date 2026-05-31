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
└── doelwit/       vaste omkadering rond de doelwit-opsomming
    ├── voor.wav   ("De volgende doelwitten zijn gekozen:")
    └── na.wav     ("...dat waren de doelwitten.")
```

## Hoe een event klinkt (knip-en-plak)

Bij een event met getal speelt de Pi achter elkaar:
`events/<id>_voor.wav` → `getallen/<getal>.wav` → `events/<id>_na.wav`

Voorbeeld event `verplaatsing1` met getal 3:
> "Minimum" + "drie" + "uur vooruit."

Bij de doelwitten:
`doelwit/voor.wav` → (`spelers/lilou.wav` of `uren/7.wav`, per doelwit) → `doelwit/na.wav`

## Een nieuw event van audio voorzien

Geef het event in de Node-RED `[CONFIG]`-inject de velden `audioVoor` en
`audioNa` met de bestandsnamen, bv.:
```json
{ "id": "verplaatsing1", "audioVoor": "verplaatsing1_voor.wav", "audioNa": "verplaatsing1_na.wav", ... }
```
Leg dan `events/verplaatsing1_voor.wav` en `events/verplaatsing1_na.wav` klaar.

Ontbrekende bestanden worden gewoon overgeslagen (met een logregel) — de service
blijft draaien.

Zie `docs/handleidingen/audio-player.md` voor het volledige stappenplan.
