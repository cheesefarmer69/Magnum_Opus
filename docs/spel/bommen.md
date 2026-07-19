# Minigame: Bommen vermijden (nacht)

Een **nacht**-minigame die op het fysieke veld wordt gespeeld: er vormen zich **bommen** op de palen,
en de spelers moeten die ontwijken. Het hele spel is een **gescripte tijdlijn** die synchroon loopt met
één muziektrack. Je kiest de track op het dashboard (bommen-groep, dropdown **"Track"**) tussen twee
choreografieën:
- *"YouSeeBIGGIRL" (Attack on Titan / Hiroyuki Sawano, hardstyle)* — `audio/muziek/aot_youseebiggirl.wav`, ~122 s (default);
- *"MAKI VS THE HEI"* — `audio/muziek/maki_vs_the_hei.wav`, ~84 s.

De keuze is **retained** (`bommen/keuze`, waarden `aot`/`maki`) en overleeft dus een herstart.
**Fix juli 2026:** bij een koude start (lege context) kwam de retained keuze binnen vóór de
`[CONFIG]`-inject en werd hij hard naar AoT teruggezet — "ik kies maki maar AoT speelt". De keuze
wordt nu altijd bewaard en toegepast zodra de config geladen is. **Ook gefixt:** een handmatige
**Stop** stopt nu wél de muziek (de engine gebruikt daarvoor zijn eigen vlag `bommenMuziekAan`,
die — anders dan `bommenGestart` — niet door `resetPartij` wordt genuld) én herstelt de scan-duur. Kies vóór
je het spel start; de engine laadt de bijhorende tijdlijn (`global.bommenTijdlijn`) en WAV
(`global.bommenTrack`) op dat moment.

## Hoe een bom eruitziet

Een bom is een paal-LED die:
1. **vloeiend rood opgloeit** (oplaad-ramp, `laad_ms`),
2. zijn **felste punt vasthoudt** (`hold_ms`, voor groeps-bommen die op elkaar wachten),
3. op zijn felst **knippert** (`pink_ms` @ `pink_hz`, meestal 2 Hz),
4. **dooft** — en dat doven **is** de ontploffing.

De ramp bereikt zijn max **exact** wanneer het knipperen begint (structureel gegarandeerd in de
firmware). De slave rendert de hele animatie **lokaal** (`MSG_BOM` / actie 25) zodat ze vloeiend blijft,
ongeacht radio-jitter — Node-RED stuurt enkel de trigger + de tijden. Zie `docs/protocol.md` (§0/§2).

## Scoring

Wie bij het **doven** (de ontploffing) nog op die paal staat, verliest **−10 levensuren** — **per keer**
dat hij geraakt wordt. Dit **mag negatief** worden: er is **geen vloer en geen sterfte**, je blijft
gewoon doorspelen (net als de avondspel-regel, zie `avondspel.md`). De scoring gebeurt in Node-RED (de
"Bommen engine") op de ontplof-tijd, uit `spelerLocaties` — dus de **locatie op het moment van doven**
telt. De helderheid/scan staat tijdens de game op **300 ms** (firmware-minimum) zodat de ramp vloeiend
oogt én de locaties vers genoeg blijven om ontwijkers te volgen.

## Verloop

