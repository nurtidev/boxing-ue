#!/usr/bin/env bash
# Сборка ядра боя вне UE (заглушка CoreMinimal) и прогон: детерминизм, 200 сидов, паритет с TS, постановка раунда (S-53).
# Linux/macOS аналог build.cmd. Компилятор — $CXX (по умолчанию clang++, иначе g++).
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-$(command -v clang++ || command -v g++)}"
OUT="${OUT:-${TMPDIR:-/tmp}/boxingue_sim}"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror -I. -I../../Source/BoxingUE/Public \
	sim_main.cpp ../../Source/BoxingUE/Private/BoxingFightCore.cpp ../../Source/BoxingUE/Private/FightStaging.cpp ../../Source/BoxingUE/Private/FightBot.cpp ../../Source/BoxingUE/Private/FightProfile.cpp -o "$OUT"
"$OUT"
