// Ajudas compartilhadas entre arquivos de teste.
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "game/tuning.h"
#include "voxel/planet.h"

#define TEST_GRID 48

// Receita de um planeta pequeno (rápido de gerar) para os testes.
// O raio é 36% da grade (no jogo são 45%): em uma grade pequena o relevo
// ocupa proporcionalmente mais espaço e precisa de mais folga até a borda.
PlanetParams test_planet_params(int grid);

// O tuning real do jogo (data/tuning.txt), só com o planeta reduzido para a
// grade de teste. Registra uma falha se o arquivo não carregar.
Tuning test_tuning(void);

#endif
