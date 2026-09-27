param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory; existing files are never overwritten.' }
$dirty = & git -C $repo status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Commit reviewed changes before exporting.' }
$revision = & git -C $repo rev-parse HEAD
# Only reviewed source trees and generic examples. No local files or Git history.
$allowed = @('CMakeLists.txt','CMakePresets.json','README.md','.gitattributes','.gitignore',
    'admin','client','common','server','cmake','deploy','docs','.github','config/client.conf.in','gost.c','decr.c')
$files = & git -C $repo ls-files -- $allowed
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate tracked source.' }
$files = @($files | Where-Object { $_ -notlike 'docs/archive/*' })
foreach ($file in $files) {
    if ($file -match '(^|/)(storage|build|\.git)/|\.(key|crt|pem|db|log|exe|dll|zip)$') { throw "Forbidden export path: $file" }
    $source = Join-Path $repo $file
    if ((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Link not allowed: $file" }
    if (Select-String -LiteralPath $source -Pattern '-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----' -Quiet) {
        throw "Private key marker found: $file"
    }
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
foreach ($file in $files) {
    $destination = Join-Path $OutputDirectory $file
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo $file) -Destination $destination
}
# Templates needed by packaging and build; never copy device-specific configs.
Copy-Item -LiteralPath (Join-Path $repo 'config/client.conf.in') -Destination (Join-Path $OutputDirectory 'config/client.conf')
Copy-Item -LiteralPath (Join-Path $repo 'deploy/server.conf.example') -Destination (Join-Path $OutputDirectory 'config/server.conf')
@('ca_file=server.crt','client_cert=','client_key=','verify_peer=true') |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'config/admin.conf') -Encoding utf8
@("Development snapshot: $revision", 'Not a release. Hardware tests and installation integration remain.',
  'No Git repository created or published. Review licenses and secrets before publication.') |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'EXPORT-NOTES.txt') -Encoding utf8
$manifest = @(Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | ForEach-Object {
    [pscustomobject]@{File=$_.FullName.Substring([IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\').Length + 1); SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
$manifest | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'EXPORT-MANIFEST.csv') -NoTypeInformation -Encoding utf8
Write-Output "Prepared source snapshot $revision in $OutputDirectory; not published."
