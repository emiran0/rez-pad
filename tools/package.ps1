# Builds the release package: publish\rez-pad-<version>-win-x64.zip with
#   rez-pad.exe            self-contained single-file host (no .NET install needed), with the
#                          firmware image embedded (Device > Firmware updates the board)
#   install.ps1, Install.cmd, Uninstall.cmd
#   firmware\rez-pad-firmware-<version>.bin   full flash image (write at 0x0)
#   README.txt, CHANGELOG.txt, LICENSE.txt, THIRD-PARTY-NOTICES.txt
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-SkipFirmware]
#
# Run from PowerShell or cmd (PlatformIO refuses Git Bash).
param(
    [switch]$SkipFirmware
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "publish"
$stage = Join-Path $out "rez-pad"

# One version for everything: the VERSION file (docs/versioning.md).
$version = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
Write-Host "rez-pad $version"
Push-Location $root
try {
    # A release is built from a clean, tagged commit; say so when it is not (dev zips are fine).
    if (git status --porcelain --untracked-files=no) {
        Write-Warning "uncommitted changes: this zip is a dev build ($version.dirty)"
    }
    $tags = @(git tag --points-at HEAD)
    if ($tags -notcontains "v$version") {
        Write-Warning "HEAD is not tagged v${version}; tag the release commit (git tag -a v$version -m ""rez-pad $version"")"
    }
} finally {
    Pop-Location
}

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

if (-not $SkipFirmware) {
    Write-Host "Building firmware..."
    Push-Location (Join-Path $root "firmware")
    try {
        & pio run -e rezpad
        if ($LASTEXITCODE -ne 0) { throw "firmware build failed" }
    } finally {
        Pop-Location
    }
    $fw = Join-Path $stage "firmware"
    New-Item -ItemType Directory -Force $fw | Out-Null
    Copy-Item (Join-Path $root "firmware\.pio\build\rezpad\firmware.factory.bin") (Join-Path $fw "rez-pad-firmware-$version.bin")
}

# The host embeds the firmware image built above (Device > Firmware updates the board from the
# web page), so the firmware is built first.
if (-not (Test-Path (Join-Path $root "firmware\.pio\build\rezpad\firmware.factory.bin"))) {
    Write-Warning "no firmware image: this rez-pad.exe cannot update the board (build the firmware first)"
}
Write-Host "Publishing host..."
& dotnet publish (Join-Path $root "host\src\RezPad.Host\RezPad.Host.csproj") -c Release `
    -p:PublishSingleFile=true --self-contained true -o (Join-Path $out "host-bin")
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }
Copy-Item (Join-Path $out "host-bin\rez-pad.exe") $stage

Copy-Item (Join-Path $root "setup\install.ps1") $stage
Copy-Item (Join-Path $root "setup\Install.cmd") $stage
Copy-Item (Join-Path $root "setup\Uninstall.cmd") $stage
Copy-Item (Join-Path $root "setup\README.txt") $stage
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "THIRD-PARTY-NOTICES.md") (Join-Path $stage "THIRD-PARTY-NOTICES.txt")
Copy-Item (Join-Path $root "CHANGELOG.md") (Join-Path $stage "CHANGELOG.txt")

$zip = Join-Path $out "rez-pad-$version-win-x64.zip"
if (Test-Path $zip) {
    try {
        Remove-Item -Force $zip -ErrorAction Stop
    } catch {
        throw "Cannot replace $zip, it is open in another program (close it there and run again)."
    }
}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Wrote $zip ($mb MB)"
