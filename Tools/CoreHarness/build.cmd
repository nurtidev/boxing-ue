@echo off
rem Build the fight core outside UE (CoreMinimal stub) and run: determinism, 200 seeds, TS parity.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0"
cl /nologo /std:c++20 /O2 /EHsc /W4 /fp:precise /I. /I..\..\Source\BoxingUE\Public sim_main.cpp ..\..\Source\BoxingUE\Private\BoxingFightCore.cpp /Fe:"%~dp0sim.exe" /Fo:"%TEMP%\\" && "%~dp0sim.exe"
popd
