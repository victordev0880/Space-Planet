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

// --- orientações ------------------------------------------------------------
// Convenção do jogo para qualquer coisa que tenha frente (nave, câmera): no
// espaço local, a frente é -Z, a direita é +X e o topo é +Y. Um quatérnion
// guarda a orientação: girar um desses eixos por ele dá a direção no mundo.

static inline Vector3 quat_forward(Quaternion q)
{
    return Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, -1.0f }, q);
}

static inline Vector3 quat_right(Quaternion q)
{
    return Vector3RotateByQuaternion((Vector3){ 1.0f, 0.0f, 0.0f }, q);
}

static inline Vector3 quat_up(Quaternion q)
{
    return Vector3RotateByQuaternion((Vector3){ 0.0f, 1.0f, 0.0f }, q);
}

// Orientação que aponta a frente para `forward`, com o topo o mais perto
// possível de `up`. As duas direções não precisam ter comprimento 1.
static inline Quaternion quat_look(Vector3 forward, Vector3 up)
{
    Vector3 f = Vector3Normalize(forward);
    Vector3 r = Vector3CrossProduct(f, up);
    if (Vector3LengthSqr(r) < 1e-8f) {
        // `up` paralelo a `forward`: escolhe outro eixo qualquer para o topo.
        Vector3 other = (fabsf(f.y) < 0.9f) ? (Vector3){ 0.0f, 1.0f, 0.0f } : (Vector3){ 1.0f, 0.0f, 0.0f };
        r = Vector3CrossProduct(f, other);
    }
    r = Vector3Normalize(r);
    Vector3 u = Vector3CrossProduct(r, f);

    // Matriz de rotação com os três eixos nas colunas: direita, topo e trás.
    Matrix m = MatrixIdentity();
    m.m0 = r.x;  m.m1 = r.y;  m.m2 = r.z;
    m.m4 = u.x;  m.m5 = u.y;  m.m6 = u.z;
    m.m8 = -f.x; m.m9 = -f.y; m.m10 = -f.z;
    return QuaternionNormalize(QuaternionFromMatrix(m));
}

#endif
