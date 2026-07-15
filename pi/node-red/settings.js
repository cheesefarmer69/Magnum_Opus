/**
 * Node-RED settings voor Magnum Opus (container `magnum-Opus`).
 *
 * KERN VAN DEZE FILE: `contextStorage` -> `localfilesystem`. Zonder dit draait Node-RED
 * met de default IN-MEMORY context-store en gaat ALLE `global.*`-state verloren bij elke
 * container-restart, reboot of `deploy-flows`-push (spelerStats, globaleStats, spelHistorie,
 * de π-stand, godPunten, ...). Met localfilesystem wordt de context periodiek (flushInterval)
 * naar `/data/context/` geschreven en overleeft de state een herstart.
 *
 * Deze file wordt gemount op `/data/settings.js` (zie pi/node-red/docker-compose.yml).
 * `/data` moet een PERSISTENTE bind-mount zijn (op deze Pi: de SD-kaart / root-fs, want er is
 * geen aparte SSD), anders staat `/data/context` in de wegwerpbare containerlaag en helpt dit
 * niets bij een container-recreate.
 *
 * LET OP — credentials: deze file zet BEWUST GEEN `credentialSecret`. Node-RED gebruikt dan
 * de automatisch gegenereerde sleutel uit `/data/.config.runtime.json` (blijft bewaard zolang
 * `/data` persistent is), zodat bestaande versleutelde credentials blijven werken. Zet enkel
 * een vaste `credentialSecret` als je die bewust wil beheren (en migreer dan de creds).
 *
 * Alle niet-gespecificeerde opties vallen terug op de Node-RED defaults.
 */

