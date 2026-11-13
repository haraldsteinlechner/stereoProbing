@echo off
setlocal
set ROOT=%~dp0
cmake -S "%ROOT%." -B "%ROOT%build" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build "%ROOT%build" --config Release
if errorlevel 1 exit /b 1
ctest --test-dir "%ROOT%build" -C Release --output-on-failure
if errorlevel 1 exit /b 1

set DIST=%ROOT%build\dist\stereo-tools
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"
copy /y "%ROOT%build\Release\stereo_probe.exe" "%DIST%" >nul
copy /y "%ROOT%build\Release\redblue.exe" "%DIST%" >nul
copy /y "%ROOT%README.md" "%DIST%" >nul
copy /y "%ROOT%LICENSE" "%DIST%" >nul
copy /y "%ROOT%run_all.cmd" "%DIST%" >nul
copy /y "%ROOT%test_*.cmd" "%DIST%" >nul
mkdir "%DIST%\edid"
copy /y "%ROOT%edid\edid_stereo.py" "%DIST%\edid" >nul
if exist "%DIST%.zip" del "%DIST%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%DIST%\*' -DestinationPath '%DIST%.zip'"
if errorlevel 1 exit /b 1
echo.
echo Package: %DIST%.zip
