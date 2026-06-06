# Deploy de volledige Node-RED flow via de Admin API (Windows / PowerShell).
#
# Vervangt ALLE flows + dashboard-UI in een keer, op node-ID, zonder duplicaten
# en zonder handmatig wissen/importeren in de browser.
#
# Gebruik (vanuit deze map of met volledig pad):
#   .\deploy-flows.ps1
#   .\deploy-flows.ps1 -Url http://192.168.1.43:1880
#
# Vereist: Node-RED draait en is bereikbaar; geen admin-authenticatie.

param(
    [string]$Url = "http://192.168.1.43:1880"
)

$ErrorActionPreference = "Stop"
$flows = Join-Path $PSScriptRoot "flows.json"

if (-not (Test-Path $flows)) {
    Write-Error "flows.json niet gevonden: $flows"
    exit 1
}

# 1. JSON valideren zodat we nooit een kapotte flow pushen
try {
    $null = Get-Content $flows -Raw | ConvertFrom-Json
    Write-Host "JSON OK"
} catch {
    Write-Error "Ongeldige JSON in flows.json: $($_.Exception.Message)"
    exit 1
}

# 2. Volledige deploy via de Admin API
#    We sturen de RUWE UTF-8 bytes met expliciete charset. (Windows PowerShell 5.1
#    verminkt UTF-8 bij -InFile / string-body zonder charset -> "rare tekens" zoals
#    â€¢ of Ã© in de draaiende Node-RED. Raw bytes + char=utf-8 voorkomt dat.)
Write-Host "Deploy naar $Url/flows ..."
try {
    $bytes = [System.IO.File]::ReadAllBytes($flows)
    Invoke-RestMethod -Uri "$Url/flows" -Method Post `
        -ContentType "application/json; charset=utf-8" `
        -Headers @{ "Node-RED-Deployment-Type" = "full" } `
        -Body $bytes | Out-Null
    Write-Host "OK - alle flows vervangen." -ForegroundColor Green
} catch {
    Write-Error "Deploy mislukt: $($_.Exception.Message)"
    Write-Host "Tip: draait Node-RED op $Url ? Staat admin-auth uit?" -ForegroundColor Yellow
    exit 1
}
