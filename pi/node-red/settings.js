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
        // (leeg: het spel gebruikt uitsluitend global.get/set, geen extra libs nodig)
    },

    // --- Editor thema (cosmetisch) ---
    editorTheme: {
        projects: {
            enabled: false
        }
    }
};
