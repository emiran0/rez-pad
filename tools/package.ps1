# Builds the release package: publish\rez-pad-<version>-win-x64.zip with
#   rez-pad.exe            self-contained single-file host (no .NET install needed)
#   install.ps1, Install.cmd, Uninstall.cmd
#   firmware\rez-pad-firmware-<version>.bin   full flash image (write at 0x0)
#   README.txt, LICENSE.txt, THIRD-PARTY-NOTICES.txt
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

[xml]$props = Get-Content (Join-Path $root "host\Directory.Build.props")
$version = ($props.Project.PropertyGroup | Where-Object { $_.Version } | Select-Object -First 1).Version
Write-Host "rez-pad $version"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Write-Host "Publishing host..."
& dotnet publish (Join-Path $root "host\src\RezPad.Host\RezPad.Host.csproj") -c Release `
    -p:PublishSingleFile=true --self-contained true -o (Join-Path $out "host-bin")
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }
Copy-Item (Join-Path $out "host-bin\rez-pad.exe") $stage

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

Copy-Item (Join-Path $root "setup\install.ps1") $stage
Copy-Item (Join-Path $root "setup\Install.cmd") $stage
Copy-Item (Join-Path $root "setup\Uninstall.cmd") $stage
Copy-Item (Join-Path $root "setup\README.txt") $stage
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "THIRD-PARTY-NOTICES.md") (Join-Path $stage "THIRD-PARTY-NOTICES.txt")

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
