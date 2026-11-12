#include "raylib.h"
#include "rlgl.h"

#include "render/planet_render.h"

#include <string.h>

#include "core/vmath.h"

// Folga dos buffers de um chunk em relação ao que a malha ocupa hoje.
#define CAPACITY_SLACK_PERCENT 50
#define CAPACITY_MIN_VERTICES  64
#define CAPACITY_MIN_INDICES   192

// Modo sem shader: calcula a cor final de cada vértice (cor do material x luz
// que chega nele). É a mesma conta que o shader faz por pixel, sem o detalhe,
// o reflexo e o pulso.
static void bake_lighting(const MeshData *d, const Tuning *t, uint8_t *out)
{
    Vector3 sun = tuning_sun_direction(t);
    float wrap = t->sun_wrap;
    float sun_color[3] = { t->light_sun.r / 255.0f, t->light_sun.g / 255.0f, t->light_sun.b / 255.0f };
    float ambient[3] = { t->light_ambient.r / 255.0f, t->light_ambient.g / 255.0f, t->light_ambient.b / 255.0f };

    for (int v = 0; v < d->vertex_count; v++) {
        const float *n = &d->normals[v * 3];
        const uint8_t *albedo = &d->colors[v * 4];
        Vector3 pos = { d->positions[v * 3], d->positions[v * 3 + 1], d->positions[v * 3 + 2] };

        // Sombra do próprio planeta: do lado oposto ao Sol é noite, não
        // importa para onde a superfície aponta (ver planet.fs).
        float side = Vector3DotProduct(Vector3Normalize(pos), sun);
        float dayside = Clamp((side + 0.10f) / 0.22f, 0.0f, 1.0f);
        dayside = dayside * dayside * (3.0f - 2.0f * dayside);

        // Luz difusa: quanto mais a superfície está de frente para o Sol, mais
        // clara. O "wrap" deixa a luz passar um pouco do limite entre dia e
        // noite, o que suaviza o terminador.
        float facing = n[0] * sun.x + n[1] * sun.y + n[2] * sun.z;
        float diffuse = Clamp((facing + wrap) / (1.0f + wrap), 0.0f, 1.0f) * dayside;

        // O fundo das crateras recebe menos luz.
        float cavity = 1.0f - t->cavity_darkness * d->params[v * 2 + 1];

        // Materiais emissivos (manto, núcleo) ignoram a luz: brilham sozinhos.
        float emission = albedo[3] / 255.0f;

        for (int c = 0; c < 3; c++) {
            float base = (float)albedo[c];
            float lit = base * (ambient[c] + sun_color[c] * diffuse) * cavity;
            float value = lit + (base - lit) * emission;
            out[v * 4 + c] = (uint8_t)Clamp(value + 0.5f, 0.0f, 255.0f);
        }
        out[v * 4 + 3] = 255;
    }
}

static int with_slack(int count, int minimum)
{
    int capacity = count + count * CAPACITY_SLACK_PERCENT / 100;
    return (capacity < minimum) ? minimum : capacity;
}

// Envia a malha de um chunk para a placa de vídeo.
static void chunk_upload(ChunkMesh *c, const MeshData *d, const uint8_t *colors)
{
    c->index_count = d->index_count;
    c->vertex_count = d->vertex_count;
    if (d->index_count == 0) return;   // nada a desenhar; os buffers, se existem, ficam para depois

    bool fits = c->loaded && d->vertex_count <= c->vertex_capacity && d->index_count <= c->index_capacity;

    if (fits) {
        // Caminho rápido: escreve por cima dos dados antigos.
        UpdateMeshBuffer(c->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, d->positions,
                         d->vertex_count * 3 * (int)sizeof(float), 0);
        UpdateMeshBuffer(c->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD, d->params,
                         d->vertex_count * 2 * (int)sizeof(float), 0);
        UpdateMeshBuffer(c->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, d->normals,
                         d->vertex_count * 3 * (int)sizeof(float), 0);
        UpdateMeshBuffer(c->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, colors, d->vertex_count * 4, 0);
        UpdateMeshBuffer(c->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_INDICES, d->indices,
                         d->index_count * (int)sizeof(unsigned short), 0);
        return;
    }

    // Não coube (ou é a primeira vez): cria buffers novos, com folga.
    if (c->loaded) UnloadMesh(c->mesh);

    int vertex_capacity = with_slack(d->vertex_count, CAPACITY_MIN_VERTICES);
    int index_capacity = with_slack(d->index_count, CAPACITY_MIN_INDICES);
    index_capacity -= index_capacity % 3;

    Mesh mesh = { 0 };
    mesh.vertexCount = vertex_capacity;
    mesh.triangleCount = index_capacity / 3;
    mesh.vertices = MemAlloc((unsigned int)vertex_capacity * 3 * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned int)vertex_capacity * 2 * sizeof(float));
    mesh.normals = MemAlloc((unsigned int)vertex_capacity * 3 * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)vertex_capacity * 4);
    mesh.indices = MemAlloc((unsigned int)index_capacity * sizeof(unsigned short));

    memcpy(mesh.vertices, d->positions, (size_t)d->vertex_count * 3 * sizeof(float));
    memcpy(mesh.texcoords, d->params, (size_t)d->vertex_count * 2 * sizeof(float));
    memcpy(mesh.normals, d->normals, (size_t)d->vertex_count * 3 * sizeof(float));
    memcpy(mesh.colors, colors, (size_t)d->vertex_count * 4);
    memcpy(mesh.indices, d->indices, (size_t)d->index_count * sizeof(unsigned short));

    UploadMesh(&mesh, true);   // true = buffers "dinâmicos", que serão reescritos

    c->mesh = mesh;
    c->vertex_capacity = vertex_capacity;
    c->index_capacity = index_capacity;
    c->loaded = true;
}