Selecteer op **Bediening → Speltoestand** het speltype **"Bommen vermijden"** en zet **Spel** aan. De
engine:
- start de **muziek** en plant de **hele tijdlijn** (AoT v2: 152 bom-cues + 86 explosies + de sfeer-golven),
- zet de scan kort (300 ms) voor gladde animaties,
- ruimt bij **Stop** of op het **einde van de track** alles op (LED's uit, scan hersteld, muziek stop).

De **muziek-offset** (dashboard-slider "Muziek-offset (ms)", default 150) compenseert de audio-
opstartlatentie: verhoog/verlaag hem op gehoor tot de LED-sequentie precies op de muziek valt.

## De tijdlijn (AoT — YouSeeBIGGIRL)

Deze tijdlijn is **automatisch uit de MIDI gegenereerd** (noot-exact) met
`tools/beatmap/genereer_uit_midi.py` — **choreografie v2 (juli 2026)**: veel intenser, met
sequenties en combinaties van bom-vormen, maar mét ademruimte. 152 cues / 86 explosies:

| sectie | ~tijd | wat |
|---|---|---|
| intro | 0–20 s | ademruimte: enkele **SLOW-dreads** (7,2 s opladen — dreiging, geen paniek) |
| opbouw | 20–40 s | STD-bommen op de beat + de eerste **CHASE-3**-sequenties (7–9 expl/10 s) |
| **drop** | 40–70 s | **PUNCH**-bommen (1,8 s!), **QUAD**-ring-blasts op de accenten, **MIRROR**-paren (p en p+12) en twee **WALLs** van 5 aaneengesloten palen (11–12 expl/10 s) |
| **breakdown** | 70–82 s | **sfeer-golven** + twee SLOW-dreads die de stilte **overbruggen** en pas op de eerste beats van de finale ontploffen |
| **finale** | 82–113 s | climax: PUNCH-beats, **CHASE-4**, QUAD-accenten en als slot een **MEGA-WALL van 12 palen** |
| outro | 113–122 s | uitademen (niets) |

- **Sequenties** (chases/walls) gebruiken oplopende `hold`-waarden: de leden beginnen ná elkaar te
  laden maar **pinken en ontploffen samen**, exact op één beat — 26 verschillende bom-vormen totaal.
- De generator bewaakt zelf de firmware-limieten: geen twee overlappende bommen op dezelfde paal
  (interval-check + doorschuiven), max 1 pending per paal in het 1,2 s-LEAD-venster.
- **Sfeer-golven** (geen scoring, best-effort): een zachte, reizende rode golf over alle palen (actie 16)
  in de vensters uit **`tl.sfeer`** (voor AoT: 70–82 s). Ontbreekt `tl.sfeer`, dan valt de engine terug
  op het oude vaste venster 46,9–66 s.

**Beide** tijdlijnen (aot + maki) staan in de Node-RED-inject **`[CONFIG] Bommen-tracks`**
(`global.bommenTracks = { aot:{label,track,tijdlijn}, maki:{label,track,tijdlijn} }`; per tijdlijn:
`cmds` = MSG_BOM-sends, `expl` = explosies, optioneel `sfeer` = golf-vensters). De dropdown
**"Track"** zet `global.bommenKeuze` (retained `bommen/keuze`) en de engine past `bommenTijdlijn` +
`bommenTrack` toe. Wijzig een tijdlijn daar en draai `deploy-flows`.

**Nog een nummer toevoegen?** `python tools/beatmap/genereer_uit_midi.py <nummer.mid>` (of
`genereer_tijdlijn.py` voor een WAV zonder MIDI) → voeg een sleutel `{label,track,tijdlijn}` toe aan de
`[CONFIG] Bommen-tracks`-map, zet de WAV in `audio/muziek/`, en voeg de optie toe aan de dropdown
`bommen_track_dd`. De generatoren en de maki-bron staan in `tools/beatmap/`.

## Techniek

- **Firmware:** `MSG_BOM` (0x0B) / `ACTIE_BOM` (25), velden `laad_ms/hold_ms/pink_ms/pink_hz` +
  **`wacht_ms`/`seq`** (v2). Slaves + masters herflashen (volgorde: **eerst slaves, dan masters**,
  dan flows — elke tussenstand gedraagt zich als vanouds). Zie `docs/protocol.md`.
- **Beat-vast (juli 2026):** de engine stuurt elke cue **`LEAD` (1,2 s) vooraf** met `wacht_ms`;
  de master herzendt hem phase-locked (vrij radio-venster) met per poging een **vers herberekende**
  signed rest-wacht tot hij zeker bezorgd is; de slave plant lokaal en ankert de animatie op de
  geplande tijd (`actieStartMs = dueMs`) — een te late bezorging kort de ramp in en het
  **doofmoment blijft op de beat** (±10–30 ms i.p.v. ±300+ ms). Tijdens de scan latcht de slave de
  bom- én golf-animatie gewoon door (show-gate-uitzondering S3b, revert via build-flag
  `BOM_SHOW_TIJDENS_SCAN=0`). De scan blijft op 300 ms draaien — de scoring-verslocatie verandert
  niet. De **Muziek-offset**-slider compenseert nu enkel nog de constante audio-opstartlatentie.
- **Sfeer-golf-wis:** de golf-afsluiting (na elk `tl.sfeer`-venster) stuurt **geen actie 0** (dat
  zou bommen wissen die al gepland/onderweg zijn voor cues vlak ná het venster) maar `actie 16`
  met helderheid 0 (zwart). Stop en het einde-van-de-track gebruiken wél actie 0 — daar is het
  wissen van geplande bommen precies de bedoeling.
- **Node-RED:** speltype `"bommen"`; de **"Bommen engine"** (tab 07, op de 250 ms-tick) start/stopt
  zichzelf zoals Klokslag/Infected en plant de tijdlijn met `pofGeneration`-gated `setTimeout`-cues (een
  Stop verhoogt `pofGeneration` → alle cues bailen). Status op retained `bommen/status`. De tracknaam
  staat in één `const muziekTrack` in de engine. Het `play`-commando wordt bij de start **eenmalig
  ~800 ms later her-bevestigd** (gated met `gOk()`) voor het geval de eerste publish verloren ging of de
  audio-container net (her)verbond; de player negeert dat als de track al speelt (idempotente `play`),
  dus geen hoorbare herstart. Zie `docs/protocol.md` §5 (`audio/muziek`).
- **Simulator:** kies "Bommen vermijden" (retained `spel/type`) → het bommen-paneel toont de afteltimer +
  per-speler levensuren (negatief in het rood); de palen tonen de bom-animatie.