module.exports = {
    // --- Flows ---
    flowFile: "flows.json",
    // Bewaar flows als leesbare JSON (niet ge-pretty-print maar wel geldig) — laat op default.
    flowFilePretty: false,

    // --- HTTP / editor ---
    uiPort: process.env.PORT || 1880,

    // Simulator statisch geserveerd op /sim/ (bron: ../simulator, read-only gemount op
    // /sim-static — zie docker-compose.yml). Bereikbaar op elk Pi-adres: thuis
    // http://192.168.1.43:1880/sim/, veld-AP http://192.168.50.1:1880/sim/,
    // veld-kabel http://192.168.51.1:1880/sim/.
    httpStatic: [
        { path: "/sim-static", root: "/sim/" }
    ],

    // --- Runtime ---
    // Log-niveau: info is genoeg voor productie op de Pi.
    logging: {
        console: {
            level: "info",
            metrics: false,
            audit: false
        }
    },

    // --- Context-persistentie (DE reden van deze file) ---
    // default-store = localfilesystem: synchroon-gecacht, periodiek geflusht naar /data/context/.
    // flushInterval in seconden: bij een harde crash kan tot ~flushInterval aan state verloren
    // gaan (de retained `spel/state`-dump in flows.json dekt het grovere herstel).
    contextStorage: {
        default: {
            module: "localfilesystem",
            config: {
                flushInterval: 15
            }
        }
    },

    // --- Function-nodes ---
    functionGlobalContext: {
        // Gedeelde PARTIJ-reset (single source of truth). Wist ALLE per-partij toestand-registers
        // op één plek, zodat elk nieuw global-veld nog maar HIER hoeft te worden toegevoegd i.p.v.
        // in elke reset-knop apart. Raakt NOOIT persistente state aan (globaleStats, spelHistorie,
        // spelNummer, godPunten, spelerStats-totalen, de pi-klok midnight*, middernachtActief/Aan).
        // Roep SYNCHROON aan met het global-object:  global.get("resetPartij")(global)
        // (synchroon = veilig vlak vóór het opzetten van een nieuwe start-state, geen volgorde-val).
        resetPartij: function (global) {
            // R4: elke partij-reset verhoogt de generatie; hangende setTimeout-callbacks
            // (bv. de doelwit-reveal) checken dit token en vuren niet meer na een reset.
            global.set("pofGeneration", (global.get("pofGeneration") || 0) + 1);
            global.set("pofHuidigSpel", null); global.set("pofHuidigEvent", null); global.set("pofWachtrij", []);
            global.set("pofActief", false); global.set("pofFase", "idle"); global.set("pofTeller", 0);
            global.set("pofEventenRonde", 0); global.set("pofDoelwitReveal", "");
            global.set("bordStaat", {}); global.set("spelerEffecten", {}); global.set("wereldEffecten", []);
            global.set("ziekeSpelers", {}); global.set("pofGenezen", []); global.set("dienaars", {}); global.set("luisterNaam", {});
            global.set("tijdbomSpelers", {}); global.set("tijdbomOntmantelPalen", []);
            global.set("tijdreizenActief", false); global.set("etenstijd", null); global.set("tweelingen", []); global.set("tweelingVerbrokenCue", false);
            global.set("polonaiseActief", false); global.set("polonaiseTeller", 0); global.set("polonaiseAfloop", false); global.set("maxPerUur", null); global.set("geenWinstVolgende", {});
            global.set("poolsActief", false); global.set("poolsGestart", false);   // Poolse-reactietijd-muziek: tick stuurt bij stop een audio/muziek stop
            global.set("infectedActief", false); global.set("infected", null); global.set("infectedLed", {}); global.set("infectedLaatstePalen", []); global.set("infectedStatusSig", "");
            global.set("bommenGestart", false); global.set("bommenStatusSig", ""); global.set("bommenFlourishGewist", false);   // minigame "Bommen vermijden": Stop stopt de gescripte tijdlijn (pofGeneration is al opgehoogd -> geplande cues bailen)
            global.set("nukeActief", false); global.set("nukeNaglow", false); global.set("middernachtOogst", false); global.set("tornadoActief", []); global.set("spelTempoFactor", 1); global.set("pofSnapshots", []); global.set("paalLedForceRebuild", true);
            global.set("pofVerificatie", {}); global.set("pofLaatsteControle", []); global.set("pofPad", {});
            global.set("pofVrijPad", {}); global.set("pofVrijVanaf", 0);
            global.set("mnGestraft", {}); global.set("paalLedActie", {});
        },

        // Gedeelde TWEELING-DOOD (single source of truth, invariant TW3). Sterft een speler, dan
        // sterft zijn tweeling mee (uren 0 + 1 sterfte) en breekt de band. Dit moet OOK gebeuren
        // buiten "Verifieer beweging": bij de middernacht-oogst en bij de ziekte-dood. Roep aan met
        // de lijst zojuist gestorven namen:  global.get("tweelingDood")(global, ["Ann"])
        // Uitzondering: de NUKE roept dit NOOIT aan -- een nuke breekt geen tweelingbanden (TW5).
        // Retourneert de namen die door de propagatie zijn mee-gestorven (voor logging/afroep).
        tweelingDood: function (global, namen) {
            const paren = global.get("tweelingen") || [];
            if (!paren.length || !namen || !namen.length) return [];
            const dood = new Set(namen);
            let stats = global.get("spelerStats") || {};
            const verbroken = new Set();
            const meeGestorven = [];
            for (const p of paren) {
                const inDood = dood.has(p.a) || dood.has(p.b);
                if (!inDood) continue;
                verbroken.add(p.inst);
                for (const n of [p.a, p.b]) {
                    let s = stats[n] || {};
                    if (s.totaalUren === undefined) s.totaalUren = 0;
                    if (s.sterftes === undefined) s.sterftes = 0;
                    if (!dood.has(n)) { s.sterftes += 1; meeGestorven.push(n); }   // de partner sterft mee
                    s.totaalUren = 0;
                    stats[n] = s;
                }
            }
            if (!verbroken.size) return [];
            global.set("spelerStats", stats);
            global.set("tweelingen", paren.filter(p => !verbroken.has(p.inst)));
            global.set("wereldEffecten", (global.get("wereldEffecten") || []).filter(e => !(e.effect === "tweeling" && verbroken.has(e.instId))));
            global.set("tweelingVerbrokenCue", true);   // eind-afroep "de tweelingen zijn verbroken": bij ELKE verbreking (hier: dood-propagatie). Opgepikt door "Verouder effecten" op de afgelopen-emissie.
            return meeGestorven;
        },

        // Gedeelde SPELER-RESET (single source of truth, invariant S10). Haalt ÉÉN speler uit ALLE
        // lopende toestanden en uit elk doelwit waarvoor hij gekozen was, zodat hij "schoon" verder
        // speelt. Bedoeld voor het Admin-paneel wanneer een speler door een beacon-/detectiefout in
        // een toestand is beland die niet klopt.
        //
        // WAT WEL:  ziekte, tijdbom, speler-effecten, dienaar/meester, identiteitscrisis-alias,
        //           tweelingband (verbroken ZONDER dood), etenstijd (wolf/schaap), infected,
        //           max-per-uur-vlag, middernacht-dedup, pad-opnames, en zijn lidmaatschap van
        //           het huidige doelwit / de laatste controle.
        // WAT NIET: zijn SCORE (totaalUren, sterftes, valsspeelpunten, godPunten, doel-voortgang) --
        //           daarvoor is "Handmatig bijstellen" (S9). Ook zijn LOCATIE (fysieke waarheid) en
        //           zijn PAUZE-stand (aparte, bewuste operator-keuze) blijven staan.
        //
        // Roep SYNCHROON aan:  global.get("resetSpeler")(global, "Aagje")
        // Geeft een lijst terug van wat er effectief gewist is (voor de dashboard-melding).
        resetSpeler: function (global, naam) {
            if (!naam) return [];
            const weg = [];
            const zonder = (arr, n) => (arr || []).filter(x => x !== n);

            // --- Speler-toestanden
            const ziek = global.get("ziekeSpelers") || {};
            if (ziek[naam] != null) { delete ziek[naam]; global.set("ziekeSpelers", ziek); weg.push("ziekte"); }

            const bommen = global.get("tijdbomSpelers") || {};
            if (bommen[naam] != null) { delete bommen[naam]; global.set("tijdbomSpelers", bommen); weg.push("tijdbom"); }

            const se = global.get("spelerEffecten") || {};
            if ((se[naam] || []).length) { delete se[naam]; global.set("spelerEffecten", se); weg.push("speler-effecten"); }

            // --- Dienaar/meester (middernacht-oogst): beide richtingen
            const dn = global.get("dienaars") || {};
            let dnDirty = false;
            if (dn[naam] != null) { delete dn[naam]; dnDirty = true; weg.push("dienaar-van"); }
            for (const d in dn) { if (dn[d] === naam) { delete dn[d]; dnDirty = true; weg.push("meester-van-" + d); } }
            if (dnDirty) global.set("dienaars", dn);

            // --- Identiteitscrisis: als key én als waarde (anders luistert hij nog naar/voor iemand)
            const ln = global.get("luisterNaam") || {};
            let lnDirty = false;
            if (ln[naam] != null) { delete ln[naam]; lnDirty = true; }
            for (const k in ln) { if (ln[k] === naam) { delete ln[k]; lnDirty = true; } }
            if (lnDirty) { global.set("luisterNaam", ln); weg.push("identiteitscrisis"); }

            // --- Tweeling: band verbreken ZONDER dood (dus NIET tweelingDood gebruiken)
            const paren = global.get("tweelingen") || [];
            const raak = paren.filter(p => p.a === naam || p.b === naam);
            if (raak.length) {
                const inst = new Set(raak.map(p => p.inst));
                global.set("tweelingen", paren.filter(p => !inst.has(p.inst)));
                global.set("wereldEffecten", (global.get("wereldEffecten") || [])
                    .filter(e => !(e.effect === "tweeling" && inst.has(e.instId))));
                weg.push("tweelingband");
            }

            // --- Etenstijd: wolf -> hele jacht opheffen; schaap -> enkel hemzelf eruit
            const et = global.get("etenstijd");
            if (et) {
                if (et.wolf === naam) {
                    global.set("etenstijd", null);
                    global.set("wereldEffecten", (global.get("wereldEffecten") || []).filter(e => e.effect !== "etenstijd"));
                    weg.push("etenstijd (was de wolf -> jacht opgeheven)");
                } else if ((et.schapen || []).indexOf(naam) >= 0 || (et.gevangen || []).indexOf(naam) >= 0) {
                    et.schapen = zonder(et.schapen, naam);
                    et.gevangen = zonder(et.gevangen, naam);
                    et.over = et.schapen.length;
                    global.set("etenstijd", et);
                    weg.push("etenstijd (schaap)");
                }
            }

            // --- Infected-minigame
            const inf = global.get("infected");
            if (inf && typeof inf === "object") {
                let infDirty = false;
                for (const k of ["besmet", "gezond", "genezen", "patient0"]) {
                    if (Array.isArray(inf[k]) && inf[k].indexOf(naam) >= 0) { inf[k] = zonder(inf[k], naam); infDirty = true; }
                    else if (inf[k] === naam) { inf[k] = null; infDirty = true; }
                }
                if (infDirty) { global.set("infected", inf); weg.push("infected"); }
            }

            // --- Losse vlaggen en pad-opnames
            for (const key of ["geenWinstVolgende", "mnGestraft", "pofPad", "pofVrijPad"]) {
                const m = global.get(key) || {};
                if (m[naam] != null) { delete m[naam]; global.set(key, m); }
            }
            const genezen = global.get("pofGenezen") || [];
            if (genezen.some(g => g && g.naam === naam)) {
                global.set("pofGenezen", genezen.filter(g => !(g && g.naam === naam)));
            }

            // --- Doelwit-lidmaatschap: hij vervalt als doelwit van het LOPENDE event
            const ver = global.get("pofVerificatie") || {};
            if (Array.isArray(ver.doelwit) && ver.doelwit.indexOf(naam) >= 0) {
                ver.doelwit = zonder(ver.doelwit, naam);
                if (ver.startPosities) delete ver.startPosities[naam];
                global.set("pofVerificatie", ver);
                weg.push("doelwit (lopend event)");
            }
            const he = global.get("pofHuidigEvent");
            if (he && Array.isArray(he.doelwit) && he.doelwit.indexOf(naam) >= 0) {
                he.doelwit = zonder(he.doelwit, naam);
                global.set("pofHuidigEvent", he);
            }
            const bereikt = global.get("pofDoelBereikt") || [];
            if (bereikt.indexOf(naam) >= 0) global.set("pofDoelBereikt", zonder(bereikt, naam));

            const ctrl = global.get("pofLaatsteControle") || [];
            if (ctrl.some(r => r && (r.Speler === naam || r.speler === naam))) {
                global.set("pofLaatsteControle", ctrl.filter(r => !(r && (r.Speler === naam || r.speler === naam))));
            }

            // LED's opnieuw laten opbouwen (medicijn/tijdbom-palen kunnen nu overbodig zijn).
            global.set("paalLedForceRebuild", true);
            return weg;
        }
    },

    // --- Editor thema (cosmetisch) ---
    editorTheme: {
        projects: {
            enabled: false
        }
    }
};
