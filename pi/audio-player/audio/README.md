# Audio-bestanden

Deze map wordt als volume in de `audio-player` container gemount op `/app/audio`.
Drop hier je **WAV-bestanden** (mono of stereo, 44.1 kHz aanbevolen). Node-RED
verwijst ernaar via een vaste naamconventie; de Pi speelt de segmenten
sequentieel af over de aux-jack.

## Mapstructuur

```
audio/
├── events/        begin- en eind-segment per event, in een submap per CATEGORIE:
│   ├── verplaatsingen/   speler-events (bv. verplaatsing2_voor.wav)
│   ├── toestanden/       toestand-events (bv. portalen_voor.wav, ziekte_voor.wav)
│   └── wereld-events/    wereld-events (bv. nuke.wav, bomaanslag.wav)
│       (per event: <eventid>_voor.wav en <eventid>_na.wav)
├── getallen/      één bestand per getalwaarde (1..24); gebruikt voor het AANTAL
│   ├── 1.wav        doelwitten, voor event-getallen (stappen, …) ÉN voor een
│   ├── 2.wav        uur-doelwit (paal N → getallen/N.wav)
│   └── ... t/m de hoogste mogelijke waarde (bv. 24.wav)
├── spelers/       één bestand per speler (zie naamregel hieronder)
│   ├── lilou.wav
│   ├── zoe.wav     (speler "Zoë" → accent gestript → zoe.wav)
│   └── ...
├── woorden/       losse verbindingswoorden
│   ├── speler.wav   ("speler")
│   ├── spelers.wav  ("spelers")
│   ├── uur.wav      ("uur")
│   ├── uren.wav     ("uren")
│   └── of.wav       ("of", connector bij twee getallen)
├── groepen/       ALLE groep-audio, gebundeld (zie groepen/README.md)
│   ├── groep.wav / groepen.wav        (de aanroep "een groep" / "groepen")
│   ├── kleur/    kleur.wav + rood/zwart/blauw.wav
│   ├── jaar/     jaar.wav + eerste/tweede/derde.wav
│   ├── maand/    maand.wav + januari..december.wav   (voorbereid)
│   └── seizoen/  seizoen.wav + lente/zomer/herfst/winter.wav   (voorbereid)
├── doelwit/       vaste omkadering rond de doelwit-opsomming
│   ├── voor.wav   ("De volgende doelwitten zijn gekozen:")
│   └── na.wav     ("...dat waren de doelwitten.")
└── sound-effect/  geluidseffecten (countdown, per event-soort, reactietijd)
    └── (zie sound-effect/README.md — verzamelmap, nog niet auto-afgespeeld)
```

## Naamregel voor `spelers/` en `groepen/` (belangrijk!)

Node-RED leidt de bestandsnaam **automatisch** af uit de spelernaam (uit `[CONFIG]
Spelerslijst`) met deze normalisatie — je WAV-bestand moet daar **exact** mee overeenkomen:

1. **accenten/diakrieten strippen** (`Zoë` → `Zoe`)
2. **alles naar kleine letters** (`Zoe` → `zoe`)
3. **elke reeks niet-alfanumerieke tekens → `_`** (spaties en koppeltekens worden `_`)
4. leidende/sluitende `_` weg, en `.wav` erachter

| Speler in de lijst | Bestandsnaam        |
|--------------------|---------------------|
| Lilou              | `spelers/lilou.wav` |
| Zoë                | `spelers/zoe.wav`   |
| Louisa             | `spelers/louisa.wav`|
| Lola               | `spelers/lola.wav`  |
| Maud               | `spelers/maud.wav`  |
| Mien               | `spelers/mien.wav`  |

Klopt de naam niet exact, dan wordt het segment gewoon overgeslagen (logregel
`[AUDIO] Bestand ontbreekt`) en blijft dat doelwit stil. Dezelfde regel geldt voor
`groepen/<waarde>.wav`.

## Hoe een event klinkt (knip-en-plak)

Vóór de event-tekst roept de Pi eerst het **aantal getroffen doelwitten** af, gevolgd
door het zelfstandig naamwoord (enkel/meervoud):
`getallen/<aantal>.wav` → `woorden/<speler|spelers|uur|uren>.wav`

