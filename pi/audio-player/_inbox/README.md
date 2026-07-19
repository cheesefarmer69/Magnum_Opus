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

### Nieuw (juli 2026): storm / bliksem / roulette / gelijke verdeling / middernacht-uitbreiding
Alle hieronder ✗ — de config is al bekabeld, enkel de opnames ontbreken (ontbrekend = stil, nooit fataal).

**Afroepen (audioVoor/Na):**
- ✗ `events/wereld-events/middernacht_uitbreiding.wav`  *("Middernacht zal uitbreiden.")*
- ✗ `events/wereld-events/storm.wav`  *("Een storm trekt over het veld," — daarna klinkt het getal)*
- ✗ `events/wereld-events/uren_groot.wav`  *("uren groot.")*
- ✗ `woorden/klok_mee.wav`  *("met de klok mee.")*
- ✗ `woorden/klok_tegen.wav`  *("tegen de klok in.")*
- ✗ `events/toestanden/gelijke_verdeling.wav`  *("Gelijke verdeling! Druk op de regenboog-knop.")*
- ✗ `events/wereld-events/drukknop_roulette.wav`  *("Drukknop roulette!" — bij het spontane alarm)*

**Eind-cues (afgelopen):**
- ✗ `events/afgelopen/middernacht_uitbreiding_voorbij.wav`  *("Middernacht krimpt terug.")*
- ✗ `events/afgelopen/storm_voorbij.wav`  *("De storm is gaan liggen.")*
- ✗ `events/afgelopen/gelijke_verdeling_uitgevoerd.wav`  *("De levensuren zijn gelijk verdeeld.")*
- ✗ `events/afgelopen/gelijke_verdeling_voorbij.wav`  *("De kans is verkeken." — niemand drukte)*
- ✗ `events/afgelopen/roulette_afgewend.wav`  *("Ramp afgewend.")*
- ✗ `events/afgelopen/roulette_mislukt.wav`  *("Niemand drukte: iedereen verliest tien procent.")*

**Sound-effects:**
- ✗ `sound-effect/wereld-events/storm.wav`  *(wind/onweer-sfeer, sfxReactie tijdens de reactietijd)*
- ✗ `sound-effect/wereld-events/bliksem.wav`  *(donderklap, bij elke bliksem-inslag)*

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
