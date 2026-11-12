#version 330

// Shader de vertice do planeta.
//
// (Comentarios sem acentos de proposito: alguns drivers recusam caracteres
// fora do ASCII no codigo de um shader.)
//
// Os vertices do planeta ja estao em coordenadas do mundo, entao aqui so
// se aplica a camera (mvp) e se repassa o resto para o shader de fragmento.

in vec3 vertexPosition;
in vec2 vertexTexCoord;   // x = brilho especular do material (0 a 1)
in vec3 vertexNormal;
in vec4 vertexColor;      // rgb = cor do material, a = emissao (0 a 1)

uniform mat4 mvp;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;
out vec2 fragParams;

void main()
{
    fragPosition = vertexPosition;
    fragNormal = vertexNormal;
    fragColor = vertexColor;
    fragParams = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
