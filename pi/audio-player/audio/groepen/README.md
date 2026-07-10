# Groep-audio

Alle WAV-segmenten voor **groep-events** staan hier gebundeld. Een groep-event roept
een hele groep spelers op die één **eigenschap** delen (zie `docs/spel/spelers.md`).
De afroep klinkt als:

> `doelwit/voor.wav` → (`groepen/groep.wav`, optioneel) → … event … →
> **één gecombineerde clip** `groepen/<veld>/<clip>.wav` (bv. "kleur rood" /
> "eerste jaars") → `doelwit/na.wav`

Per dimensie zit het **veld én de waarde in één bestand** (zo klinkt het natuurlijk:
"kleur rood", "eerste jaars"). De engine bouwt de bestandsnaam per veld met een vast
patroon:

| Veld | Patroon | Voorbeelden |
|------|---------|-------------|
| `kleur` | `kleur_<waarde>.wav` | `kleur_rood.wav`, `kleur_zwart.wav`, `kleur_blauw.wav` |
| `jaar` | `<waarde>_jaars.wav` | `eerste_jaars.wav`, `tweede_jaars.wav`, `derde_jaars.wav` |

Naamregel waarde: kleine letters, accenten gestript, spaties → `_`.
Ontbrekende WAV's worden gewoon overgeslagen (de afroep gaat door zonder dat stukje).

## Mapindeling

```
groepen/
├── groep.wav     (optioneel) de aanroep "een groep" / "groep"
├── groepen.wav   (optioneel) meervoud "groepen" — voor toekomstig gebruik
├── kleur/        kleur_rood.wav , kleur_zwart.wav , kleur_blauw.wav
├── jaar/         eerste_jaars.wav , tweede_jaars.wav , derde_jaars.wav
├── maand/        januari.wav .. december.wav
└── seizoen/      lente.wav , zomer.wav , herfst.wav , winter.wav
```

## Welke dimensies gebruikt het spel nu?

- **Actief:** `kleur`, `jaar`, `maand` én `seizoen` — de engine ("Kies event", `ATTR`) kiest bij
  `veld: "willekeurig"` uit alle vier, en "Kies doelwit" speelt de bijbehorende clip af. Bron:
  `[CONFIG] Speler-eigenschappen` in Node-RED (`{ naam: { kleur, jaar, maand, seizoen } }`).
- Daarnaast bestaat het **virtuele** veld `pariteit` (even/oneven **startuur**, uit `spelerLocaties`);
  dat komt niet uit `spelerEigenschappen` en gebruikt de `uur/`-clips (`even.wav` / `oneven.wav`).
- **Identiteitscrisis** verschuift alleen de **luisternaam** en daarmee de **kleur**-groep;
  jaar/maand/seizoen/pariteit blijven van de speler zelf.
- Het **etenstijd**-event zit vast op `veld: "kleur"` en roept dus altijd één kleur-groep af.

## Op te nemen WAV's (checklist — actief)

| Map | Bestanden |
|-----|-----------|
| `groepen/kleur/` | `kleur_rood.wav`, `kleur_zwart.wav`, `kleur_blauw.wav` |
| `groepen/jaar/` | `eerste_jaars.wav`, `tweede_jaars.wav`, `derde_jaars.wav` |
| `groepen/` (optioneel) | `groep.wav` ("een groep") |
