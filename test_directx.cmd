@echo off
rem Double-click: DirectX stereo test on the Windows main screen, fullscreen.
echo DirectX stereo test on the main screen. Put on the glasses.
echo Expected: left eye sees BLUE, right eye sees RED. Esc ends the test.
echo.
"%~dp0redblue.exe" --api=dxgi --fullscreen > "%~dp0result_directx.txt" 2>&1
set CODE=%ERRORLEVEL%
if %CODE%==0 echo DirectX accepted the stereo picture. Did each eye see its own color?
if %CODE%==1 echo DirectX test failed with an error. Details: result_directx.txt
if %CODE%==2 echo DirectX offers no stereo on this screen. Details: result_directx.txt
echo.
pause
