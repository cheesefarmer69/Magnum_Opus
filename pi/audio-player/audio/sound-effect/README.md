# Sound-effects

Map om alle **geluidseffecten** te verzamelen (sfeer/feedback, los van de gesproken
afroep-segmenten in `events/`, `getallen/`, `spelers/`, …).

> Status: **`countdown/countdown.wav`** speelt automatisch bij de start van elke 5s-aftelklok
> vóór een event (zie "Engine tick" in Node-RED). **`wereld-events/woosh.wav`** wordt door
> "Kies event" vóór élk wereld-event gezet (grote-event-signatuur). Sinds juli 2026 zijn óók de
> **per-event reactietijd-sfx** ingehaakt via het config-veld **`sfxReactie`** (zie onder):
> **`wereld-events/bomaanslag.wav`** (bang), **`toestanden/tornado.wav`** en
> **`toestanden/portalen.wav`**. De map `reactietijd/` blijft een verzamelmap voor
> generieke (niet-event-gebonden) sfeer en wordt nog niet auto-afgespeeld.

## Mapindeling

```
sound-effect/
├── countdown/        countdown.wav speelt bij de 5s-aftelklok vóór elk event   [ACTIEF]
├── verplaatsingen/   effect(en) bij speler-events (verplaatsing)               [via sfxReactie]
├── toestanden/       tornado.wav + portalen.wav (reactietijd-sfx toestand-events) [ACTIEF via sfxReactie]
├── wereld-events/    woosh.wav (elk wereld-event) + bomaanslag.wav (bang)      [ACTIEF]
└── reactietijd/      generieke sfeer tijdens het reactietijd-venster (nog niet auto-afgespeeld)
```

De drie event-soorten volgen dezelfde indeling als `audio/events/`
(`speler→verplaatsingen`, `toestand→toestanden`, `wereld→wereld-events`).

## Reactietijd-sfx per event (`sfxReactie`)

Geef een event in de Node-RED `[CONFIG]`-inject het veld **`sfxReactie`** met **enkel de
bestandsnaam** (zonder map), bv. `"sfxReactie": "tornado.wav"`. "Kies event" hangt dan
`sound-effect/<categorie>/<sfxReactie>` **achteraan de afroep-segmenten** (na de gesproken
opkomst-clip), zodat het effect klinkt zodra het event valt / de reactietijd begint. De submap
volgt de `categorie` (net als bij `audioVoor`): `verplaatsing→verplaatsingen`,
`toestand→toestanden`, `wereld→wereld-events`. Eén-shot per afvuring.

Huidige koppelingen:

| Event | `categorie` | `sfxReactie` | Bestand |
|-------|-------------|--------------|---------|
| bomaanslag | wereld | `bomaanslag.wav` | `sound-effect/wereld-events/bomaanslag.wav` (bang) |
| tornado | toestand | `tornado.wav` | `sound-effect/toestanden/tornado.wav` |
| portalen | toestand | `portalen.wav` | `sound-effect/toestanden/portalen.wav` |

(Een wereld-event krijgt dus zowel de generieke `woosh.wav` vooraan als een eventuele
`sfxReactie` achteraan; bomaanslag = woosh + bang.)

## Conventies

- **WAV**, 44,1 kHz, mono of stereo, 16-bit (zelfde als de rest).
- Bestandsnamen: kleine letters, spaties → `_`.
- Ontbrekende bestanden worden overgeslagen (de audio-player blijft draaien).

## Een nieuw effect laten spelen

Voor een **event-gebonden reactietijd-sfx**: leg de WAV in `sound-effect/<categorie>/` en zet
`sfxReactie` in de `[CONFIG]`-inject van dat event (zie hierboven). Verder geen wiring nodig —
"Kies event" bouwt het pad automatisch.

Voor een **los** `audio/afspelen`-effect (buiten het event-afroep om) stuurt Node-RED een bericht
met het juiste pad, bv.:

```json
{ "fase": "sfx", "segments": ["sound-effect/countdown/countdown.wav"] }
```

Zeg het als er een generiek `reactietijd/`-effect (los van een specifiek event) tijdens het
denkvenster moet spelen — dat is nog niet gekoppeld.
