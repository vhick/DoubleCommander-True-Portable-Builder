@echo off
rem Double-click to safely back up/migrate custom plugins before replacing App.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Migrate-v1.0-Addons.ps1"
if errorlevel 1 (
  echo Migration stopped. The existing app was not intentionally deleted or modified.
  pause
)
