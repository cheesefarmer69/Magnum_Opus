# beatmap — LED-tijdlijn genereren uit muziek (proof-of-concept)

Genereert automatisch een **bommen-tijdlijn** (`global.bommenTijdlijn`, zie
[`docs/spel/bommen.md`](../../docs/spel/bommen.md)) uit een muziek-WAV, in plaats van elke cue met de
hand op centiseconden te timen.

## Waarom / hoe

Claude kan geen audio *horen*, maar een WAV is gewoon PCM-data (getallen). Met signaalanalyse haal je
de muzikale structuur er rechtstreeks uit:

- **Onset-detectie** (spectral flux, numpy-FFT) → de aanslagmomenten waarop iets "knalt".
- **Tempo-schatting** (autocorrelatie van de onset-envelope) → grove BPM.
- **RMS-energie** → rustige vs. drukke stukken (kandidaat-sfeer-golven).

Elke sterke hit wordt een **explosie-moment** `t`; de bom start `som/1000` s eerder
(`laad 3830 + hold 0 + pink 3330 = 7160 ms`) zodat hij **precies op die hit dooft** — exact hoe de
minigame scoort. Output is exact het bestaande schema:

```json
{ "cmds": [ {"send":7.11,"paal":7,"laad":3830,"hold":0,"pink":3330,"hz":2}, … ],
  "expl": [ {"t":14.27,"palen":[7]}, … ],
  "duur": 83.56 }
```

Er is **geen** audio→MIDI-omweg nodig: voor LED-op-de-beat heb je geen toonhoogtes nodig, wel
beats/onsets/energie — en die zijn robuuster rechtstreeks uit de WAV te halen. (Wil je tóch MIDI:
`basic-pitch` + MuseScore → MusicXML, maar dat is voor dit doel niet de moeite.)

## Installeren & draaien

Dev-PC, niet de Pi:

```bash
pip install -r tools/beatmap/requirements.txt      # enkel numpy

# genereer een tijdlijn (schrijft out/<naam>_generated.json)
python tools/beatmap/genereer_tijdlijn.py pi/audio-player/audio/muziek/maki_vs_the_hei.wav
python tools/beatmap/genereer_tijdlijn.py <jouw.wav> --dichtheid laag|midden|hoog

# vergelijk met de handmatige tijdlijn (ground truth uit flows.json)
python tools/beatmap/vergelijk.py
```

Plak de inhoud van de gegenereerde JSON in de Node-RED-inject **`[CONFIG] Bommen-tijdlijn`**, draai
`deploy-flows`, test in de simulator en stel de **Muziek-offset**-slider bij tot de LED's op de beat
vallen. (Doe dit op een **test**-kopie; de tool raakt `flows.json` zelf niet aan.)

## PoC-resultaat (maki_vs_the_hei.wav)

Gemeten tegen Nic's handmatige tijdlijn (15 explosies):

| vraag | resultaat |
|---|---|
| **Ziet de analyse de handmatige hits?** | **15/15 binnen ±150 ms** (gem. afwijking 59 ms) |
| Dekt de auto-gegenereerde selectie diezelfde hits? | 4/15 (27%) |

Oftewel: de analyse **detecteert elke** muzikale hit die Nic op gehoor koos, zeer precies. Maar het
**kiezen** van wélke hits bommen worden (op pure spectral-flux-sterkte) valt anders uit dan het
menselijk oor — meer dichtheid helpt daar nauwelijks aan (33% bij "hoog"). Dat is de kern van deze PoC.

## De eerlijke grens

- **Mechanisch-op-de-beat: ✅** — de tool timt cues betrouwbaar op de muziek.
- **Muzikale saillantie & dramatiek: mensenwerk** — welke hit een grote groeps-explosie wordt, en
  welk **paalpatroon** (draaiend/gespiegeld/groepen) "goed voelt", kiest de mens. De paal-toewijzing
  hier is een simpele roterende baseline, geen choreografie.

## Vervolg (niet in deze PoC)

- **Beat-grid-snapping / downbeat-detectie** i.p.v. pure flux-sterkte → selecteert musicaal
  belangrijker momenten (zou de dekking-score fors verbeteren).
- Keuze-heuristieken (dichtheid-slider, paalpatronen, presets per sfeer), sfeer-golven mee genereren,
  en een export die het `bommenTijdlijn`-blok direct schrijft. Werkt dan voor élke nieuwe track.

## Bestanden

- `genereer_tijdlijn.py` — analyse + generatie.
- `vergelijk.py` — match-rapport tegen de ground-truth in `flows.json`.
- `out/` — gegenereerde JSON (git-genegeerd houden indien gewenst).
