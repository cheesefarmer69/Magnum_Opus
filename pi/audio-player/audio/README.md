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
│   ├── wereld-events/    wereld-events (bv. nuke.wav, bomaanslag.wav)
│   │   (per event: <eventid>_voor.wav en <eventid>_na.wav)
│   └── afgelopen/        eind-cue per toestand-event (audioAfgelopen): speelt zodra de
│                         duratie verloopt, net vóór het volgende event (bv. portaal_gesloten.wav)
├── getallen/      één bestand per getalwaarde (1..24); gebruikt voor het AANTAL
│   ├── 1.wav        doelwitten, voor event-getallen (stappen, …) ÉN voor een
│   ├── 2.wav        uur-doelwit (paal N → getallen/N.wav)
│   └── ... t/m de hoogste mogelijke waarde (bv. 24.wav)
├── spelers/       één bestand per speler (zie naamregel hieronder)
│   ├── lilou.wav
│   ├── zoe.wav     (speler "Zoë" → accent gestript → zoe.wav)
│   └── ...
├── prefix/        aantal-prefix vóór het event: enkel-/meervoud (zie prefix/README.md)
│   ├── speler.wav / spelers.wav   (speler-doelwit, 1 / meer)
│   ├── uur.wav / uren.wav         (uur-doelwit, 1 / meer)
│   └── groep.wav / groepen.wav    (groep-doelwit)
├── woorden/       losse verbindingswoorden
│   └── of.wav       ("of", connector bij twee getallen)
├── groepen/       ALLE groep-doelwit-clips, gebundeld (zie groepen/README.md)
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
door het zelfstandig naamwoord (enkel/meervoud naar het aantal én het doelwit-type):
`getallen/<aantal>.wav` → `prefix/<speler|spelers|uur|uren>.wav` (zie prefix/README.md)

Bij een **groep-event** (`doelwit.type: "groep"`) is de prefix in plaats daarvan
`prefix/groep.wav` ("groep"), en het doelwit (in de doelwit-fase) is de groep-clip:
`groepen/<veld>/<clip>.wav`
(bv. `groepen/kleur/kleur_rood.wav`), omsloten door
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

De bestandsnaam mag je vrij kiezen (bv. naar de inhoud) zolang de config-velden er exact
naar verwijzen. `audioVoor` = het stuk vóór het getal, `audioNa` = het stuk erna; laat een
veld leeg (`""`) als er geen stuk is (bv. bij events die met het getal beginnen).

Huidige mapping (config → bestand, per submap):

- **`events/verplaatsingen/`** (speler-events): `maximum.wav` (voor) + `uur_vooruit.wav` (na),
  gedeeld door verplaatsing2 / groep_verplaatsing; bij de `of`-events is `audioVoor` leeg en is
  `uur_vooruit.wav` het na-stuk (de connector "of" zit in `woorden/of.wav`).
- **`events/toestanden/`** (toestand-events): `worden_ziek.wav`, `worden_een_tijdbom.wav`,
  `worden_getroffen_door_een_tornado.wav`, `een_portaal_opent_tussen_twee_uren.wav`,
  en (nog op te nemen) `worden_happy_hour.wav`.
- **`events/wereld-events/`** (wereld-events): `events_komen_sneller.wav`,
  `events_komen_trager.wav`, `een_bomaanslag_vind_plaats_op_uur_9_en_11.wav`,
  en (nog op te nemen) `nuke.wav`.

De ziekte-waarschuwing (ziekenhuis-monitor + hartslag) speelt op de **slave-buzzer**
(acties 5/6/7), niet via de audio-player.

De aantal-prefix (`speler`/`spelers`/`uur`/`uren`/`groep`/`groepen`) staat in `prefix/`
(zie `prefix/README.md`); de connector `of.wav` staat in `woorden/`.

**Alle groep-audio** (de aanroep "groep/groepen" + kleur/jaar/maand/seizoen met hun
waarden) staat gebundeld in `groepen/` — zie **`groepen/README.md`** voor de volledige
checklist. (Ontbrekende WAV's worden gewoon overgeslagen — het event werkt ook zonder audio.)

Ontbrekende bestanden worden gewoon overgeslagen (met een logregel) — de service
blijft draaien.

Zie `docs/handleidingen/audio-player.md` voor het volledige stappenplan.
