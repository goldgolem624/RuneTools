@echo off
REM Builds the CS2 extractor sidecar from the bundled source into ..\dist.
REM Requires Node.js (https://nodejs.org). Run once; the launcher needs only the
REM built dist\cs2export.js plus node.exe next to it.
setlocal
cd /d "%~dp0src" || exit /b 1

if not exist node_modules (
    echo Installing dependencies ^(first run only^)...
    call npm install || exit /b 1
)

echo Building cs2export...
call npx webpack --config webpack.config.js || exit /b 1

echo.
echo Done. Output: %~dp0dist\cs2export.js
endlocal
