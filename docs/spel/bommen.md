# Minigame: Bommen vermijden (nacht)

Een **nacht**-minigame die op het fysieke veld wordt gespeeld: er vormen zich **bommen** op de palen,
en de spelers moeten die ontwijken. Het hele spel is een **gescripte tijdlijn** die synchroon loopt met
één muziektrack (*"MAKI VS THE HEI"*, `audio/muziek/maki_vs_the_hei.wav`, ~83,5 s).

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
- start de **muziek** en plant de **hele tijdlijn** (~66 bom-cues + 15 explosies + de sfeer-golven),
- zet de scan kort (300 ms) voor gladde animaties,
- ruimt bij **Stop** of op het **einde van de track** alles op (LED's uit, scan hersteld, muziek stop).

De **muziek-offset** (dashboard-slider "Muziek-offset (ms)", default 150) compenseert de audio-
opstartlatentie: verhoog/verlaag hem op gehoor tot de LED-sequentie precies op de muziek valt.

## De tijdlijn (samengevat)

Tijden in `[mm:]ss:centiseconden`. **Scorende** bommen (ontploffen → −10):

| tijd | palen | bom |
|---|---|---|
| 7,11 / 13,40 / 15,24 / 17,00 / 20,50 / 22,11 / 23,57 | 7 / 18 / 3 / 14 / 1 / 10 / 21 | bom1 (3,83 s laad + 3,33 s pink) |
| 25,00 | 4&5,15&16,9&10,19&20,2,11,3 (0,23 s versprongen) | bom2-chase → samen pinken 26,30, ontplof 29,94 |
| 26,58 | 11,12,13 | ontplof 29,81 |
| 29,96 | 1,24,23 | ontplof 32,79 |
| 33,68 | oneven-nacht (19,21,23,1,3,5) → **shift +1** → 20,22,24,2,4,6 | ontplof 39,09 (op de verschoven palen) |
| 40,09 | alle palen met cijfer 1/3/6/9 (16 palen) | ontplof 46,27 |
| 66,50 / 68,00 | 1-3 / 6-9 | bom1 |
| 72,75 | willekeurige cluster (onregelmatig pinken) | ontplof 79,05 |

**Sfeer-golven** (geen scoring, best-effort): ~46,9-66 s een zachte, reizende rode golf over alle palen
(actie 16). Puur visueel; de scorende bommen bezitten de LED's buiten dat venster.

De volledige data staat in de Node-RED-inject **`[CONFIG] Bommen-tijdlijn`** (`global.bommenTijdlijn`:
`cmds` = MSG_BOM-sends, `expl` = explosies). Wijzig de tijdlijn daar en draai `deploy-flows`.

## Techniek

- **Firmware:** `MSG_BOM` (0x0B) / `ACTIE_BOM` (25), velden `laad_ms/hold_ms/pink_ms/pink_hz`. Slaves +
  masters herflashen. Zie `docs/protocol.md`.
- **Node-RED:** speltype `"bommen"`; de **"Bommen engine"** (tab 07, op de 250 ms-tick) start/stopt
  zichzelf zoals Klokslag/Infected en plant de tijdlijn met `pofGeneration`-gated `setTimeout`-cues (een
  Stop verhoogt `pofGeneration` → alle cues bailen). Status op retained `bommen/status`.
- **Simulator:** kies "Bommen vermijden" (retained `spel/type`) → het bommen-paneel toont de afteltimer +
  per-speler levensuren (negatief in het rood); de palen tonen de bom-animatie.
