<#
.SYNOPSIS
  Leest het WiFi-STA-MAC van een ESP32-C3 SuperMini via esptool -- GEEN firmware flashen,
  GEEN seriele monitor nodig. Print het MAC + de kant-en-klare regel voor
  firmware/shared/paal_macs.h. Met -Paal wordt die regel bewaard in paal-macs-verzameld.txt.

.DESCRIPTION
  Het base-MAC dat esptool read_mac teruggeeft is exact het WiFi-STA-MAC dat de slave bij boot
  opzoekt (esp_read_mac(ESP_MAC_WIFI_STA)) -- dus 1-op-1 bruikbaar in paal_macs.h.

  >> Zet het bordje eerst in DOWNLOAD-MODE (betrouwbaar uitlezen op de C3 SuperMini):
  >>    BOOT vasthouden  ->  RESET tikken  ->  BOOT loslaten   (dan pas dit script draaien)

.EXAMPLE
  .\lees-mac.ps1                 # auto-poort als er precies 1 COM-poort is
  .\lees-mac.ps1 -Port COM7      # expliciete poort
  .\lees-mac.ps1 -Port COM7 -Paal 5   # + append regel voor paal 5 aan paal-macs-verzameld.txt
#>
param(
  [string]$Port,
  [int]$Paal = 0
)

$ErrorActionPreference = "Stop"

# --- esptool + python uit de PlatformIO-installatie ---
$pioHome = Join-Path $env:USERPROFILE ".platformio"
$py      = Join-Path $pioHome "penv\Scripts\python.exe"
$esptool = Join-Path $pioHome "packages\tool-esptoolpy\esptool.py"
if (-not (Test-Path $py))      { throw "PlatformIO-python niet gevonden: $py" }
if (-not (Test-Path $esptool)) { throw "esptool niet gevonden: $esptool  (doe eenmalig een PlatformIO-build zodat de tool geinstalleerd wordt)" }

# --- COM-poort bepalen ---
if (-not $Port) {
  $poorten = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique
  if ($poorten.Count -eq 1) {
    $Port = $poorten[0]; Write-Host "Auto-poort: $Port" -ForegroundColor Cyan
  } elseif ($poorten.Count -eq 0) {
    throw "Geen COM-poort gevonden. Zit het bordje in download-mode? (BOOT vasthouden -> RESET tikken -> BOOT loslaten)"
  } else {
    throw "Meerdere poorten gevonden ($($poorten -join ', ')). Geef expliciet op met -Port COMx"
  }
}

Write-Host "esptool read_mac op $Port ..." -ForegroundColor Cyan
$uit = & $py $esptool --chip esp32c3 --port $Port read_mac 2>&1
$uitTekst = ($uit | Out-String)

$m = [regex]::Match($uitTekst, '([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})')
if (-not $m.Success) {
  Write-Host $uitTekst
  throw "Geen MAC gevonden. Staat het bordje in download-mode (BOOT+RESET) en klopt -Port $Port ?"
}

$mac   = $m.Value.ToUpper()
$b     = $mac.Split(':')
$paalT = if ($Paal -gt 0) { "$Paal" } else { "<paal>" }
$cRegel = "  {{0x$($b[0]), 0x$($b[1]), 0x$($b[2]), 0x$($b[3]), 0x$($b[4]), 0x$($b[5])}, $paalT},"

Write-Host ""
Write-Host "  MAC (WiFi-STA) : $mac" -ForegroundColor Green
Write-Host "  paal_macs.h    : $cRegel" -ForegroundColor Green

if ($Paal -gt 0) {
  $verzamel = Join-Path $PSScriptRoot "paal-macs-verzameld.txt"
  Add-Content -Path $verzamel -Value $cRegel -Encoding utf8
  Write-Host ""
  Write-Host "  -> toegevoegd aan $verzamel  (paal $Paal)" -ForegroundColor Yellow
  Write-Host "     Na alle borden: plak die regels tussen de { } in firmware/shared/paal_macs.h." -ForegroundColor DarkGray
}
