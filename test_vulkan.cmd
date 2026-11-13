@echo off
rem Double-click: Vulkan stereo test on the Windows main screen, fullscreen.
echo Vulkan stereo test on the main screen. Put on the glasses.
echo Expected: left eye sees BLUE, right eye sees RED. Esc ends the test.
echo.
"%~dp0redblue.exe" --api=vk --fullscreen > "%~dp0result_vulkan.txt" 2>&1
set CODE=%ERRORLEVEL%
if %CODE%==0 echo Vulkan accepted the stereo picture. Did each eye see its own color?
if %CODE%==1 echo Vulkan test failed with an error. Details: result_vulkan.txt
if %CODE%==2 echo Vulkan offers no stereo on this screen. Details: result_vulkan.txt
echo.
pause
