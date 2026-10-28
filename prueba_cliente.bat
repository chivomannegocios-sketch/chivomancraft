@echo off
cd /d "%~dp0"
set PRUEBA_RED_VEH=1
build\Release\juego.exe --unir 127.0.0.1 --nombre Amigo
