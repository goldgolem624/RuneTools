@echo off
rem Standalone tests. No project reference, no dependencies beyond the compiler:
rem   tests\run-tests.cmd
rem Each test is a single .cpp with a main() that returns non-zero on failure.
setlocal

where cl.exe >nul 2>nul
if not errorlevel 1 goto :haveCl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio with the C++ toolset was not found.
    exit /b 1
)
rem vswhere output goes through a temp file: the "(x86)" in its path breaks a for /f set.
set "VSPATH_FILE=%TEMP%\rtx_tests_vspath.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSPATH_FILE%"
set /p VSPATH=<"%VSPATH_FILE%"
del "%VSPATH_FILE%" >nul 2>nul
if not defined VSPATH (
    echo Visual Studio with the C++ toolset was not found.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

:haveCl
set "HERE=%~dp0"
set "OUT=%HERE%..\x64\tests"
if not exist "%OUT%" mkdir "%OUT%"

set FAILED=0
for %%t in ("%HERE%*_test.cpp") do call :runOne "%%t"

echo(
if "%FAILED%"=="1" (
    echo TESTS FAILED
    exit /b 1
)
echo All tests passed.
exit /b 0

:runOne
echo(
echo === %~n1 ===
cl /nologo /W4 /WX /EHsc /std:c++17 /I"%HERE%..\companion" "%~1" /Fo"%OUT%\\" /Fe"%OUT%\%~n1.exe" >nul
if errorlevel 1 (
    echo   BUILD FAILED
    set FAILED=1
    goto :eof
)
"%OUT%\%~n1.exe"
if errorlevel 1 set FAILED=1
goto :eof
