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

## Beacon-toewijzing (MAC → speler)

De actuele koppeling van elk baken (BLE-MAC) aan een speler — de bron voor de Node-RED-seed
`[CONFIG] Spelerslijst` (`global.spelersLijst`). De MAC's staan in **kleine letters**, want zo levert de
hardware ze aan; hoofdletters zouden de detectie breken. De namen zijn identiek aan de kolom **Voornaam**
hierboven, zodat elke speler ook zijn groep-eigenschappen krijgt. Wijzigen: pas de `[CONFIG] Spelerslijst`-
inject aan én draai `deploy-flows`, **of** koppel live via het dashboard (**Beacons & Locatie → Spelers /
bakens beheren**; retained op `config/spelers`, wint dan van deze seed).

| Speler | Beacon-MAC |
|--------|------------|
| Aagje | `48:87:2d:9d:ba:a1` |
| Alix Blond | `48:87:2d:9d:c2:31` |
| Alix Bruin | `48:87:2d:9d:ba:a2` |
| Amélie | `48:87:2d:9d:ba:66` |
| Anna | `48:87:2d:9d:bb:79` |
| Aster | `48:87:2d:9d:ba:d7` |
| Blanche | `48:87:2d:9d:cc:ec` |
| Casper | `48:87:2d:9d:ba:f2` |
| Elias | `48:87:2d:9d:ba:d8` |
| Elisa | `48:87:2d:9d:bb:9c` |
| Emma | `48:87:2d:9d:cf:67` |
| Estée | `48:87:2d:9d:bb:8b` |
| Ina | `48:87:2d:9d:ba:ac` |
| Jinte | `48:87:2d:9d:bb:d4` |
| Lilou | `48:87:2d:9d:bb:7d` |
| Lola | `48:87:2d:9d:ba:5f` |
| Lore | `48:87:2d:9d:b9:f2` |
| Lotta | `48:87:2d:9d:bb:97` |
| Louisa | `48:87:2d:9d:ba:cc` |
| Margaux | `48:87:2d:9d:ba:99` |
| Marie DM | `48:87:2d:9d:bb:a6` |
| Marie Smet | `48:87:2d:9d:bb:6f` |
| Maud | `48:87:2d:9d:bb:0b` |
| Mauro | `48:87:2d:9d:bb:a4` |
| Maybel | `48:87:2d:9d:ba:a6` |
| Mien | `48:87:2d:9d:ba:a5` |
| Mila | `48:87:2d:9d:c2:5e` |
| Stelle | `48:87:2d:9d:cf:6b` |
| Suzan | `48:87:2d:9d:ba:51` |
| Tobin | `48:87:2d:9d:bb:96` |
| Zoë | `48:87:2d:9d:ba:5c` |

> **Reserve-baken:** `48:87:2d:9d:ba:b8` ligt apart en staat **niet** in de seed. Nodig op de dag? Koppel
> het via het dashboard aan de te vervangen speler — dan gelden diens eigenschappen automatisch. (Lore
> draagt het baken dat vroeger “reserve 2” heette, `48:87:2d:9d:b9:f2`.)
