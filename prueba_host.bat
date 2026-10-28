@echo off
cd /d "%~dp0"
set MUNDO=prueba_red2
set SEMILLA=7
set PRUEBA_AVION=1
set PRUEBA_SINPILOTO=1
set PRUEBA_RED_VEH=1
build\Release\juego.exe --hospedar --nombre CHlVOMAN
