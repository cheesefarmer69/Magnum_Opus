"""Orakel — onafhankelijke referentie-implementatie van de verplaatsingscontrole.

Berekent uit het *gespeelde pad* en het *event* wat de engine ZOU moeten teruggeven
(status + delta levensuren), volgens docs/spel/event-systeem.md §3/§7 en
docs/invarianten.md §2. Vergelijkt dat met de echte `pof/controle` van Node-RED.

Deze module deelt GEEN code met Node-RED: een bug in de engine wordt dus niet door
dezelfde bug in het orakel gemaskeerd. Het orakel is bewust een getrouwe replica van
de *bedoelde* scoring (zoals gespecificeerd én geïmplementeerd in "Verifieer beweging"),
zodat legale zetten 0 vals-positieven geven en echte regressies opvallen.

Dekkingsgraad:
- Volledig: verplaatsing-events (max/of/min), OK/stil, BEWOOG, TE VEEL/WEINIG,
  ONGELDIGE KEUZE, TERUG IN TIJD, ONGELDIGE TELEPORT, happy-hour ×2, portaal-teleport,
  middernacht-poort dicht, sterfte-collaps in de pof/controle-status.
- Best-effort/advisory: ziekte- en nuke-rondes (gemarkeerd, delta-gericht).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from . import config


# ---------------------------------------------------------------------------
# Pad-ontleding (1-op-1 met `ontleed()` in "Verifieer beweging")
# ---------------------------------------------------------------------------
@dataclass
class PadAnalyse:
    voor: int = 0
    achter: int = 0
    ongeldig: bool = False      # >1× zelfde portaal
    bewogen: bool = False
    kruist: bool = False        # voorwaartse oversteek van middernacht (wrap)


def _partner_map(portals: list[tuple[int, int]]) -> dict[int, int]:
    m: dict[int, int] = {}
    for a, b in portals:
        m[a] = b
        m[b] = a
    return m


def ontleed(hops: list[list[int]], portals: list[tuple[int, int]],
            ring: list[int]) -> PadAnalyse:
    r = PadAnalyse()
    partner = _partner_map(portals)
    palen = sorted(ring)
    n = len(palen)
    portaal_gebruik: dict[str, int] = {}
    for h in (hops or []):
        r.bewogen = True
        van, naar = h[0], h[1]
        if partner.get(van) == naar:  # TELEPORT
            key = f"{van}-{naar}" if van < naar else f"{naar}-{van}"
            portaal_gebruik[key] = portaal_gebruik.get(key, 0) + 1
            if portaal_gebruik[key] > 1:
                r.ongeldig = True
            continue
        if van not in palen or naar not in palen or n < 2:
            continue
        iv, inr = palen.index(van), palen.index(naar)
        fd = ((inr - iv) % n + n) % n
        bd = n - fd
        if fd <= bd:
            r.voor += fd
            if inr < iv:
                r.kruist = True
        else:
            r.achter += bd
    return r


# ---------------------------------------------------------------------------
# Ronde-context + per-speler verwachting
# ---------------------------------------------------------------------------
@dataclass
class RondeContext:
    event: dict[str, Any]                       # incl. voorwaarde, getalWaarde(2), doelwit, doelwitType, gevolgen
    start_pos: dict[str, int]                   # snapshot vóór bewegen
    end_pos: dict[str, int]                     # spelerLocaties bij de controle (laatst gepubliceerd)
    paths: dict[str, list[list[int]]]           # naam -> hops
    portals: list[tuple[int, int]] = field(default_factory=list)
    happy_palen: set[int] = field(default_factory=set)
    medicijn_palen: set[int] = field(default_factory=set)
    zieke: set[str] = field(default_factory=set)
    dienaars: dict[str, str] = field(default_factory=dict)
    geblokkeerd: set[str] = field(default_factory=set)   # mag_niet_bewegen (harness-getrackt)
    mn_dicht: bool = False
    mn_paal: int | None = None
    ring: list[int] = field(default_factory=lambda: list(range(1, config.RING + 1)))


@dataclass
class VerwachtSpeler:
    speler: str
    base_status: str           # bv. "OK", "TE VEEL", "MIDDERNACHT DICHT", ...
    delta: int                 # ruwe delta (zoals pof/controle._delta)
    collapsed_status: str      # zoals pof/controle.status (FOUT-set of "OK")
    sterfte: bool              # of deze ronde een sterfte veroorzaakte


def _bewegingsevent(voorwaarde: str) -> bool:
    return voorwaarde in ("min", "max", "of")


class Orakel:
    """Houdt lopende totalen bij om de sterfte-collaps in pof/controle te voorspellen.

    Start een sessie met wis-stats (alle totalen 0) zodat het orakel synchroon loopt
    met de engine. Roep `evalueer_ronde` aan per ronde; vergelijk met `vergelijk`.
    """

    def __init__(self) -> None:
        self.totaal: dict[str, int] = {}
        self.sterftes: dict[str, int] = {}

    def reset(self) -> None:
        self.totaal.clear()
        self.sterftes.clear()

    # -- kern: bepaal verwachting per speler, en werk de lopende totalen bij --
    def evalueer_ronde(self, ctx: RondeContext) -> dict[str, VerwachtSpeler]:
        ev = ctx.event
        voorwaarde = ev.get("voorwaarde") or "geen"
        x = ev.get("getalWaarde")
        y = ev.get("getalWaarde2")
        beweging = _bewegingsevent(voorwaarde)
        # Doelwit-namen tellen enkel bij een bewegings-event; bij uur-events zijn de
        # doelwitten palen (ints) die nooit een spelernaam matchen — onschadelijk.
        doelwit = set(ev.get("doelwit") or [])
        resultaat: dict[str, VerwachtSpeler] = {}

        for naam in ctx.start_pos:
            r = ontleed(ctx.paths.get(naam), ctx.portals, ctx.ring)
            is_doel = beweging and naam in doelwit
            status, delta = self._classificeer(naam, r, is_doel, voorwaarde, x, y, ctx)

            # Middernacht-poort dicht: OVERSTEKEN (voorwaartse wrap over de poort, r.kruist) = alle
            # uren kwijt + sterfte. Wie naar de poort-paal loopt zonder over te steken blijft ongestraft.
            mn_kruis_dood = bool(ctx.mn_dicht and r.kruist)
            if mn_kruis_dood:
                status = "MIDDERNACHT DICHT"
                delta = -self.totaal.get(naam, 0)

            # Ziekte-modifier (niet bij een verboden middernacht-oversteek)
            if not mn_kruis_dood and naam in ctx.zieke:
                if delta > 0:
                    delta = 0
                legaal = status in ("OK", "OK (stil)")
                op_med = ctx.end_pos.get(naam) in ctx.medicijn_palen
                if legaal and op_med:
                    status = "GENEZEN"
                elif legaal:
                    status = "ZIEK"

            # Happy hour ×2 (alleen op legaal verdiende winst)
            if delta > 0 and ctx.end_pos.get(naam) in ctx.happy_palen:
                delta *= 2

            # Sterfte-voorspelling op de lopende totalen (dienaars verliezen niet via winst).
            sterfte = self._pas_toe(naam, status, delta, ctx, force_sterfte=mn_kruis_dood)

            collapsed = self._collapse(status, sterfte)
            resultaat[naam] = VerwachtSpeler(naam, status, delta, collapsed, sterfte)
        return resultaat

    def _classificeer(self, naam, r: PadAnalyse, is_doel, voorwaarde, x, y, ctx):
        if naam in ctx.geblokkeerd:
            return "GEBLOKKEERD", 0
        if is_doel:
            if r.ongeldig:
                return "ONGELDIGE TELEPORT", -r.voor
            if r.achter > 0:
                return "TERUG IN TIJD", -r.achter
            if voorwaarde == "max" and x is not None and r.voor > x:
                return "TE VEEL", -(r.voor - x)
            if voorwaarde == "min" and x is not None and r.voor < x:
                return "TE WEINIG", -r.voor
            if voorwaarde == "of" and r.voor != x and r.voor != y:
                return "ONGELDIGE KEUZE", -r.voor
            return "OK", r.voor
        # niet-doelwit: moet stil blijven
        if not r.bewogen and not r.ongeldig:
            return "OK (stil)", 0
        return "BEWOOG (mocht niet)", -(r.voor + r.achter)

    def _pas_toe(self, naam, status, delta, ctx, force_sterfte: bool = False) -> bool:
        """Werk lopende totalen bij; geef terug of er een sterfte was."""
        self.totaal.setdefault(naam, 0)
        self.sterftes.setdefault(naam, 0)
        if force_sterfte:
            # Verboden middernacht-oversteek: alle uren kwijt + sterfte (geen dienaar-omleiding).
            self.totaal[naam] = 0
            self.sterftes[naam] += 1
            return True
        meester = ctx.dienaars.get(naam)
        if meester is not None and delta > 0:
            self.totaal.setdefault(meester, 0)
            self.totaal[meester] += delta
            return False
        nieuw = self.totaal[naam] + delta
        if nieuw < 0:
            self.totaal[naam] = 0
            self.sterftes[naam] += 1
            return True
        self.totaal[naam] = nieuw
        return False

    @staticmethod
    def _collapse(base_status: str, sterfte: bool) -> str:
        # De engine zet pof/controle.status op de base-status enkel als die EXACT in de
        # FOUT-set zit; elke suffix (sterfte/happy/dienaar) maakt het "OK".
        if sterfte:
            return "OK"
        return base_status if base_status in config.FOUT_STATUSSEN else "OK"


# ---------------------------------------------------------------------------
# Vergelijking met de echte pof/controle
# ---------------------------------------------------------------------------
@dataclass
class Mismatch:
    speler: str
    veld: str            # "delta" of "status"
    verwacht: Any
    gekregen: Any
    base_status: str
    regel: str           # korte verwijzing naar de spec-regel


def vergelijk(verwacht: dict[str, VerwachtSpeler], controle_payload: dict[str, Any]) -> list[Mismatch]:
    """Vergelijk de orakel-verwachting met een echte pof/controle-payload."""
    mismatches: list[Mismatch] = []
    resultaten = {r.get("speler"): r for r in (controle_payload or {}).get("resultaten", [])}
    for naam, vw in verwacht.items():
        got = resultaten.get(naam)
        if got is None:
            mismatches.append(Mismatch(naam, "ontbreekt", vw.delta, None, vw.base_status,
                                       "speler niet in pof/controle"))
            continue
        if int(got.get("delta", 0)) != int(vw.delta):
            mismatches.append(Mismatch(naam, "delta", vw.delta, got.get("delta"),
                                       vw.base_status, "scoringtabel (event-systeem §7)"))
        if got.get("status") != vw.collapsed_status:
            mismatches.append(Mismatch(naam, "status", vw.collapsed_status, got.get("status"),
                                       vw.base_status, "FOUT-set / sterfte-collaps"))
    return mismatches


# ---------------------------------------------------------------------------
# Zelftest: de gespecificeerde voorbeelden uit event-systeem.md / spel.md
# ---------------------------------------------------------------------------
def _selftest() -> int:
    fouten = 0

    def check(naam, voorwaarde, x, y, start, hops, end, portals, happy, is_doel_naam,
              verwacht_status, verwacht_delta, extra=""):
        nonlocal fouten
        orakel = Orakel()
        ev = {"voorwaarde": voorwaarde, "getalWaarde": x, "getalWaarde2": y,
              "doelwit": [is_doel_naam] if is_doel_naam else [], "doelwitType": "speler"}
        ctx = RondeContext(event=ev, start_pos={naam: start}, end_pos={naam: end},
                           paths={naam: hops}, portals=portals, happy_palen=set(happy))
        res = orakel.evalueer_ronde(ctx)[naam]
        ok = res.base_status == verwacht_status and res.delta == verwacht_delta
        vlag = "OK " if ok else "FOUT"
        if not ok:
            fouten += 1
        print(f"  [{vlag}] {extra or naam}: status={res.base_status!r} delta={res.delta} "
              f"(verwacht {verwacht_status!r}/{verwacht_delta})")

    print("Orakel-zelftest (spec-voorbeelden):")
    # Niet-doelwit dat beweegt: 5->8 = -3, 5->4 = -1 (event-systeem §spel)
    check("A", "max", 3, None, 5, [[5, 6], [6, 7], [7, 8]], 8, [], [], None,
          "BEWOOG (mocht niet)", -3, extra="niet-doelwit 5->8")
    check("A", "max", 3, None, 5, [[5, 4]], 4, [], [], None,
          "BEWOOG (mocht niet)", -1, extra="niet-doelwit 5->4")
    # Portaal-voorbeeld: paal10 max5, portaal 12-20: 10->12 (2), 12->20 teleport(0), 20->23 (3) = +5
    check("B", "max", 5, None, 10,
          [[10, 11], [11, 12], [12, 20], [20, 21], [21, 22], [22, 23]], 23,
          [(12, 20)], [], "B", "OK", 5, extra="portaalsprong telt 0 (+5)")
    # Happy hour ×2: doelwit 3 vooruit eindigend op happy -> +6
    check("C", "max", 5, None, 5, [[5, 6], [6, 7], [7, 8]], 8, [], [8], "C",
          "OK", 6, extra="happy hour ×2 (+6)")
    # TE VEEL: max 3, voor 5 -> -(5-3) = -2
    check("D", "max", 3, None, 1, [[1, 2], [2, 3], [3, 4], [4, 5], [5, 6]], 6, [], [], "D",
          "TE VEEL", -2, extra="max 3, voor 5")
    # ONGELDIGE KEUZE (of x=2 y=5, voor=3) -> -3
    check("E", "of", 2, 5, 1, [[1, 2], [2, 3], [3, 4]], 4, [], [], "E",
          "ONGELDIGE KEUZE", -3, extra="of 2/5, voor 3")
    # OK (of), voor=5 -> +5
    check("F", "of", 2, 5, 1, [[1, 2], [2, 3], [3, 4], [4, 5], [5, 6]], 6, [], [], "F",
          "OK", 5, extra="of 2/5, voor 5")
    # TERUG IN TIJD: doelwit zet achteruit 5->4 -> -1
    check("G", "max", 3, None, 5, [[5, 4]], 4, [], [], "G",
          "TERUG IN TIJD", -1, extra="doelwit achteruit")
    # ONGELDIGE TELEPORT: pingpong door zelfde portaal 2x
    check("H", "max", 5, None, 12, [[12, 20], [20, 12]], 12, [(12, 20)], [], "H",
          "ONGELDIGE TELEPORT", 0, extra="portaal pingpong")

    # Sterfte-collaps: een TE VEEL (-2) bij speler met saldo 0 -> sterfte -> pof/controle "OK"
    orakel = Orakel()
    ev = {"voorwaarde": "max", "getalWaarde": 0, "doelwit": ["Z"], "doelwitType": "speler"}
    ctx = RondeContext(event=ev, start_pos={"Z": 1}, end_pos={"Z": 3},
                       paths={"Z": [[1, 2], [2, 3]]}, ring=list(range(1, 25)))
    res = orakel.evalueer_ronde(ctx)["Z"]
    ok = res.base_status == "TE VEEL" and res.delta == -2 and res.sterfte and res.collapsed_status == "OK"
    fouten += 0 if ok else 1
    print(f"  [{'OK ' if ok else 'FOUT'}] sterfte-collaps: base={res.base_status!r} "
          f"delta={res.delta} sterfte={res.sterfte} collapsed={res.collapsed_status!r}")

    # Middernacht-poort dicht: 24->1 oversteek = alle uren kwijt + sterfte.
    orakel = Orakel(); orakel.totaal["W"] = 5
    ev = {"voorwaarde": "max", "getalWaarde": 5, "doelwit": ["W"], "doelwitType": "speler"}
    ctx = RondeContext(event=ev, start_pos={"W": 24}, end_pos={"W": 1},
                       paths={"W": [[24, 1]]}, ring=list(range(1, 25)), mn_dicht=True, mn_paal=24)
    res = orakel.evalueer_ronde(ctx)["W"]
    ok = res.base_status == "MIDDERNACHT DICHT" and res.delta == -5 and res.sterfte and res.collapsed_status == "OK"
    fouten += 0 if ok else 1
    print(f"  [{'OK ' if ok else 'FOUT'}] middernacht-oversteek: base={res.base_status!r} "
          f"delta={res.delta} sterfte={res.sterfte} (verwacht -5 + sterfte)")

    # Middernacht-poort dicht: tot AAN paal 24 lopen zonder oversteken = ongestraft (+2).
    orakel = Orakel()
    ev = {"voorwaarde": "max", "getalWaarde": 5, "doelwit": ["V"], "doelwitType": "speler"}
    ctx = RondeContext(event=ev, start_pos={"V": 22}, end_pos={"V": 24},
                       paths={"V": [[22, 23], [23, 24]]}, ring=list(range(1, 25)), mn_dicht=True, mn_paal=24)
    res = orakel.evalueer_ronde(ctx)["V"]
    ok = res.base_status == "OK" and res.delta == 2 and not res.sterfte
    fouten += 0 if ok else 1
    print(f"  [{'OK ' if ok else 'FOUT'}] naar poort zonder oversteek: status={res.base_status!r} "
          f"delta={res.delta} (verwacht OK/+2)")

    print(f"\nResultaat: {'ALLE TESTS GESLAAGD' if fouten == 0 else str(fouten) + ' FOUT(EN)'}")
    return 1 if fouten else 0


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        raise SystemExit(_selftest())
    print("Gebruik: python -m tools.speltest.oracle --selftest")