// Gera a malha de um chunk e a envia. Retorna quantos triângulos ela tem.
static int remesh_chunk(PlanetRender *r, const Planet *p, const Tuning *t, const MeshStyle *style,
                        int cx, int cy, int cz, int index)
{
    mesher_build_chunk(p, cx, cy, cz, style, &r->scratch);

    const uint8_t *colors = r->scratch.colors;   // com shader: cor do material e emissão
    if (!r->shaded) {
        bake_lighting(&r->scratch, t, r->lit_colors);
        colors = r->lit_colors;
    }
    chunk_upload(&r->chunks[index], &r->scratch, colors);
    return r->scratch.index_count / 3;
}

bool planet_render_init(PlanetRender *r, const Planet *p, const Tuning *t, bool use_shaders)
{
    *r = (PlanetRender){ 0 };
    r->use_shaders = use_shaders;

    // Memória de trabalho: a malha do pior caso de um chunk e suas cores.
    size_t scratch_bytes = (size_t)MESHER_MAX_VERTICES * (3 * sizeof(float) * 2 + 2 * sizeof(float) + 4 + 4)
                         + (size_t)MESHER_MAX_INDICES * sizeof(uint16_t) + 4096;
    if (!arena_init(&r->arena, scratch_bytes)) return false;
    if (!mesh_data_init(&r->scratch, &r->arena)) return false;
    r->lit_colors = ARENA_ARRAY(&r->arena, uint8_t, MESHER_MAX_VERTICES * 4);
    if (r->lit_colors == NULL) return false;

    int n = p->chunks_per_axis;
    r->chunk_count = n * n * n;
    r->chunks = MemAlloc((unsigned int)r->chunk_count * sizeof(ChunkMesh));
    r->meshed_version = MemAlloc((unsigned int)r->chunk_count * sizeof(uint32_t));
    if (r->chunks == NULL || r->meshed_version == NULL) return false;

    r->material = LoadMaterialDefault();
    planet_render_reload_shader(r);
    planet_render_rebuild(r, p, t);
    return true;
}

void planet_render_reload_shader(PlanetRender *r)
{
    planet_shader_unload(&r->shader);
    r->shaded = r->use_shaders && planet_shader_load(&r->shader);
}

void planet_render_rebuild(PlanetRender *r, const Planet *p, const Tuning *t)
{
    MeshStyle style = tuning_mesh_style(t);
    int n = p->chunks_per_axis;
    double start = GetTime();

    r->mesh_count = 0;
    r->vertex_total = 0;
    r->triangle_total = 0;

    int index = 0;
    for (int cz = 0; cz < n; cz++) {
        for (int cy = 0; cy < n; cy++) {
            for (int cx = 0; cx < n && index < r->chunk_count; cx++, index++) {
                int triangles = remesh_chunk(r, p, t, &style, cx, cy, cz, index);
                if (triangles > 0) {
                    r->mesh_count++;
                    r->vertex_total += r->scratch.vertex_count;
                    r->triangle_total += triangles;
                }
                r->meshed_version[index] = p->chunk_version[index];
            }
        }
    }

    r->backlog = 0;
    r->rebuild_ms = (GetTime() - start) * 1000.0;
    TraceLog(LOG_INFO, "planeta: %d malhas, %d triangulos, geradas em %.1f ms (%s)", r->mesh_count,
             r->triangle_total, r->rebuild_ms, r->shaded ? "com shader" : "sem shader");
}

