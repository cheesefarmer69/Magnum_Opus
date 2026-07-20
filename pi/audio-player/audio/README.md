# Audio-bestanden

Deze map wordt als volume in de `audio-player` container gemount op `/app/audio`.
Drop hier je **WAV-bestanden** (mono of stereo, 44.1 kHz aanbevolen). Node-RED
verwijst ernaar via een vaste naamconventie; de Pi speelt de segmenten
sequentieel af over de aux-jack.

## Mapstructuur

```
audio/
├── events/        begin- en eind-segment per event, in een submap per CATEGORIE:
│   ├── verplaatsingen/   verplaatsing-events (bv. maximum.wav / uur_vooruit.wav)
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
│   ├── groep.wav / groepen.wav    (groep-doelwit)
│   └── iedereen.wav               (Iedereen-event: vervangt de getal-prefix + naam-opsomming, O1)
├── woorden/       losse verbindingswoorden
│   ├── of.wav       ("of", connector bij twee getallen)
│   └── een.wav      ("een", los tussenwoord voor toekomstige afroepen — nog niet gewired)
├── groepen/       ALLE groep-doelwit-clips, gebundeld (zie groepen/README.md)
│   ├── kleur/    kleur.wav + rood/zwart/blauw.wav
│   ├── jaar/     jaar.wav + eerste/tweede/derde.wav
│   ├── maand/    maand.wav + januari..december.wav
│   ├── seizoen/  seizoen.wav + lente/zomer/herfst/winter.wav
│   └── uur/      even.wav / oneven.wav (pariteit-verplaatsing)
├── doelwit/       vaste omkadering rond de doelwit-opsomming
│   ├── voor.wav   ("De volgende doelwitten zijn gekozen:")
│   └── na.wav     ("...dat waren de doelwitten.")
├── sound-effect/  geluidseffecten (countdown, per event-soort, reactietijd)
│   ├── countdown/countdown.wav      speelt bij de 5s-aftelklok vóór elk event
│   ├── wereld-events/woosh.wav       "groot event komt aan"-sting: speelt ná het aftellen en
│   │                                 vóór de afroep, ENKEL bij categorie "wereld"
│   ├── wereld-events/bomaanslag.wav  reactietijd-sfx (bang) bij het bomaanslag-event   (via sfxReactie)
│   ├── toestanden/tornado.wav        reactietijd-sfx bij het tornado-event            (via sfxReactie)
│   ├── toestanden/portalen.wav       reactietijd-sfx bij het portalen-event           (via sfxReactie)
│   └── (zie sound-effect/README.md — reactietijd-sfx via het config-veld `sfxReactie`)
└── muziek/        LANGE tracks voor het bestuurbare kanaal `audio/muziek` (pauze/hervat/stop)
    ├── reactie_pools.wav   Poolse song (event "De reactietijd wordt Pools")
    └── (de dood-cutscene-track staat in events/wereld-events/onmiddellijke_dood.wav)
```

## Twee afspeelwegen: segment-queue vs. bestuurbaar kanaal

- **`audio/afspelen`** (segment-queue): korte segmenten die **sequentieel** en niet-onderbreekbaar
  spelen (afroepen, getallen, doelwitten). Dit is het gros van de audio.
- **`audio/muziek`** (bestuurbaar kanaal): één **lange** track die je kan **pauzeren/hervatten op
  positie** of **hard stoppen** mid-track — `{"cmd":"play|pause|resume|stop","track":"muziek/…"}`.
  Speelt naast de afroepen (ALSA `default`/dmix mixt). Gebruikt door het Poolse-reactietijd-event
  (muziek tijdens de reactietijd) en de onmiddellijke-dood-cutscene (24 s-track, afgekapt bij de
  landing van de rode lamp). Zie `docs/protocol.md` §5 en `docs/handleidingen/audio-player.md`.

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

Voorbeeld event `groep_verplaatsing` (groep — kleur: rood), met getal 3:
> "een groep" + "Maximum" + "drie" + "uur vooruit." + "kleur rood"  → *"een groep maximum 3 uur vooruit. kleur: rood."*

Voorbeeld event `groep_of_verplaatsing` (groep — jaar: eerste), met x=2 en y=5:
> "een groep" + "twee" + "of" + "vijf" + "uur vooruit." + "eerste jaars"  → *"een groep 2 of 5 uur vooruit. jaar: eerste."*

Bij een **speler**-doelwit event (bv. ziekte) is de prefix het aantal + zelfstandig naamwoord:
"drie" + "spelers" + "worden ziek." (enkelvoud: "één" + "speler" + …). Verplaatsing-events zelf
zijn **groep-only** (geen individuele speler-doelwitten meer).

**Wereld-events — woosh-signatuur:** bij een event met `categorie === "wereld"` speelt **direct ná het
aftellen en vóór de afroep** de sting `sound-effect/wereld-events/woosh.wav` ("er komt een groot event aan").
Andere categorieën krijgen die niet.

