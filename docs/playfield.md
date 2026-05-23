# Magnum Opus — Speelveld Geometrie

Dit document beschrijft de fysieke layout van het Magnum Opus speelveld, zodat
tooling (incl. Claude Code) de ruimtelijke opstelling van de palen ("palen") en
de touw-/spaakstructuur correct kan interpreteren.

## Overzicht

Het speelveld is een **regelmatige ring** opgebouwd uit twee concentrische
24-hoeken (polygonen) met daartussen radiale "spaken". Op de buitenste polygoon
staan de paaltjes. De ring benadert een cirkel, maar bestaat uit rechte
touwsegmenten tussen de palen.

```
            buitenpolygoon (24 zijden)  ← paaltjes staan hier
          ┌───────────────────────────┐
          │   spaak   spaak   spaak    │   radiale verbinding
          │ ┌───────────────────────┐ │
          │ │  binnenpolygoon (24)   │ │
          │ │                        │ │
          │ │      open midden       │ │
          │ │                        │ │
          │ └───────────────────────┘ │
          └───────────────────────────┘
```

## Kernafmetingen

| Parameter | Waarde |
|---|---|
| Buitenste straal | 11.50 m |
| Binnenste straal | 8.00 m |
| Diameter buitencirkel | 23.00 m |
| Aantal zijden per polygoon | 24 |
| Aantal paaltjes | 24 |
| Aantal spaken | 24 |
| Hoek per segment | 15° (= 360° / 24) |

## Polygoon-zijden

| Element | Zijdelengte | Totale lengte (24 zijden) |
|---|---|---|
| Buitenpolygoon | 3.002 m | 72.050 m |
| Binnenpolygoon | 2.088 m | 50.122 m |

- **Koorde buurpalen** (afstand tussen twee naburige paaltjes op de buitenring):
  3.002 m
- **Afwijking t.o.v. cirkel**: de rechte koorde is 0.29% korter dan de
  bijbehorende cirkelboog — de polygoon-benadering is dus zeer dicht bij een
  echte cirkel.

## Spaken

Spaken zijn de radiale touwen die de buiten- en binnenpolygoon verbinden, één
per segment.

| Element | Lengte per spaak | Totale lengte (24 spaken) |
|---|---|---|
| Spaak | 3.500 m | 84.000 m |

> De spaaklengte (3.500 m) komt overeen met het radiale verschil tussen de
> buiten- en binnenstraal (11.50 m − 8.00 m = 3.50 m).

## Touw-totaaltelling (rechte lijnen)

| Onderdeel | Lengte |
|---|---|
| Buitenpolygoon (24 zijden) | 72.050 m |
| Binnenpolygoon (24 zijden) | 50.122 m |
| 24 spaken | 84.000 m |
| **Totaal touw** | **206.173 m** |

## Paalplaatsing (voor sensor-/BLE-logica)

- Er zijn **24 paaltjes**, gelijkmatig verdeeld op de buitenste polygoon.
- Onderlinge hoek tussen palen vanuit het middelpunt: **15°**.
- Onderlinge afstand (koorde) tussen naburige palen: **3.002 m**.
- Hartlijn-radius van de palenring (buitenpolygoon): **11.50 m**.

### Hoekpositie per paal

Paal *n* (met *n* = 0 … 23) staat op hoek `θ = n × 15°` rond het middelpunt.
Cartesische positie t.o.v. het middelpunt (radius R = 11.50 m):

```
x_n = R · cos(n · 15°)
y_n = R · sin(n · 15°)
```

Dit geeft een vaste, voorspelbare mapping tussen een paal-index en zijn fysieke
locatie, bruikbaar voor afstandsberekeningen, buur-detectie en
spelmechanieken.

## Aannames / open punten

- De referentie-oriëntatie (welke paal index 0 is, en de richting van de
  nummering — met of tegen de klok) is nog niet vastgelegd. Leg dit eenmalig
  vast zodra de fysieke opstelling bekend is.
- Bovenstaande maten zijn de **touw-/hartlijn**-maten; eventuele paaldikte of
  montage-offset is hierin niet verrekend.
