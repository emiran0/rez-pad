@echo off
rem Removes rez-pad (settings in %APPDATA%\rez-pad are kept; add -RemoveSettings to delete them).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
pause
