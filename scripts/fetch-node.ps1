# Downloads node.exe of a Node.js release for the package and checks it against the SHA-256 that nodejs.org publishes.
# The download is cached in build\node-<version>.exe
param([Parameter(Mandatory)][string]$Version, [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
$cache = Join-Path $PSScriptRoot "..\build\node-$Version.exe"
$sums = Invoke-RestMethod "https://nodejs.org/dist/$Version/SHASUMS256.txt"
$line = ($sums -split "`n") | Where-Object { $_ -match '\swin-x64/node\.exe$' }
if (-not $line) { throw "No checksum for win-x64/node.exe in $Version" }
$expected = ($line -split '\s+')[0]
if (-not (Test-Path $cache) -or (Get-FileHash $cache -Algorithm SHA256).Hash -ne $expected) {
    Invoke-WebRequest "https://nodejs.org/dist/$Version/win-x64/node.exe" -OutFile $cache
    if ((Get-FileHash $cache -Algorithm SHA256).Hash -ne $expected) {
        Remove-Item $cache
        throw "Checksum mismatch for node.exe $Version"
    }
}
Copy-Item $cache $Out -Force
