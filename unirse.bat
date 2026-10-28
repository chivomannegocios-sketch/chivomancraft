@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ============================================
echo   UNIRSE A UNA PARTIDA
echo ============================================
set /p IP=IP del anfitrion (ej. 192.168.1.5; en la misma PC: 127.0.0.1): 
set /p NOMBRE=Tu nombre (Enter = Amigo): 
if "%NOMBRE%"=="" set NOMBRE=Amigo
build\Release\juego.exe --unir %IP% --nombre "%NOMBRE%"
pause
