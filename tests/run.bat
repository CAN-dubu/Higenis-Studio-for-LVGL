@echo off
rem Replays input scripts against build\HigenisStudio.exe in a hidden window
setlocal enabledelayedexpansion
cd /d "%~dp0"
set EXE=%~dp0..\build\HigenisStudio.exe
if not exist "%EXE%" ( echo build first: build.bat & exit /b 1 )
set FAILED=0
pushd "%TEMP%"
for %%f in ("%~dp0*.txt") do (
  start "" /wait "%EXE%" --script "%%f" --log "%TEMP%\higenis_%%~nf.log" --size 1520x940
  if errorlevel 1 ( set FAILED=1 & echo FAIL  %%~nf & findstr /b FAIL "%TEMP%\higenis_%%~nf.log" ) else ( echo ok    %%~nf )
)
popd
exit /b %FAILED%
