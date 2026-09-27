param(
    [string]$ExecutableDirectory,
    [string]$BuildDirectory,
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
if ([bool]$ExecutableDirectory -eq [bool]$BuildDirectory) { throw 'Specify either ExecutableDirectory or BuildDirectory.' }
$binaries = if ($ExecutableDirectory) { (Resolve-Path -LiteralPath $ExecutableDirectory).Path } else { (Resolve-Path -LiteralPath $BuildDirectory).Path }
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
    $relative = if ($BuildDirectory) { "$appName/Release/encoder-$appName.exe" } else { "encoder-$appName.exe" }
    if (!(Test-Path -LiteralPath (Join-Path $binaries $relative))) { throw "Missing $appName executable" }
}
$notices = @(Get-ChildItem -LiteralPath (Join-Path $triplet 'share') -Recurse -File -Filter copyright)
if (!$notices.Count) { throw 'Dependency license notices not found.' }
if ($PublicCertificate) {
    $PublicCertificate = (Resolve-Path -LiteralPath $PublicCertificate).Path
    $certificateText = [IO.File]::ReadAllText($PublicCertificate)
    if ($certificateText -match 'PRIVATE KEY' -or $certificateText -notmatch 'BEGIN CERTIFICATE') {
        throw 'Only a public PEM certificate can be bundled.'
    }
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
foreach ($appName in @('client','admin')) {
    $stage = Join-Path $OutputDirectory "encoder-$appName-windows-x64"
    New-Item -ItemType Directory -Path "$stage/config","$stage/platforms" | Out-Null
    $relative = if ($BuildDirectory) { "$appName/Release/encoder-$appName.exe" } else { "encoder-$appName.exe" }
    Copy-Item -LiteralPath (Join-Path $binaries $relative) -Destination $stage
    Get-ChildItem -LiteralPath (Join-Path $triplet 'bin') -Filter '*.dll' | Copy-Item -Destination $stage
    Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination $stage
    Copy-Item -LiteralPath $platform -Destination "$stage/platforms"
    New-Item -ItemType Directory -Path "$stage/licenses" | Out-Null
    foreach ($notice in $notices) {
        $noticeName = $notice.Directory.Name + '-copyright.txt'
        Copy-Item -LiteralPath $notice.FullName -Destination (Join-Path "$stage/licenses" $noticeName)
    }
    if ($appName -eq 'client') {
        Copy-Item -LiteralPath (Join-Path $repo 'config/client.conf.in') -Destination "$stage/config/client.conf"
    } else {
        Copy-Item -LiteralPath (Join-Path $repo 'config/admin.conf') -Destination "$stage/config/admin.conf"
    }
    if ($PublicCertificate) { Copy-Item -LiteralPath $PublicCertificate -Destination "$stage/config/server.crt" }
    Copy-Item -LiteralPath (Join-Path $repo 'docs/PORTABLE.md') -Destination "$stage/START-HERE.md"
    $revision = & git -C $repo rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision.' }
    @("Source revision: $revision", 'Pre-release; hardware acceptance pending.',
      'Network switching requires opt-in server configuration and separately reviewed Linux helpers.') | Set-Content -LiteralPath "$stage/BUILD-INFO.txt" -Encoding utf8
    $zip = "$stage.zip"
    Compress-Archive -Path "$stage/*" -DestinationPath $zip
    Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Select-Object Path,Hash
}
