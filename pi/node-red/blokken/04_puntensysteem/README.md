# Flow 04 — Puntensysteem (Levensjaren)

## Doel

Per speler de **levensuren** en **levensdagen** bijhouden — de score van het
middagspel (zie `docs/spel.md`). Dit blok is de centrale plek voor alle
speler-specifieke spelinformatie.

## De regels (uit `docs/spel.md`)

Het speelveld is een 24-hoekige klok: elke paal stelt één uur voor.

- Elk uur dat een speler **vooruit in de tijd** (met de klok mee) reist
  levert **+1 levensuur** op.
- **24 levensuren = 1 levensdag.**
- **Terug in de tijd** reizen kost **−1 levensuur per uur**. In normale
  omstandigheden mag dit **niet**: achteruit bewegen telt dan gewoon niet mee.
  Het mechanisme bestaat al en wordt per speler aangezet via de vlag
  `tijdTerug` (toekomstige spelmechanismen kunnen die aan/uit zetten).

De teller kan nooit onder 0 zakken (geen negatieve levensuren).

> **Scoren gebeurt alleen tijdens een lopend spel.** `Bereken levensuren`
> controleert `global.spelToestand`: is die niet `"lopend"` (dus `gestopt`
> of `gepauzeerd`), dan wordt de positie wel bijgewerkt maar worden er geen
> levensuren toegekend of afgetrokken. Zo kun je opstellen en testen zonder
> dat de score vervuilt. Start het spel via de **Bediening**-pagina (flow 03).

## Wat de flow doet

| Node                          | Functie                                                                 |
|-------------------------------|-------------------------------------------------------------------------|
| `Beweging van Locatiebepaling`| `link in` — ontvangt paal-wissels van flow 01                           |
| `Bereken levensuren`          | berekent de kortste verplaatsing op de klok en past de score aan        |
| `Stel tijdreizen in`          | zet de `tijdTerug`-vlag aan/uit voor een speler                         |
| `Reset levensjaren`           | zet alle tellers terug op 0 (bij 'Herstart' van een spelcyclus)         |
| `Levensjaren (debug)`         | toont de laatste mutatie in de debug-sidebar én als node-status         |
| `[TEST] …`-injects            | simuleren bewegingen, tijdreizen-toggle en reset zonder hardware        |

## Hoe een beweging binnenkomt

Flow 01 (`Locatiebepaling Spelers`) is de enige plek die met hysteresis bepaalt
wanneer een speler **echt** van paal wisselt. Op het moment van een wissel
stuurt die functie via zijn 2e output een event:

```json
{ "speler": "Lilou", "vanPaal": 3, "naarPaal": 4 }
```

Dat gaat via een `link out` → `link in` naar `Bereken levensuren`. Zo wordt
de hysteresis-logica niet gedupliceerd en kennen ruis-detecties geen punten toe.

## De berekening (kortste weg op de cirkel)

```
vooruit   = ((naarPaal - vanPaal) mod 24 + 24) mod 24   // 0..23
achteruit = 24 - vooruit
```

De speler loopt fysiek langs de ring, dus de **korte weg** is de werkelijke
route. Is `vooruit <= achteruit` → vooruit bewogen (`+vooruit` levensuren),
anders achteruit (`-achteruit`, alleen als `tijdTerug` aan staat). Bij een
gelijke stand (12 vs 12) wordt vooruit gekozen.

> **N = 24, altijd.** De klok heeft 24 uren, ook tijdens het testen met minder
> palen. `AANTAL_UREN` staat bovenaan `Bereken levensuren`.

## Outputs

| Bestemming          | Beschrijving                                                       |
|---------------------|--------------------------------------------------------------------|
| `global.spelerStats`| `{ naam: { totaalUren, tijdTerug, huidigePaal } }` — de bron van waarheid voor de score. |
| Radar-tabel (flow 01)| Leest `spelerStats` en toont kolommen **Levensdagen** en **Levensuren**. |

`totaalUren` is het totaal aantal verzamelde levensuren. De radar toont dit als
`Levensdagen = floor(totaalUren / 24)` en `Levensuren = totaalUren % 24`.

## Afhankelijkheid

- Flow **00** moet de `spelersLijst` gezet hebben (flow 01 vertaalt MAC → naam).
- Flow **01** levert de beweging-events. Zonder flow 01 verandert er niets.
- Flow **03** Bediening zet `global.spelToestand`. Punten tellen alleen mee
  als die op `"lopend"` staat.

## Globale variabelen

| Variabele          | Type                                          | Gezet door | Gelezen door            |
|--------------------|-----------------------------------------------|------------|-------------------------|
| `spelerStats`      | `{ naam: { totaalUren, tijdTerug, huidigePaal } }` | 04    | 04, 01 (radar-tabel)    |

## Testen (zonder hardware)

1. Zorg dat flow 00 gedraaid heeft (spelerslijst geladen).
2. **Start het spel** op de **Bediening**-pagina (anders worden geen punten
   toegekend — je ziet dan de melding "spel is GESTOPT - geen levensuren").
3. Open de tab **04 Puntensysteem**.
4. Klik `[TEST] Lilou vooruit (paal 1->2)` enkele keren → in de debug-sidebar
   zie je `Lilou +1 levensuur(en) (vooruit in de tijd)`; `totaalUren` loopt op.
5. Open dashboard → **Live Radar**: bij Lilou stijgen Levensuren; na 24 klikken
   springt Levensdagen naar 1 en Levensuren terug naar 0.
6. Klik `[TEST] Lilou achteruit (paal 2->1)` → melding "tijdreizen staat UIT -
   geen wijziging"; de teller blijft gelijk.
7. Klik `[TEST] tijdreizen AAN: Lilou`, dan opnieuw achteruit → nu daalt de
   teller met 1 (tot minimaal 0).
8. `[TEST] Reset levensjaren` zet alle tellers op 0.

### Met echte hardware

Loop fysiek met een beacon van de ene paal naar de volgende. Zodra flow 01 de
wissel bevestigt (na hysteresis), verschijnt de mutatie in de debug en stijgt
de score in de radar-tabel.
