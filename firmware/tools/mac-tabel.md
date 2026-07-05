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
| 4  | 1 | | ⬜ |
| 5  | 1 | | ⬜ |
| 6  | 1 | | ⬜ |
| 7  | 1 | | ⬜ |
| 8  | 1 | | ⬜ |
| 9  | 2 | `AC:A7:04:C0:7F:C8` | ✅ |
| 10 | 2 | | ⬜ |
| 11 | 2 | | ⬜ |
| 12 | 2 | | ⬜ |
| 13 | 2 | | ⬜ |
| 14 | 2 | | ⬜ |
| 15 | 2 | | ⬜ |
| 16 | 2 | | ⬜ |
| 17 | 3 | `8C:FD:49:54:DF:F0` | ✅ |
| 18 | 3 | | ⬜ |
| 19 | 3 | | ⬜ |
| 20 | 3 | | ⬜ |
| 21 | 3 | | ⬜ |
| 22 | 3 | | ⬜ |
| 23 | 3 | | ⬜ |
| 24 | 3 | | ⬜ |

Master-indeling: master1 = palen 1–8, master2 = 9–16, master3 = 17–24.

## Nadat de tabel vol is
1. Zet elke regel in `firmware/shared/paal_macs.h` (formaat: `{{0x.., 0x.., 0x.., 0x.., 0x.., 0x..}, <paal>},`).
   `lees-mac.ps1 -Paal N` schrijft die regel al kant-en-klaar naar `paal-macs-verzameld.txt`.
2. Flash de **universele** slave-binary (`firmware/Slave`, env `esp32-c3-devkitm-1`) op elk bord —
   je stelt niks per bord in; het bord zoekt zijn `PAAL_ID` zelf op.
3. Herflash de drie masters (die vullen `slaveAdressen[]` uit dezelfde tabel).
