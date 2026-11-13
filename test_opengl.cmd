@echo off
rem Double-click: OpenGL stereo test on the Windows main screen, fullscreen.
echo OpenGL stereo test on the main screen. Put on the glasses.
echo Expected: left eye sees BLUE, right eye sees RED. Esc ends the test.
echo.
"%~dp0redblue.exe" --api=gl --fullscreen > "%~dp0result_opengl.txt" 2>&1
set CODE=%ERRORLEVEL%
if %CODE%==0 echo OpenGL accepted the stereo picture. Did each eye see its own color?
if %CODE%==1 echo OpenGL test failed with an error. Details: result_opengl.txt
if %CODE%==2 echo OpenGL offers no stereo on this screen. Details: result_opengl.txt
echo.
pause
