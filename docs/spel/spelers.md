# Spelers

De kolommen **Kleur** (polsbandje: rood/zwart/blauw) en **Jaar** (eerste/tweede/derde) zijn
**groep-dimensies**: een verplaatsing-event kan een hele **groep** spelers aanroepen die een
eigenschap delen (bv. "een groep — kleur: rood"), zodat events minder exclusief zijn bij veel
spelers. Zie `docs/spel/events.md` (doelwit-type `groep`).

Deze tabel is de bron voor `global.spelerEigenschappen` in Node-RED, gevuld door de
`[CONFIG] Speler-eigenschappen`-inject (`{ naam: { kleur, jaar } }`, gekeyd op voornaam).
Bij een wijziging hier ook die inject bijwerken en `deploy-flows.ps1` draaien.

| Voornaam | Kleur | Jaar |
|----------|-------|------|
| Aagje | rood | eerste |
| Alix D | rood | derde |
| Attah | rood | eerste |
| Emma | rood | eerste |
| Blanche | rood | derde |
| Casper | rood | derde |
| Elias | rood | eerste |
| Tobin | rood | eerste |
| Margaux | rood | eerste |
| Louisa | rood | derde |
| Jinte | zwart | eerste |
| Aster | zwart | derde |
| Suzan | zwart | derde |
| Lotta | zwart | derde |
| Elisa | zwart | tweede |
| Maud | zwart | eerste |
| Anna | zwart | derde |
| Lilou | zwart | derde |
| Marie S | zwart | tweede |
| Lore | zwart | derde |
| Marie D | blauw | derde |
| Mauro | blauw | derde |
| Amélie | blauw | derde |
| Mien | blauw | tweede |
| Alix R | blauw | derde |
| Mila | blauw | tweede |
| Ina | blauw | tweede |
| Stelle | blauw | derde |
| Estée | blauw | eerste |
| Lola | blauw | eerste |
| Zoë | blauw | eerste |
