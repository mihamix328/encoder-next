param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output must be new.' }
# Offline staging only. No SSH, service changes, secrets or network configuration.
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
foreach ($name in @('encoder-server.service','encoder-wifi-collector.service',
    'encoder-wifi-collector.timer','encoder-wifi-scan.socket','encoder-wifi-scan@.service',
    'server.conf.example')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $OutputDirectory
}
Copy-Item -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'docs/SERVER_DEPLOYMENT.md') -Destination $OutputDirectory
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'MIGRATION-CHECKLIST.md') -Destination $OutputDirectory
Write-Output 'Offline templates prepared. No ARM binaries, secrets or experimental Wi-Fi switch units included. Nothing installed.'
