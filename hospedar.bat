@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ============================================
echo   HOSPEDAR PARTIDA  (tus amigos se unen con tu IP)
echo   Elige o crea un mundo en el menu; al entrar, arriba a la izquierda sale tu IP.
echo   Puerto: 25565  (en internet: Tailscale, o abrir ese puerto en el router)
echo ============================================
set /p NOMBRE=Tu nombre (Enter = CHlVOMAN): 
if "%NOMBRE%"=="" set NOMBRE=CHlVOMAN
build\Release\juego.exe --hospedar --nombre "%NOMBRE%"
pause