void planet_render_update(PlanetRender *r, const Planet *p, const Tuning *t, Vector3 camera_pos)
{
    const int n = p->chunks_per_axis;
    const float chunk = (float)p->chunk_size;
    double start = GetTime();
    MeshStyle style;
    bool style_ready = false;
    int done = 0;

    while (done < t->remesh_max_chunks) {
        // Entre os chunks desatualizados, pega o mais próximo da câmera: é
        // onde o jogador está olhando, e onde um atraso apareceria.
        int best = -1;
        float best_distance = 0.0f;
        for (int i = 0; i < r->chunk_count; i++) {
            if (r->meshed_version[i] == p->chunk_version[i]) continue;
            Vector3 center = {
                ((float)(i % n) + 0.5f) * chunk - p->center,
                ((float)((i / n) % n) + 0.5f) * chunk - p->center,
                ((float)(i / (n * n)) + 0.5f) * chunk - p->center,
            };
            float distance = Vector3DistanceSqr(center, camera_pos);
            if (best < 0 || distance < best_distance) {
                best = i;
                best_distance = distance;
            }
        }
        if (best < 0) break;   // tudo em dia

        if (!style_ready) {
            style = tuning_mesh_style(t);
            style_ready = true;
        }

        ChunkMesh *c = &r->chunks[best];
        int old_vertices = c->vertex_count, old_indices = c->index_count;
        remesh_chunk(r, p, t, &style, best % n, (best / n) % n, best / (n * n), best);
        r->meshed_version[best] = p->chunk_version[best];

        r->vertex_total += c->vertex_count - old_vertices;
        r->triangle_total += (c->index_count - old_indices) / 3;
        r->mesh_count += (c->index_count > 0) - (old_indices > 0);
        done++;

        if ((GetTime() - start) * 1000.0 >= (double)t->remesh_budget_ms) break;
    }

    int backlog = 0;
    for (int i = 0; i < r->chunk_count; i++) {
        if (r->meshed_version[i] != p->chunk_version[i]) backlog++;
    }

    r->remeshed_last_frame = done;
    r->remesh_ms_last_frame = (done > 0) ? (GetTime() - start) * 1000.0 : 0.0;
    r->backlog = backlog;
    r->remeshed_total += done;
    r->remesh_ms_total += r->remesh_ms_last_frame;
    if (done > r->remeshed_max_frame) r->remeshed_max_frame = done;
    if (backlog > r->backlog_max) r->backlog_max = backlog;
    if (r->remesh_ms_last_frame > r->remesh_ms_max) r->remesh_ms_max = r->remesh_ms_last_frame;
}

void planet_render_draw(const PlanetRender *r, const Tuning *t, Vector3 camera_pos, float time, bool wireframe)
{
    // Equivale a chamar DrawMesh para cada chunk, mas preparando o shader, as
    // matrizes e a textura uma única vez. Com centenas de chunks, repetir essa
    // preparação a cada um custaria mais que o desenho em si.
    rlDrawRenderBatchActive();

    Shader shader;
    if (r->shaded) {
        shader = r->shader.shader;
        planet_shader_begin(&r->shader, t, camera_pos, time);
    } else {
        shader = r->material.shader;
        rlEnableShader(shader.id);

        float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        rlSetUniform(shader.locs[SHADER_LOC_COLOR_DIFFUSE], white, SHADER_UNIFORM_VEC4, 1);

        int slot = 0;
        rlActiveTextureSlot(slot);
        rlEnableTexture(rlGetTextureIdDefault());   // textura branca: sobra só a cor do vértice
        rlSetUniform(shader.locs[SHADER_LOC_MAP_DIFFUSE], &slot, SHADER_UNIFORM_INT, 1);
    }

    // Os vértices já estão em coordenadas do mundo, então só faltam a câmera
    // (view) e a perspectiva (projection).
    Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MVP], mvp);

    if (wireframe) rlEnableWireMode();

    for (int i = 0; i < r->chunk_count; i++) {
        const ChunkMesh *c = &r->chunks[i];
        if (!c->loaded || c->index_count == 0) continue;
        rlEnableVertexArray(c->mesh.vaoId);
        rlDrawVertexArrayElements(0, c->index_count, 0);
    }

    if (wireframe) rlDisableWireMode();

    rlDisableVertexArray();
    if (!r->shaded) rlDisableTexture();
    rlDisableShader();
}

void planet_render_unload(PlanetRender *r)
{
    if (r->chunks != NULL) {
        for (int i = 0; i < r->chunk_count; i++) {
            if (r->chunks[i].loaded) UnloadMesh(r->chunks[i].mesh);
        }
        MemFree(r->chunks);
    }
    if (r->meshed_version != NULL) MemFree(r->meshed_version);
    if (r->material.maps != NULL) UnloadMaterial(r->material);
    planet_shader_unload(&r->shader);
    arena_free(&r->arena);
    *r = (PlanetRender){ 0 };
}
