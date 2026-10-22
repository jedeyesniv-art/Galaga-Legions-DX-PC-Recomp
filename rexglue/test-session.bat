@echo off
rem Test session with detailed logs: frame stats every 10 s, a timeline around each slow frame (frame trace) and
rem the sampling profiler. Writes logs\galaga-<date_time>.log next to this file. Extra arguments are passed on,
rem e.g. test-session.bat --fullscreen=false
cd /d "%~dp0"
set "exe=galaga.exe"
if not exist "%exe%" set "exe=out\build\win-amd64-release\galaga.exe"
if not exist "%exe%" (
  echo galaga.exe not found - build it first.
  pause
  exit /b 1
)
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HHmmss"') do set "stamp=%%i"
if not exist logs mkdir logs
start "" "%exe%" --galaga_frame_trace=true --galaga_profile=true "--log_file=%~dp0logs\galaga-%stamp%.log" %*
