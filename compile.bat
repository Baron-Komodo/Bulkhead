cls
@ECHO OFF
SETLOCAL EnableExtensions DisableDelayedExpansion
for /F %%a in ('echo prompt $E ^| cmd') do (
  set "ESC=%%a"
)

SETLOCAL EnableDelayedExpansion

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
echo !ESC![33m [33m compilation...50% [0m
cmake --build build -j
IF %ERRORLEVEL% EQU 0 (
echo !ESC![32m [32m Compilation...DONE [0m
echo !ESC![0m [0m [0m
  .\build\sea_builder.exe
) ELSE (
  echo !ESC! [101m [101m ERROR in compilation check above [0m
)

