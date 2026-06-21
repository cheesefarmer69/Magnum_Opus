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
├── maand/        (voorbereid — patroon nog te kiezen, bv. maand_januari.wav)
└── seizoen/      (voorbereid — patroon nog te kiezen, bv. seizoen_lente.wav)
```

## Welke dimensies gebruikt het spel nu?

- **Actief:** `kleur` en `jaar` — de engine ("Kies event") kiest hieruit een groep en
  "Kies doelwit" speelt de bijbehorende gecombineerde clip af. Bron: `[CONFIG]
  Speler-eigenschappen` in Node-RED (`{ naam: { kleur, jaar } }`).
- **Voorbereid (nog niet gekozen):** `maand` en `seizoen` staan in `docs/spel/spelers.md`,
  maar de engine kiest ze nog niet. Om ze te activeren:
  1. voeg `maand` en `seizoen` per speler toe aan `[CONFIG] Speler-eigenschappen`;
  2. neem die velden mee in de veld-keuze in **"Kies event"**;
  3. voeg het bestandsnaam-patroon voor die velden toe in `_GROEPCLIP` in **"Kies doelwit"**
     (bv. `maand: w => "maand_" + w`) en leg de WAV's klaar.

## Op te nemen WAV's (checklist — actief)

| Map | Bestanden |
|-----|-----------|
| `groepen/kleur/` | `kleur_rood.wav`, `kleur_zwart.wav`, `kleur_blauw.wav` |
| `groepen/jaar/` | `eerste_jaars.wav`, `tweede_jaars.wav`, `derde_jaars.wav` |
| `groepen/` (optioneel) | `groep.wav` ("een groep") |
