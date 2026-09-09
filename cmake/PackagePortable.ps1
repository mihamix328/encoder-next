param(
    [Parameter(Mandatory=$true)][string]$ExecutableDirectory,
    [Parameter(Mandatory=$true)][string]$VcpkgTripletDirectory,
    [Parameter(Mandatory=$true)][string]$VcRuntimeDirectory,
    [string]$OutputDirectory,
    [string]$PublicCertificate
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repo ('build/portable/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists; choose a new directory.' }
$binaries = (Resolve-Path -LiteralPath $ExecutableDirectory).Path
$triplet = (Resolve-Path -LiteralPath $VcpkgTripletDirectory).Path
$crt = (Resolve-Path -LiteralPath $VcRuntimeDirectory).Path
$required = @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','libcrypto-3-x64.dll','libssl-3-x64.dll')
foreach ($file in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $triplet "bin/$file"))) { throw "Missing runtime: $file" }
}
$platform = Join-Path $triplet 'Qt6/plugins/platforms/qwindows.dll'
if (!(Test-Path -LiteralPath $platform)) { throw 'Missing qwindows.dll' }
if (!(Test-Path -LiteralPath (Join-Path $crt 'vcruntime140.dll'))) { throw 'Missing Visual C++ runtime' }
foreach ($appName in @('client','admin')) {
    if (!(Test-Path -LiteralPath (Join-Path $binaries "cipheator-$appName.exe"))) { throw "Missing $appName executable" }
}
if ($PublicCertificate) {
    $PublicCertificate = (Resolve-Path -LiteralPath $PublicCertificate).Path
    $certificateText = [IO.File]::ReadAllText($PublicCertificate)
    if ($certificateText -match 'PRIVATE KEY' -or $certificateText -notmatch 'BEGIN CERTIFICATE') {
        throw 'Only a public PEM certificate can be bundled.'
    }
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
foreach ($appName in @('client','admin')) {
    $stage = Join-Path $OutputDirectory "encoeder-$appName-windows-x64"
    New-Item -ItemType Directory -Path "$stage/config","$stage/platforms" | Out-Null
    Copy-Item -LiteralPath (Join-Path $binaries "cipheator-$appName.exe") -Destination $stage
    Get-ChildItem -LiteralPath (Join-Path $triplet 'bin') -Filter '*.dll' | Copy-Item -Destination $stage
    Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination $stage
    Copy-Item -LiteralPath $platform -Destination "$stage/platforms"
    if ($appName -eq 'client') {
        Copy-Item -LiteralPath (Join-Path $repo 'config/client.conf.in') -Destination "$stage/config/client.conf"
    } else {
        Copy-Item -LiteralPath (Join-Path $repo 'config/admin.conf') -Destination "$stage/config/admin.conf"
    }
    if ($PublicCertificate) { Copy-Item -LiteralPath $PublicCertificate -Destination "$stage/config/server.crt" }
    Copy-Item -LiteralPath (Join-Path $repo 'docs/PORTABLE.md') -Destination "$stage/START-HERE.md"
    Copy-Item -LiteralPath (Join-Path $repo 'LICENSE.txt') -Destination $stage
    $zip = "$stage.zip"
    Compress-Archive -Path "$stage/*" -DestinationPath $zip
    Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Select-Object Path,Hash
}
