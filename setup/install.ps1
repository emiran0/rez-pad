# rez-pad setup: installs the host app for the current user (no admin needed except for the
# optional "Start with Windows" task, which asks once through UAC).
#
#   install.ps1                  install or update from the folder this script is in
#   install.ps1 -Uninstall       remove the app (keeps settings unless -RemoveSettings)
#
# Options: -StartWithWindows / -NoStartWithWindows skip the question, -NoLaunch does not start
# the app afterwards, -Quiet asks nothing (startup task left as it is).
#
# Install folder: %LOCALAPPDATA%\Programs\rez-pad. Settings: %APPDATA%\rez-pad (kept on update).
param(
    [switch]$Uninstall,
    [switch]$RemoveSettings,
    [switch]$StartWithWindows,
    [switch]$NoStartWithWindows,
    [switch]$NoLaunch,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$appName = "rez-pad"
$installDir = Join-Path $env:LOCALAPPDATA "Programs\rez-pad"
$exe = Join-Path $installDir "rez-pad.exe"
$shortcut = Join-Path ([Environment]::GetFolderPath("Programs")) "rez-pad.lnk"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\rez-pad"
$taskName = "rez-pad"
$configUrl = "http://127.0.0.1:47800"
$pawnIoUrl = "https://pawnio.eu/"

function Ask([string]$question, [bool]$default) {
    if ($Quiet) { return $default }
    $hint = if ($default) { "[Y/n]" } else { "[y/N]" }
    $a = Read-Host "$question $hint"
    if ([string]::IsNullOrWhiteSpace($a)) { return $default }
    return $a.Trim().ToLowerInvariant().StartsWith("y")
}

function Get-TaskCommand {
    # The command the startup task runs, or $null when there is no task.
    $xml = & schtasks.exe /Query /TN $taskName /XML 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    try {
        $doc = [xml]($xml -join "`n")
        return $doc.Task.Actions.Exec.Command.Trim('"')
    } catch {
        return ""
    }
}

function Stop-App {
    $running = Get-Process -Name $appName -ErrorAction SilentlyContinue
    if (-not $running) { return }
    Write-Host "Closing the running rez-pad..."
    # Ask it to exit cleanly (frees the COM and HTTP ports); any rez-pad.exe can send the signal.
    $sender = if (Test-Path $exe) { $exe } else { $script:sourceExe }
    if ($sender -and (Test-Path $sender)) {
        try { Start-Process -FilePath $sender -ArgumentList "--exit" -WindowStyle Hidden -Wait } catch { }
    }
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Process -Name $appName -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
    }
    $left = Get-Process -Name $appName -ErrorAction SilentlyContinue
    if ($left) {
        try { $left | Stop-Process -Force -ErrorAction Stop } catch { }
        Start-Sleep -Milliseconds 500
    }
    if (Get-Process -Name $appName -ErrorAction SilentlyContinue) {
        throw "rez-pad is still running (it may run as administrator). Right-click its tray icon, choose Exit, then run setup again."
    }
}

function Invoke-Elevated([string]$file, [string]$arguments) {
    try {
        $p = Start-Process -FilePath $file -ArgumentList $arguments -Verb RunAs -Wait -PassThru
        return $p.ExitCode -eq 0
    } catch {
        return $false  # UAC prompt declined
    }
}

