@echo off
setlocal
cd /d "%~dp0"

echo ============================================
echo  Arssyut P1 Native Capture Soak
echo  Duration: 30 minutes
echo  Output cadence: 60 FPS
echo ============================================
echo.
echo Keep Windows unlocked, monitor active, and avoid sleep/RDP changes.
echo.

if not exist "arssyut_p1_probe.exe" (
  echo ERROR: arssyut_p1_probe.exe is missing from this folder.
  echo.
  pause
  exit /b 2
)

"arssyut_p1_probe.exe" 1800 60 > "p1-soak-30min-60fps.log" 2>&1
set "rc=%ERRORLEVEL%"

type "p1-soak-30min-60fps.log"

echo.
if "%rc%"=="0" (
  echo ============================================
  echo  RESULT: PASS
  echo  Log saved as p1-soak-30min-60fps.log
  echo ============================================
) else (
  echo ============================================
  echo  RESULT: FAIL ^(exit code %rc%^)
  echo  Please send p1-soak-30min-60fps.log for analysis.
  echo ============================================
)

echo.
pause
exit /b %rc%
