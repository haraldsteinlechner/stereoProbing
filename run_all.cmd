@echo off
rem Records probe and red/blue runs for every API into results\.
rem Usage: run_all.cmd [--adapter=N] [--output=N]   (indices from redblue --list)
rem Watch each run through the glasses and note what each eye sees.
setlocal
set HERE=%~dp0
set OUT=%HERE%results
if not exist "%OUT%" mkdir "%OUT%"

"%HERE%stereo_probe.exe" > "%OUT%\probe.txt" 2>&1
"%HERE%redblue.exe" --list > "%OUT%\list.txt" 2>&1
type "%OUT%\list.txt"

for %%A in (dxgi gl vk) do (
    for %%M in (windowed fullscreen) do (
        echo.
        echo === %%A %%M: 8 s, left eye blue, right eye red. Press a key to start.
        pause >nul
        if "%%M"=="fullscreen" (
            "%HERE%redblue.exe" --api=%%A --seconds=8 --fullscreen %* > "%OUT%\redblue_%%A_%%M.txt" 2>&1
        ) else (
            "%HERE%redblue.exe" --api=%%A --seconds=8 %* > "%OUT%\redblue_%%A_%%M.txt" 2>&1
        )
        type "%OUT%\redblue_%%A_%%M.txt" | findstr /b "result= error="
    )
)
echo.
echo Results in %OUT%