function Register-Startup {
    $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    Write-Host "Registering the startup task (Windows asks for permission)..."
    if (Invoke-Elevated $exe "--register-startup `"$user`"") {
        Write-Host "  rez-pad starts at logon with full sensor access."
        return $true
    }
    Write-Host "  Not registered. You can turn it on later from the tray menu: Start with Windows."
    return $false
}

function Remove-App {
    $script:sourceExe = $null
    Stop-App
    $taskCmd = Get-TaskCommand
    if ($null -ne $taskCmd -and $taskCmd -ne "" -and $taskCmd -ine $exe) {
        Write-Host "The startup task runs another copy ($taskCmd); left as it is."
    } elseif ($null -ne $taskCmd) {
        Write-Host "Removing the startup task (Windows asks for permission)..."
        $ok = $false
        if (Test-Path $exe) { $ok = Invoke-Elevated $exe "--unregister-startup" }
        if (-not $ok) {
            $ok = Invoke-Elevated "schtasks.exe" "/Delete /TN $taskName /F"
        }
        if (-not $ok) { Write-Warning "The startup task 'rez-pad' is still there; delete it in Task Scheduler." }
    }
    if (Test-Path $shortcut) { Remove-Item -Force $shortcut }
    if (Test-Path $uninstallKey) { Remove-Item -Recurse -Force $uninstallKey }
    Set-Location $env:TEMP  # not inside the folder being removed
    if (Test-Path $installDir) { Remove-Item -Recurse -Force $installDir }
    $settings = Join-Path $env:APPDATA "rez-pad"
    if ($RemoveSettings -and (Test-Path $settings)) {
        Remove-Item -Recurse -Force $settings
        Write-Host "Removed settings ($settings)."
    } elseif (Test-Path $settings) {
        Write-Host "Settings kept in $settings (use -RemoveSettings to delete them)."
    }
    Write-Host "rez-pad removed."
}

function Install-App {
    $script:sourceExe = Join-Path $PSScriptRoot "rez-pad.exe"
    if (-not (Test-Path $script:sourceExe)) { throw "rez-pad.exe not found next to install.ps1" }
    $version = (Get-Item $script:sourceExe).VersionInfo.ProductVersion -replace '\+.*$', ''
    $updating = Test-Path $exe
    Write-Host ("{0} rez-pad {1} in {2}" -f ($(if ($updating) { "Updating" } else { "Installing" }), $version, $installDir))

    Stop-App
    New-Item -ItemType Directory -Force $installDir | Out-Null
    Copy-Item -Force $script:sourceExe $exe
    if ($PSCommandPath -ne (Join-Path $installDir "install.ps1")) {
        Copy-Item -Force $PSCommandPath (Join-Path $installDir "install.ps1")  # used for uninstall
    }

    # Start menu shortcut.
    $shell = New-Object -ComObject WScript.Shell
    $lnk = $shell.CreateShortcut($shortcut)
    $lnk.TargetPath = $exe
    $lnk.WorkingDirectory = $installDir
    $lnk.Description = "rez-pad desk companion host"
    $lnk.Save()

    # Entry in Settings > Apps.
    $uninstallCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$installDir\install.ps1`" -Uninstall"
    New-Item -Force $uninstallKey | Out-Null
    $values = @{
        DisplayName = "rez-pad"; DisplayVersion = $version; Publisher = "rez-lab"
        InstallLocation = $installDir; DisplayIcon = $exe; UninstallString = $uninstallCmd
    }
    foreach ($k in $values.Keys) { Set-ItemProperty -Path $uninstallKey -Name $k -Value $values[$k] }
    Set-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -Type DWord
    Set-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -Type DWord
    $size = [int]((Get-Item $exe).Length / 1KB)
    Set-ItemProperty -Path $uninstallKey -Name EstimatedSize -Value $size -Type DWord

    # Start with Windows: an elevated logon task, needed for CPU temperatures and fans.
    $taskCmd = Get-TaskCommand
    $registered = $false
    if ($null -ne $taskCmd -and $taskCmd -ieq $exe) {
        Write-Host "Start with Windows is on."
        $registered = $true
    } elseif ($null -ne $taskCmd) {
        # A task from an older copy (for example a development build): point it here.
        if ($NoStartWithWindows) {
            Write-Host "A startup task for another copy exists ($taskCmd); left as it is."
        } elseif ($StartWithWindows -or (-not $Quiet -and (Ask "The startup task runs $taskCmd. Point it to this install?" $true))) {
            $registered = Register-Startup
        }
    } elseif ($StartWithWindows) {
        $registered = Register-Startup
    } elseif (-not $NoStartWithWindows -and -not $Quiet) {
        Write-Host ""
        Write-Host "Start with Windows runs rez-pad at logon as administrator, which CPU temperatures and"
        Write-Host "motherboard fans need. Without it, start rez-pad from the Start menu (GPU, load and"
        Write-Host "media still work)."
        if (Ask "Start with Windows?" $true) { $registered = Register-Startup }
    }

    # PawnIO: the driver LibreHardwareMonitor uses for CPU temperature and motherboard sensors.
    $pawnIo = Test-Path "HKLM:\SYSTEM\CurrentControlSet\Services\PawnIO"
    if (-not $pawnIo) {
        Write-Host ""
        Write-Host "The PawnIO driver is not installed. CPU temperature and motherboard fans need it"
        Write-Host "(installed separately, from $pawnIoUrl)."
        if (Ask "Open the PawnIO page?" $false) { Start-Process $pawnIoUrl }
    }

    if (-not $NoLaunch) {
        Write-Host ""
        Write-Host "Starting rez-pad..."
        $started = $false
        if ($registered) {
            & schtasks.exe /Run /TN $taskName | Out-Null
            $started = $LASTEXITCODE -eq 0
        }
        if (-not $started) { Start-Process -FilePath $exe -WorkingDirectory $installDir }
        # Wait for the config page, then open it.
        $deadline = (Get-Date).AddSeconds(20)
        $up = $false
        while (-not $up -and (Get-Date) -lt $deadline) {
            try {
                Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 "$configUrl/api/status" | Out-Null
                $up = $true
            } catch {
                Start-Sleep -Milliseconds 500
            }
        }
        if ($up) {
            if (-not $updating) { Start-Process $configUrl }  # first install: show the editor
        } else {
            Write-Warning "rez-pad did not answer on $configUrl yet; open it from the tray icon."
        }
    }
    Write-Host ""
    Write-Host "Done. rez-pad lives in the tray (notification area); double-click it for the config page."
}

try {
    if ($Uninstall) { Remove-App } else { Install-App }
    $code = 0
} catch {
    Write-Host ""
    Write-Host "Setup failed: $($_.Exception.Message)" -ForegroundColor Red
    $code = 1
}
exit $code
