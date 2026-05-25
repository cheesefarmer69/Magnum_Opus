# Flow 05 — Admin

## Doel

Een afgeschermd beheerpaneel voor gevaarlijke acties. In deze eerste versie:
de **levensjaren-tellers resetten** per onderdeel, voor alle spelers tegelijk.
De knoppen zitten achter een **twee-staps verificatie** zodat je er niet per
ongeluk op drukt.

## Dashboard-pagina **Admin**

| Element                       | Werking                                                              |
|-------------------------------|----------------------------------------------------------------------|
| Switch **Admin ontgrendelen** | Stap 1. Moet AAN staan voordat een resetknop iets doet.              |
| Knop **Levensdagen → 0**      | Stap 2. Zet de levensdagen van alle spelers op 0 (uren blijven).     |
| Knop **Levensuren → 0**       | Stap 2. Zet de losse levensuren op 0 (hele dagen blijven).           |
| Knop **Achterstand → 0**      | Stap 2. Zet de `achterstand` van alle spelers op 0.                  |
| Knop **Paal-effecten → 0**    | Stap 2. Wist alle blijvende effecten op de palen (`bordStaat`).      |
| Notificatie                   | Bevestiging of waarschuwing ("vergrendeld") na elke klik.            |

## Twee-staps verificatie

1. Zet de switch **Admin ontgrendelen** aan.
2. Druk op een resetknop → de reset wordt uitgevoerd.
3. Het paneel **vergrendelt automatisch** weer (de switch springt terug op UIT).

Druk je op een resetknop terwijl het paneel vergrendeld is, dan gebeurt er
niets en verschijnt de melding "VERGRENDELD - zet eerst 'Admin ontgrendelen'
aan". Zo is één losse klik nooit genoeg om de score te wissen.

## Wat de resets precies doen

De score per speler zit in één teller, `global.spelerStats[naam].totaalUren`.
De radar splitst die in `Levensdagen = floor(totaalUren / 24)` en
`Levensuren = totaalUren % 24`. De knoppen werken op die componenten:

| Knop              | Bewerking op `totaalUren`           | Voorbeeld (totaalUren = 50) |
|-------------------|-------------------------------------|------------------------------|
| Levensdagen → 0   | `totaalUren = totaalUren % 24`      | 50 (2d 2u) → 2 (0d 2u)       |
| Levensuren → 0    | `totaalUren = floor(totaalUren/24)*24` | 50 (2d 2u) → 48 (2d 0u)   |

> Wil je álles in één keer wissen (dagen én uren)? Druk beide knoppen na
> elkaar, of gebruik de `[TEST] Reset levensjaren`-inject in flow 04
> Puntensysteem (zet `totaalUren` volledig op 0).

## Globale variabelen

| Variabele         | Type      | Gezet door | Gelezen door |
|-------------------|-----------|------------|--------------|
| `admin_unlocked`  | `boolean` | 05 Admin   | 05 Admin     |

`admin_unlocked` is standaard `false` (vergrendeld). Een ontbrekende waarde
geldt ook als vergrendeld, dus het paneel is veilig bij een verse start.

## Testen

1. Open dashboard → pagina **Admin**.
2. Druk meteen op **Levensdagen → 0** → melding "VERGRENDELD…"; niets verandert.
3. Zet **Admin ontgrendelen** aan, druk **Levensdagen → 0** → melding bevestigt,
   de switch springt terug op UIT. Controleer in de Live Radar dat de
   levensdagen op 0 staan en de losse uren behouden zijn.
4. Herhaal voor **Levensuren → 0** (dagen blijven, uren-rest weg).
