#include "raylib.h"
#include "rlgl.h"

#include "render/shaders.h"

#include "core/vmath.h"

#define PLANET_VS "assets/shaders/planet.vs"
#define PLANET_FS "assets/shaders/planet.fs"

bool planet_shader_load(PlanetShader *s)
{
    *s = (PlanetShader){ 0 };

    if (!FileExists(PLANET_VS) || !FileExists(PLANET_FS)) {
        TraceLog(LOG_WARNING, "shader do planeta nao encontrado em assets/shaders; usando o modo sem shader");
        return false;
    }

    Shader shader = LoadShader(PLANET_VS, PLANET_FS);

    // Se a compilação falha, o raylib avisa no log e devolve o shader padrão
    // (ou uma mistura do nosso com o padrão). As duas situações se revelam do
    // mesmo jeito: a variável "sunDir", que só o nosso shader de fragmento
    // usa, não existe no programa resultante.
    int loc_sun_dir = GetShaderLocation(shader, "sunDir");
    if (shader.id == rlGetShaderIdDefault() || loc_sun_dir < 0) {
        TraceLog(LOG_WARNING, "shader do planeta nao compilou; usando o modo sem shader");
        if (shader.id != rlGetShaderIdDefault()) UnloadShader(shader);
        return false;
    }

    s->shader = shader;
    s->loc_sun_dir = loc_sun_dir;
    s->loc_sun_color = GetShaderLocation(shader, "sunColor");
    s->loc_ambient = GetShaderLocation(shader, "ambientColor");
    s->loc_sun_wrap = GetShaderLocation(shader, "sunWrap");
    s->loc_camera = GetShaderLocation(shader, "cameraPos");
    s->loc_time = GetShaderLocation(shader, "time");
    s->loc_detail_scale = GetShaderLocation(shader, "detailScale");
    s->loc_detail_strength = GetShaderLocation(shader, "detailStrength");
    s->loc_bump_strength = GetShaderLocation(shader, "bumpStrength");
    s->loc_spec_strength = GetShaderLocation(shader, "specStrength");
    s->loc_spec_power = GetShaderLocation(shader, "specPower");
    s->loc_pulse_amount = GetShaderLocation(shader, "pulseAmount");
    s->loc_pulse_speed = GetShaderLocation(shader, "pulseSpeed");
    s->loc_cavity_darkness = GetShaderLocation(shader, "cavityDarkness");
    s->ready = true;
    return true;
}

void planet_shader_unload(PlanetShader *s)
{
    if (s->ready) UnloadShader(s->shader);
    *s = (PlanetShader){ 0 };
}

static void set_float(int loc, float value)
{
    rlSetUniform(loc, &value, SHADER_UNIFORM_FLOAT, 1);
}

static void set_vec3(int loc, Vector3 value)
{
    float v[3] = { value.x, value.y, value.z };
    rlSetUniform(loc, v, SHADER_UNIFORM_VEC3, 1);
}

static Vector3 color_to_vec3(Rgb8 c)
{
    return (Vector3){ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
}

void planet_shader_begin(const PlanetShader *s, const Tuning *t, Vector3 camera_pos, float time)
{
    rlEnableShader(s->shader.id);

    set_vec3(s->loc_sun_dir, tuning_sun_direction(t));
    set_vec3(s->loc_sun_color, color_to_vec3(t->light_sun));
    set_vec3(s->loc_ambient, color_to_vec3(t->light_ambient));
    set_float(s->loc_sun_wrap, t->sun_wrap);
    set_vec3(s->loc_camera, camera_pos);
    set_float(s->loc_time, time);
    set_float(s->loc_detail_scale, t->shader_detail_scale);
    set_float(s->loc_detail_strength, t->shader_detail_strength);
    set_float(s->loc_bump_strength, t->shader_bump_strength);
    set_float(s->loc_spec_strength, t->shader_spec_strength);
    set_float(s->loc_spec_power, t->shader_spec_power);
    set_float(s->loc_pulse_amount, t->shader_pulse_amount);
    set_float(s->loc_pulse_speed, t->shader_pulse_speed);
    set_float(s->loc_cavity_darkness, t->cavity_darkness);
}