**Iedereen-event (`geenDoelwitAfroep:true`, O1):** het `verplaatsing_iedereen`-event richt zich op **alle**
spelers en somt de doelwitten daarom **niet** op. In plaats van de getal-prefix `getallen/<N>.wav` (die bij
31 spelers naar het onbestaande `getallen/31.wav` zou zoeken) speelt één `prefix/iedereen.wav`, en de 31
naam-clips worden **overgeslagen**. Afroep = `prefix/iedereen.wav` → `maximum.wav` → `getallen/<getal>.wav`
→ `uur_vooruit.wav`.

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

- **`events/verplaatsingen/`** (verplaatsing-events): `maximum.wav` (voor) + `uur_vooruit.wav` (na),
  gebruikt door `groep_verplaatsing`; bij de `of`-events (`groep_of_verplaatsing`) is `audioVoor`
  leeg en is `uur_vooruit.wav` het na-stuk (de connector "of" zit in `woorden/of.wav`).
- **`events/toestanden/`** (toestand-events): `worden_ziek.wav`, `worden_een_tijdbom.wav`,
  `worden_getroffen_door_een_tornado.wav`, `een_portaal_opent_tussen_twee_uren.wav`,
  `worden_happy_hour.wav`, `etenstijd.wav` ("een wolf zal jagen op zijn schaapjes"),
  `tweeling.wav` ("2 spelers worden een tweeling"), `body_swap.wav` ("twee spelers wisselen van plaats"),
  en (nog op te nemen) `gelijke_verdeling.wav` ("Gelijke verdeling! Druk op de regenboog-knop.").
- **`events/wereld-events/`** (wereld-events): `events_komen_sneller.wav`,
  `events_komen_trager.wav`, `een_bomaanslag_vind_plaats_op.wav`, `nuke.wav`, `identiteitscrisis.wav`,
  `tijdreizen.wav` ("tijdreizen zal worden toegestaan"), `polonaise.wav` ("de polonaise begint"),
  `max_per_uur.wav` + `spelers_per_uur_staan.wav`, en (nog op te nemen)
  `middernacht_uitbreiding.wav`, `storm.wav` + `uren_groot.wav` (het storm-getal klinkt ertussen;
  de richting komt uit `woorden/klok_mee|klok_tegen.wav`) en `drukknop_roulette.wav`.
  De **bomaanslag** roept de gekozen uren af als getal-segmenten na `een_bomaanslag_vind_plaats_op.wav`;
  die clip is meteen de gesproken waarschuwing vlak vóór de reactietijd van dat event.
- **`events/afgelopen/`** (eind-cue `audioAfgelopen` bij afloop van de duratie):
  `portaal_gesloten.wav`, `happy_hour_voorbij.wav`, `identiteitscrisis_voorbij.wav`,
  `tijdreizen_voorbij.wav`, `max_per_uur_voorbij.wav`, `polonaise_voorbij.wav`, en (nog op te nemen)
  `etenstijd_voorbij.wav` ("de wolf is voldaan").
  Nieuw (nog op te nemen): `middernacht_uitbreiding_voorbij.wav`, `storm_voorbij.wav`,
  `gelijke_verdeling_voorbij.wav`; en buiten `audioAfgelopen` om: `gelijke_verdeling_uitgevoerd.wav`
  (bij een druk), `roulette_afgewend.wav` / `roulette_mislukt.wav` (drukknop roulette) en de
  donderklap `sound-effect/wereld-events/bliksem.wav` + storm-sfx `sound-effect/wereld-events/storm.wav`.
  Buiten het generieke `audioAfgelopen`-pad om spelen: **`alle_zieken_gestorven.wav`**
  ("Alle zieken zijn gestorven." — **Ziekte-beheer**, geen medicijn meer terwijl er zieken zijn,
  invariant Z9), **`tijdbom_ontmanteld.wav`** / **`tijdbom_ontploft.wav`** (**Knop-verwerking** /
  **Tijdbom-beheer**, T4/T5/T6, nog op te nemen), en **`tweeling_verbroken.wav`** ("de tweelingen
  zijn verbroken" — gespeeld bij **elke** tweeling-verbreking: hereniging TW6 én dood-propagatie,
  via de vlag `global.tweelingVerbrokenCue` die "Verouder effecten" op de afgelopen-emissie oppikt;
  tweeling eindigt immers niet op duratie).

> Ontbrekende WAV's zijn **niet fataal**: `player.py` slaat een onbestaand bestand over
> (`[AUDIO] Bestand ontbreekt, overgeslagen`). De **zoemer op de paal** klinkt sowieso.

**Reactietijd-sfx (`sfxReactie`).** Een event kan een sfeer-effect krijgen dat **tijdens de
reactietijd** klinkt (zodra het event valt). Zet in de `[CONFIG]`-inject het veld
`"sfxReactie": "<bestand>.wav"`; "Kies event" hangt dan `sound-effect/<categorie>/<bestand>`
**achteraan de afroep-segmenten**. Huidige koppelingen: bomaanslag → `wereld-events/bomaanslag.wav`
(bang), tornado → `toestanden/tornado.wav`, portalen → `toestanden/portalen.wav`. Een wereld-event
krijgt daarnaast altijd de generieke `sound-effect/wereld-events/woosh.wav` vooraan. Zie
`sound-effect/README.md`.

