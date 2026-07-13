# MAC-oogst werkblad (24 palen)

Vul dit in terwijl je de bordjes afgaat. Het **WiFi-STA-MAC** haal je het snelst met
`lees-mac.ps1` (esptool, geen firmware/monitor nodig) — of uit de slave-banner in de seriële
monitor. Dit MAC gaat 1-op-1 in [`firmware/shared/paal_macs.h`](../shared/paal_macs.h).

> **C3 SuperMini in download-mode zetten** (nodig om betrouwbaar uit te lezen/flashen):
> **BOOT vasthouden → RESET tikken → BOOT loslaten.**
>
> Snelste manier per bordje:
> ```powershell
> cd firmware\tools
> .\lees-mac.ps1 -Port COM7 -Paal 5      # leest MAC + bewaart de paal_macs.h-regel
> ```

| Paal | Master | MAC (xx:xx:xx:xx:xx:xx) | In `paal_macs.h`? |
|-----:|:------:|-------------------------|:-----------------:|
| 1  | 1 | `AC:A7:04:BD:3A:48` | ✅ |
| 2  | 1 | `AC:A7:04:C0:C6:14` | ✅ |
| 3  | 1 | `8C:FD:49:54:C4:38` | ✅ |
| 4  | 1 | `8C:FD:49:54:BC:10` | ✅ |
| 5  | 1 | `AC:A7:04:D3:01:30` | ✅ |
| 6  | 1 | `AC:A7:04:D5:2A:A0` | ✅ |
| 7  | 1 | `AC:A7:04:BF:B5:A8` | ✅ |
| 8  | 1 | `AC:A7:04:BF:4D:0C` | ✅ |
| 9  | 2 | `AC:A7:04:C0:7F:C8` | ✅ |
| 10 | 2 | `AC:A7:04:BC:B5:54` | ✅ |
| 11 | 2 | `AC:A7:04:C0:79:DC` | ✅ |
| 12 | 2 | `AC:A7:04:BE:6D:78` | ✅ |
| 13 | 2 | `8C:FD:49:54:CB:48` | ✅ |
| 14 | 2 | `AC:A7:04:BB:F5:78` | ✅ |
| 15 | 2 | `AC:A7:04:D2:D5:4C` | ✅ |
| 16 | 2 | `AC:A7:04:D4:9A:A8` | ✅ |
| 17 | 3 | `8C:FD:49:54:DF:F0` | ✅ |
| 18 | 3 | `8C:FD:49:54:C9:E4` | ✅ |
| 19 | 3 | `AC:A7:04:BD:25:B8` | ✅ |
| 20 | 3 | `8C:FD:49:55:14:E8` | ✅ |
| 21 | 3 | `AC:A7:04:B8:96:C8` | ✅ |
| 22 | 3 | `AC:A7:04:B9:D6:90` | ✅ |
| 23 | 3 | `AC:A7:04:BE:60:28` | ✅ |
| 24 | 3 | `AC:A7:04:BA:60:18` | ✅ |

Master-indeling: master1 = palen 1–8, master2 = 9–16, master3 = 17–24.

## Nadat de tabel vol is
1. Zet elke regel in `firmware/shared/paal_macs.h` (formaat: `{{0x.., 0x.., 0x.., 0x.., 0x.., 0x..}, <paal>},`).
   `lees-mac.ps1 -Paal N` schrijft die regel al kant-en-klaar naar `paal-macs-verzameld.txt`.
2. Flash de **universele** slave-binary (`firmware/Slave`, env `esp32-c3-devkitm-1`) op elk bord —
   je stelt niks per bord in; het bord zoekt zijn `PAAL_ID` zelf op.
3. Herflash de drie masters (die vullen `slaveAdressen[]` uit dezelfde tabel).
