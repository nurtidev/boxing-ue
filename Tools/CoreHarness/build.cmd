@echo off
rem Сборка ядра боя вне UE (заглушка CoreMinimal) и прогон: детерминизм, 200 сидов, паритет с TS.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd %~dp0
cl /nologo /std:c++20 /O2 /EHsc /W4 /fp:precise /I. /I..\..\Source\BoxingUE\Public sim_main.cpp ..\..\Source\BoxingUE\Private\BoxingFightCore.cpp /Fe:sim.exe /Fo:%TEMP%\ && sim.exe
popd
