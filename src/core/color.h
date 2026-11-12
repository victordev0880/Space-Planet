// Cor RGB de 8 bits por canal, para a lógica (que não conhece o tipo Color
// do raylib).
#ifndef CORE_COLOR_H
#define CORE_COLOR_H

#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} Rgb8;

#endif
