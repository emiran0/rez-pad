@echo off
rem Installs or updates rez-pad for this user. Double-click to run.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
pause
