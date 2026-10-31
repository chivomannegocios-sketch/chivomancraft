@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ============================================
echo   JUEGO VOXEL - compilando (la primera vez baja GLFW, tarda un poco)
echo ============================================

set GEN=
rmdir /s /q build 2>nul
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 >nul 2>&1
if not errorlevel 1 set GEN=Visual Studio 18 2026
if not defined GEN (
  rmdir /s /q build 2>nul
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 >nul 2>&1
  if not errorlevel 1 set GEN=Visual Studio 17 2022
)
if not defined GEN (
  rmdir /s /q build 2>nul
  cmake -S . -B build -G "Visual Studio 16 2019" -A x64 >nul 2>&1
  if not errorlevel 1 set GEN=Visual Studio 16 2019
)
if not defined GEN (
  echo.
  echo *** No encontre Visual Studio con las herramientas de C++ ***
  echo Abre "Visual Studio Installer" y en Visual Studio 2026 Community marca
  echo "Desarrollo para el escritorio con C++" y dale Instalar/Modificar.
  goto fin
)
echo Usando: %GEN%
cmake -S . -B build -G "%GEN%" -A x64
if errorlevel 1 goto error
cmake --build build --config Release --target juego
if errorlevel 1 goto error
cmake --build build --config Release --target juego_sub1
if errorlevel 1 goto error
echo.
echo Listo. Abriendo el juego...
call build\Release\juego.exe
goto fin
:error
echo.
echo *** ALGO FALLO *** Copia lo que salio arriba y pasamelo.
:fin
pause
