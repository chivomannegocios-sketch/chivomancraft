@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem SUB 1 ahora se elige desde el menu principal del juego (boton MUNDO SUB). Este archivo solo compila y abre la version SUB 1.
if not exist build\Release\juego_sub1.exe (
  echo Primero ejecuta compilar_y_jugar.bat una vez: compila las dos versiones.
  pause
  goto :eof
)
call build\Release\juego_sub1.exe
