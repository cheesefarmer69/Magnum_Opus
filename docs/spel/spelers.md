# Spelers

De kolommen **Kleur** (polsbandje: rood/zwart/blauw), **Jaar** (eerste/tweede/derde),
**Maand** en **Seizoen** zijn **groep-dimensies**: een verplaatsing-event kan een hele
**groep** spelers aanroepen die een eigenschap delen (bv. "een groep — kleur: rood" of
"een groep — seizoen: zomer"), zodat events minder exclusief zijn bij veel spelers.
Zie `docs/spel/events.md` (doelwit-type `groep`). De afroep-audio voor deze dimensies
staat gebundeld in `pi/audio-player/audio/groepen/` (zie die map's `README.md`).

Deze tabel is de bron voor `global.spelerEigenschappen` in Node-RED, gevuld door de
`[CONFIG] Speler-eigenschappen`-inject (`{ naam: { kleur, jaar, maand, seizoen } }`, gekeyd
op voornaam — dezelfde naam als in de spelerslijst/`config/spelers`, zodat een speler in élke
groep-dimensie meedoet). Bij een wijziging hier ook die inject bijwerken en `deploy-flows.ps1` draaien.

> Seizoenen volgen de **meteorologische** indeling: lente (mrt–mei), zomer (jun–aug),
> herfst (sep–nov), winter (dec–feb).

| Voornaam | Kleur | Jaar | Maand | Seizoen |
|----------|-------|------|-------|---------|
| Aagje | rood | eerste | december | winter |
| Alix Blond | rood | derde | juni | zomer |
| Maybel | rood | eerste | december | winter |
| Emma | rood | eerste | maart | lente |
| Blanche | rood | derde | juli | zomer |
| Casper | rood | derde | november | herfst |
| Elias | rood | eerste | maart | lente |
| Tobin | rood | eerste | december | winter |
| Margaux | rood | eerste | december | winter |
| Louisa | rood | derde | maart | lente |
| Jinte | zwart | eerste | februari | winter |
| Aster | zwart | derde | mei | lente |
| Suzan | zwart | derde | februari | winter |
| Lotta | zwart | derde | februari | winter |
| Elisa | zwart | tweede | augustus | zomer |
| Maud | zwart | eerste | augustus | zomer |
| Anna | zwart | derde | januari | winter |
| Lilou | zwart | derde | juli | zomer |
| Marie Smet | zwart | tweede | september | herfst |
| Lore | zwart | derde | februari | winter |
| Marie DM | blauw | derde | maart | lente |
| Mauro | blauw | derde | april | lente |
| Amélie | blauw | derde | februari | winter |
| Mien | blauw | tweede | mei | lente |
| Alix Bruin | blauw | derde | maart | lente |
| Mila | blauw | tweede | december | winter |
| Ina | blauw | tweede | juli | zomer |
| Stelle | blauw | derde | augustus | zomer |
| Estée | blauw | eerste | november | herfst |
| Lola | blauw | eerste | januari | winter |
| Zoë | blauw | eerste | januari | winter |
