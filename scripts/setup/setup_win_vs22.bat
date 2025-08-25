@echo off

pushd ..\..
extern\source\premake\windows\premake5.exe --file=premake5.lua vs2022
popd
pause