De ziekte-waarschuwing (ziekenhuis-monitor + hartslag) speelt op de **slave-buzzer**
(acties 5/6/7), niet via de audio-player. Dat geldt ook voor de **ontploffing** (actie 24:
dalende sirene-sweep + rode strobe) en de **ontmantel-feedback** (actie 22: groene flits +
positief deuntje).

De aantal-prefix (`speler`/`spelers`/`uur`/`uren`/`groep`/`groepen`) staat in `prefix/`
(zie `prefix/README.md`); de losse tussenwoorden `of.wav` en `een.wav` staan in `woorden/`
(`een.wav` is nog niet in een afroep gewired — reserve voor een toekomstig event). Nieuw (nog op
te nemen): `klok_mee.wav` ("met de klok mee.") en `klok_tegen.wav` ("tegen de klok in.") — de
richting-afroep van het storm-event.

**Alle groep-audio** (de aanroep "groep/groepen" + kleur/jaar/maand/seizoen met hun
waarden) staat gebundeld in `groepen/` — zie **`groepen/README.md`** voor de volledige
checklist. (Ontbrekende WAV's worden gewoon overgeslagen — het event werkt ook zonder audio.)

Ontbrekende bestanden worden gewoon overgeslagen (met een logregel) — de service
blijft draaien.

Zie `docs/handleidingen/audio-player.md` voor het volledige stappenplan.

---

## Nog op te nemen (stand: juli 2026)

Automatisch afgeleid uit de event-configs **en** de hardgecodeerde verwijzingen in de
function-nodes. Ontbrekende bestanden zijn **niet fataal** — de player slaat ze stil over —
maar op die momenten hoor je dan niets.

Regenereer deze lijst met:
```bash
python tools/audio/ontbrekende_wavs.py
```

### Nieuw (events van juli 2026, tweede batch)
| Bestand | Wanneer je het hoort |
|---|---|
| `events/verplaatsingen/iedereen_een_uur_vooruit.wav` | afroep "Iedereen exact 1 uur vooruit." |
| `events/verplaatsingen/verplaats_een_priemgetal.wav` | afroep "Verplaats een priemgetal naar keuze." |
| `events/wereld-events/bipolair_beestje.wav` | afroep "Een bipolair beestje verschijnt." |
| `events/afgelopen/bipolair_beestje_weg.wav` | het beestje verdwijnt (na 4 humeurwissels) |
| `events/toestanden/dubbel_of_niets.wav` | afroep "Dubbel of niets! Druk op de knop." |
| `events/afgelopen/dubbel_gelukt.wav` | gok gewonnen → levensuren verdubbeld |
| `events/afgelopen/dubbel_mislukt.wav` | gok verloren → alles kwijt |
| `events/afgelopen/dubbel_of_niets_voorbij.wav` | niemand drukte; de knop vervalt |
| `events/toestanden/vijf_of_min_drie.wav` | afroep "Plus vijf of min drie! Druk op de knop." |
| `events/afgelopen/plus_vijf.wav` | gok gewonnen → +5 levensuren |
| `events/afgelopen/min_drie.wav` | gok verloren → −3 levensuren |
| `events/afgelopen/vijf_of_min_drie_voorbij.wav` | niemand drukte; de knop vervalt |

### Systeemcues (incident-pauze / crash-herstel)
`events/spel/gepauzeerd.wav` · `events/spel/hervat.wav` · `events/spel/hersteld.wav`

### Eerder toegevoegde events die nog stil zijn
`events/afgelopen/tijdbom_ontmanteld.wav` · `events/afgelopen/tijdbom_ontploft.wav` ·
`events/afgelopen/alle_zieken_gestorven.wav` · `events/afgelopen/etenstijd_voorbij.wav` ·
`events/toestanden/gelijke_verdeling.wav` · `events/afgelopen/gelijke_verdeling_uitgevoerd.wav` ·
`events/afgelopen/gelijke_verdeling_voorbij.wav` · `events/wereld-events/storm.wav` ·
`events/wereld-events/uren_groot.wav` · `events/afgelopen/storm_voorbij.wav` ·
`sound-effect/wereld-events/storm.wav` · `sound-effect/wereld-events/bliksem.wav` ·
`events/wereld-events/middernacht_uitbreiding.wav` ·
`events/afgelopen/middernacht_uitbreiding_voorbij.wav` ·
`events/wereld-events/drukknop_roulette.wav` · `events/afgelopen/roulette_afgewend.wav` ·
`events/afgelopen/roulette_mislukt.wav`

### Bouwstenen van de afroep (raken élke afroep)
`prefix/iedereen.wav` · `woorden/en.wav` · `doelwit/voor.wav` · `doelwit/na.wav`

> **Prioriteit:** de laatste groep eerst — die vier zitten in de opbouw van veel afroepen. Daarna de
> gok-uitkomsten (`dubbel_gelukt`/`dubbel_mislukt`/`plus_vijf`/`min_drie`), want zonder die clips
> weet een speler niet of hij gewonnen of verloren heeft; de LED-flits (groen/rood) blijft wel werken.
