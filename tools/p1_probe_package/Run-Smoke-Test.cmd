@echo off
setlocal
cd /d "%~dp0"

echo ============================================
echo  Arssyut P1 Native Capture Smoke Test
echo  Duration: 10 seconds
echo  Output cadence: 60 FPS
echo ============================================
echo.

if not exist "arssyut_p1_probe.exe" (
  echo ERROR: arssyut_p1_probe.exe is missing from this folder.
  echo.
  pause
  exit /b 2
)

echo Starting capture test...
echo Please keep this desktop session unlocked and active.
echo.

"arssyut_p1_probe.exe" 10 60 > "p1-smoke-test.log" 2>&1
set "rc=%ERRORLEVEL%"

type "p1-smoke-test.log"

echo.
if "%rc%"=="0" (
  echo ============================================
  echo  RESULT: PASS
  echo  Log saved as p1-smoke-test.log
  echo ============================================
) else (
  echo ============================================
  echo  RESULT: FAIL ^(exit code %rc%^)
  echo  Please send p1-smoke-test.log for analysis.
  echo ============================================
)

echo.
pause
exit /b %rc%
