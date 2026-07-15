# _inbox — audio dropzone (staging)

Leg hier je **nieuwe WAV-bestanden** neer die nog niet in de audio-mappenstructuur staan.
Claude haalt ze hieruit, hernoemt ze naar de juiste naam en verplaatst ze naar de correcte
`audio/events/<categorie>/`-submap, en koppelt ze aan de Node-RED-config.

- **Naam maakt niet uit** bij het droppen — dump gerust `opname1.wav` e.d. Bij een opake naam:
  zeg er kort bij welk event/afroep het is. Herkenbare namen (bv. `polonaise voorbij.wav`) worden
  automatisch gemapt.
- Deze map ligt **buiten** de Docker-volume-mount (`audio/`), dus losse bestanden hier belanden
  nooit per ongeluk in de container.
- De inhoud is **gitignored** (behalve deze README) — ruwe drops worden niet gecommit. Pas na
  plaatsing in `audio/events/…` worden de bestanden versiebeheerd.
- Formaat: WAV, mono of stereo, 44,1 kHz aanbevolen.

## Wat er nog nodig is (✗ = ontbreekt, ✓ = al aanwezig)

Bron: de `[CONFIG]`-injects in `pi/node-red/flows.json`. De config is al bekabeld — enkel de
bestanden ontbreken nog.

### `events/toestanden/` — event-afroep (audioVoor)
- ✓ `een_portaal_opent_tussen_twee_uren.wav`, `worden_ziek.wav`, `worden_een_tijdbom.wav`,
  `worden_getroffen_door_een_tornado.wav`
- ✓ `worden_happy_hour.wav`, `etenstijd.wav`, `tweeling.wav`, `body_swap.wav`

### `events/wereld-events/` — event-afroep (audioVoor)
- ✓ `events_komen_sneller.wav`, `events_komen_trager.wav`, `een_bomaanslag_vind_plaats_op.wav`
- ✓ `nuke.wav`, `identiteitscrisis.wav`, `tijdreizen.wav`, `onmiddellijke_dood.wav`,
  `max_per_uur.wav` + `spelers_per_uur_staan.wav`, `polonaise.wav`

### `events/afgelopen/` — "toestand voorbij"-cue (audioAfgelopen)
Speelt automatisch, vlak vóór het volgende event, wanneer de toestand afloopt.
- ✓ `portaal_gesloten.wav`, `happy_hour_voorbij.wav`, `identiteitscrisis_voorbij.wav`,
  `tijdreizen_voorbij.wav`, `max_per_uur_voorbij.wav`, `polonaise_voorbij.wav`
- ✓ `tweeling_verbroken.wav` *(niet op duratie — gespeeld bij elke tweeling-verbreking via
  `global.tweelingVerbrokenCue`)*
- ✗ `etenstijd_voorbij.wav`  *("de wolf is voldaan")*

### `sound-effect/` — reactietijd-sfx (`sfxReactie`) + woosh
- ✓ `wereld-events/woosh.wav` (elk wereld-event), `wereld-events/bomaanslag.wav` (bang),
  `toestanden/tornado.wav`, `toestanden/portalen.wav`

### `woorden/`
- ✓ `een.wav` *(los tussenwoord — nog niet in een afroep gewired)*

> **Geen afgelopen-cue nodig** voor tornado/bodyswap (één-shot) en ziekte/tijdbom
> (eindigen op een dood, niet op duratie). Verplaatsing-events gebruiken `maximum.wav` +
> `uur_vooruit.wav`. **Tweeling** heeft nu wél een cue (`tweeling_verbroken.wav`), maar niet via
> `audioAfgelopen` — zie boven.

Volledige naamregels en afspeel-volgorde: zie [`../audio/README.md`](../audio/README.md).
