// Matemática vetorial usada pela lógica do jogo.
//
// raymath.h é "header-only": com RAYMATH_STATIC_INLINE ele funciona sozinho,
// sem a biblioteca do raylib. Por isso os módulos de lógica (e os testes)
// podem usar Vector3/Matrix sem abrir janela e sem ligar com o raylib.
//
// Regra de inclusão: arquivos que usam raylib.h devem incluí-lo ANTES deste
// header, porque os dois definem os mesmos tipos (Vector3 etc.) e é o raylib.h
// que avisa o raymath.h para não redefini-los.
#ifndef CORE_VMATH_H
#define CORE_VMATH_H

#define RAYMATH_STATIC_INLINE
#include "raymath.h"

#endif
