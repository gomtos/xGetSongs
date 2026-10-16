@echo off
if not exist "%~dp0dist\xGetSongs.exe" call "%~dp0package-exe.bat" || exit /b 1
start "" "%~dp0dist\xGetSongs.exe"