Bij een **groep-event** (`doelwit.type: "groep"`) is de prefix in plaats daarvan
`groepen/groep.wav` ("een groep"), en het doelwit (in de doelwit-fase) is het groep-label:
`groepen/<veld>/<veld>.wav` → `groepen/<veld>/<waarde>.wav`
(bv. `groepen/kleur/kleur.wav` → `groepen/kleur/rood.wav`), omsloten door
`doelwit/voor.wav` … `doelwit/na.wav`. De individuele leden worden niet opgesomd.
**Alle groep-audio staat gebundeld in `groepen/` — zie `groepen/README.md`.**

Daarna de event-tekst zelf (met eventueel getal). De `_voor`/`_na`-bestanden komen uit de
**categorie-submap** (`speler→verplaatsingen`, `toestand→toestanden`, `wereld→wereld-events`):
`events/<categorie>/<id>_voor.wav` → `getallen/<getal>.wav` → `events/<categorie>/<id>_na.wav`

Heeft het event een **tweede getal** (`getal2`, bij `voorwaarde: "of"`), dan komt na het
eerste getal het connector-woord `woorden/of.wav` en het tweede getal:
`events/<categorie>/<id>_voor.wav` → `getallen/<x>.wav` → `woorden/of.wav` → `getallen/<y>.wav` → `events/<categorie>/<id>_na.wav`

Voorbeeld event `verplaatsing2` dat 3 spelers raakt, met getal 3:
> "drie" + "spelers" + "Maximum" + "drie" + "uur."  → *"3 spelers maximum 3 uur."*

Voorbeeld event `of_verplaatsing` dat 2 spelers raakt, met x=2 en y=5:
> "twee" + "spelers" + "twee" + "of" + "vijf" + "uur vooruit."  → *"2 spelers 2 of 5 uur vooruit."*

Raakt het 1 speler: "één" + "speler" + … (enkelvoud).

Bij de doelwitten (zoals voorheen, één voor één):
`doelwit/voor.wav` → (`spelers/lilou.wav` voor een speler, of `getallen/7.wav` voor uur/paal 7) → `doelwit/na.wav`
(een uur-doelwit gebruikt dus dezelfde `getallen/`-opnames; er is geen aparte `uren/`-map meer)

## Een nieuw event van audio voorzien

Geef het event in de Node-RED `[CONFIG]`-inject de velden `audioVoor` en
`audioNa` met **enkel de bestandsnaam** (zonder map), bv.:
```json
{ "id": "verplaatsing1", "categorie": "speler", "audioVoor": "verplaatsing1_voor.wav", "audioNa": "verplaatsing1_na.wav", ... }
```
De engine kiest de **submap automatisch op `categorie`**: `speler→verplaatsingen`,
`toestand→toestanden`, `wereld→wereld-events`. Leg dit voorbeeld dus klaar als
`events/verplaatsingen/verplaatsing1_voor.wav` en `…_na.wav`.

Huidige events die audio verwachten (bestandsnaam → juiste submap):

- **`events/verplaatsingen/`** (speler-events): `verplaatsing2_voor.wav` / `verplaatsing2_na.wav`,
  `of_verplaatsing_voor.wav` / `of_verplaatsing_na.wav` ("uur vooruit"),
  `groep_verplaatsing_voor.wav` / `_na.wav`.
- **`events/toestanden/`** (toestand-events): `portalen_voor.wav` / `portalen_na.wav`,
  `happy_hour_voor.wav` / `happy_hour_na.wav`, `ziekte_voor.wav` / `ziekte_na.wav`
  ("… worden ziek"), `tijdbom_voor.wav` / `tijdbom_na.wav`, `tornado_voor.wav` / `tornado_na.wav`.
- **`events/wereld-events/`** (wereld-events): `nuke.wav` (het woord "NUKE"),
  `sneller_events.wav`, `trager_events.wav`, `bomaanslag.wav`.

De ziekte-waarschuwing (ziekenhuis-monitor + hartslag) speelt op de **slave-buzzer**
(acties 5/6/7), niet via de audio-player.

Verbindingswoorden in `woorden/`: `speler.wav`, `spelers.wav`, `uur.wav`, `uren.wav`,
`of.wav`.

**Alle groep-audio** (de aanroep "groep/groepen" + kleur/jaar/maand/seizoen met hun
waarden) staat gebundeld in `groepen/` — zie **`groepen/README.md`** voor de volledige
checklist. (Ontbrekende WAV's worden gewoon overgeslagen — het event werkt ook zonder audio.)

Ontbrekende bestanden worden gewoon overgeslagen (met een logregel) — de service
blijft draaien.

Zie `docs/handleidingen/audio-player.md` voor het volledige stappenplan.
