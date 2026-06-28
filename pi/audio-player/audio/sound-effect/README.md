# Sound-effects

Map om alle **geluidseffecten** te verzamelen (sfeer/feedback, los van de gesproken
afroep-segmenten in `events/`, `getallen/`, `spelers/`, …).

> Status: **`countdown/countdown.wav` is ingehaakt** — speelt automatisch bij de start van
> elke 5s-aftelklok vóór een event (zie "Engine tick" in Node-RED). De overige mappen
> (per event-soort, reactietijd) zijn nog verzamelmappen en worden nog niet afgespeeld.

## Mapindeling

```
sound-effect/
├── countdown/        countdown.wav speelt bij de 5s-aftelklok vóór elk event  [ACTIEF]
├── verplaatsingen/   effect(en) bij speler-events (verplaatsing)
├── toestanden/       effect(en) bij toestand-events (portalen, happy hour, ziekte, …)
├── wereld-events/    effect(en) bij wereld-events (nuke, bomaanslag, …)
└── reactietijd/      effect(en) tijdens het reactietijd-venster (de denktijd ná het
                      voorlezen, vóór de controle — zie `reactietijd_s` per event)
```

De drie event-soorten volgen dezelfde indeling als `audio/events/`
(`speler→verplaatsingen`, `toestand→toestanden`, `wereld→wereld-events`).

## Conventies

- **WAV**, 44,1 kHz, mono of stereo, 16-bit (zelfde als de rest).
- Bestandsnamen: kleine letters, spaties → `_`.
- Ontbrekende bestanden worden overgeslagen (de audio-player blijft draaien).

## Nog te doen om ze te laten spelen

Deze map is enkel de opslag. Om een effect echt te laten klinken, moet Node-RED een
`audio/afspelen`-bericht sturen met het juiste pad, bv.:

```json
{ "fase": "sfx", "segments": ["sound-effect/countdown/tik.wav"] }
```

Zeg het als ik de koppeling moet maken (bv. countdown bij de aanloop-timer, een
reactietijd-effect tijdens het denkvenster, of een effect per event-categorie).
