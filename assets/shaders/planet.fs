#version 330

// Shader de fragmento do planeta: calcula a cor de cada pixel.
//
// (Comentarios sem acentos de proposito: alguns drivers recusam caracteres
// fora do ASCII no codigo de um shader.)
//
// O que ele faz alem da versao sem shader:
//   - luz do Sol calculada por pixel, nao por vertice;
//   - detalhe de perto: um ruido 3D varia a cor e "enruga" a superficie, o
//     que da textura a uma malha que so tem uma cor por voxel;
//   - reflexo do Sol na agua e no gelo;
//   - materiais emissivos (manto, nucleo) pulsam.

in vec3 fragPosition;   // posicao no mundo
in vec3 fragNormal;
in vec4 fragColor;      // rgb = cor do material, a = emissao
in vec2 fragParams;     // x = brilho especular do material
                        // y = profundidade abaixo do nivel do mar (0 a 1)

uniform vec3 sunDir;          // do planeta para o Sol, comprimento 1
uniform vec3 sunColor;
uniform vec3 ambientColor;
uniform float sunWrap;
uniform vec3 cameraPos;
uniform float time;           // segundos de jogo

uniform float detailScale;    // tamanho do grao do detalhe (maior = grao menor)
uniform float detailStrength; // quanto o detalhe varia a cor
uniform float bumpStrength;   // quanto o detalhe "enruga" a superficie
uniform float specStrength;
uniform float specPower;
uniform float pulseAmount;
uniform float pulseSpeed;
uniform float cavityDarkness; // quanto o fundo das crateras escurece

out vec4 finalColor;

// Numero pseudoaleatorio de 0 a 1 a partir de um ponto (hash de Dave Hoskins).
float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

// Ruido de valor: sorteia um numero em cada ponto de uma grade e interpola
// suavemente entre os 8 vizinhos.
float valueNoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// Uma camada de detalhe, de -0.5 a 0.5. Quando o grao do ruido fica menor
// que um pixel (camera longe, ou superficie vista de raspao), a camada some
// aos poucos em vez de virar chuvisco.
float detailLayer(vec3 p, float scale)
{
    vec3 q = p * scale;
    float cellsPerPixel = max(length(dFdx(q)), length(dFdy(q)));
    float fade = 1.0 - smoothstep(0.30, 0.75, cellsPerPixel);
    if (fade <= 0.0) return 0.0;
    return (valueNoise(q) - 0.5) * fade;
}

// Inclina a normal de acordo com a variacao de uma "altura" h de um pixel
// para o vizinho (metodo de Mikkelsen). Da relevo sem precisar de mais
// triangulos nem de coordenadas de textura.
vec3 perturbNormal(vec3 n, vec3 p, float h, float strength)
{
    vec3 dpdx = dFdx(p);
    vec3 dpdy = dFdy(p);
    float dhdx = dFdx(h);
    float dhdy = dFdy(h);
    vec3 r1 = cross(dpdy, n);
    vec3 r2 = cross(n, dpdx);
    float det = dot(dpdx, r1);
    vec3 grad = sign(det) * (dhdx * r1 + dhdy * r2);
    return normalize(abs(det) * n - strength * grad);
}

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 albedo = fragColor.rgb;
    float emission = fragColor.a;
    float gloss = fragParams.x;

    // Detalhe em duas escalas: manchas largas e grao fino. Em materiais com
    // brilho (agua) o ruido desliza devagar com o tempo, o que faz a
    // superficie ondular.
    vec3 detailPos = fragPosition + gloss * time * vec3(0.05, 0.11, 0.07);
    float detail = detailLayer(detailPos, detailScale)
                 + 0.5 * detailLayer(detailPos, detailScale * 3.7);

    // Agua e gelo ficam mais lisos que terra e rocha: a cor varia menos e a
    // superficie quase nao enruga.
    albedo *= 1.0 + detailStrength * detail * 2.0 * mix(1.0, 0.25, gloss);
    normal = perturbNormal(normal, fragPosition, detail, bumpStrength * mix(1.0, 0.16, gloss));

    // Sombra do proprio planeta: do lado oposto ao Sol e noite, nao importa
    // para onde a superficie aponta. Sem isto, a encosta de uma cratera no
    // lado da noite que estivesse virada para o Sol apareceria iluminada.
    float dayside = smoothstep(-0.10, 0.12, dot(normalize(fragPosition), sunDir));

    // Luz difusa, com o mesmo "wrap" da versao sem shader.
    float facing = dot(normal, sunDir);
    float diffuse = clamp((facing + sunWrap) / (1.0 + sunWrap), 0.0, 1.0) * dayside;

    // O fundo das crateras recebe menos luz: quanto mais fundo, mais escuro.
    // E isso que faz uma cratera parecer um buraco, e nao uma bola.
    float cavity = 1.0 - cavityDarkness * fragParams.y;
    vec3 lit = albedo * (ambientColor + sunColor * diffuse) * cavity;

    // Reflexo do Sol: mais forte quando a superficie espelha o Sol na
    // direcao da camera. So aparece em materiais com brilho (agua, gelo).
    vec3 toCamera = normalize(cameraPos - fragPosition);
    vec3 halfway = normalize(toCamera + sunDir);
    float spec = pow(max(dot(normal, halfway), 0.0), specPower) * smoothstep(0.0, 0.15, facing) * dayside;
    lit += sunColor * spec * specStrength * gloss;

    // Emissao: manto e nucleo brilham sozinhos, sem depender do Sol.
    vec3 color = lit;
    if (emission > 0.01) {
        // Magma: dois ruidos que deslizam devagar em sentidos diferentes
        // formam manchas escuras (rocha esfriando) e veios claros (rocha
        // fundida) que mudam com o tempo.
        float flow = 0.65 * valueNoise(fragPosition * 0.55 + vec3(0.0, time * 0.10, 0.0))
                   + 0.35 * valueNoise(fragPosition * 1.70 - vec3(time * 0.17, 0.0, 0.0));
        float heat = smoothstep(0.25, 0.80, flow);

        // Ondas de brilho que saem do centro do planeta.
        float wave = sin(time * pulseSpeed - length(fragPosition) * 0.45);

        vec3 glow = fragColor.rgb * (0.50 + 0.85 * heat);
        glow = mix(glow, vec3(1.0, 0.95, 0.75), heat * heat * 0.35);   // os veios mais quentes clareiam
        glow *= 1.0 + pulseAmount * wave;
        color = mix(lit, glow, emission);
    }

    finalColor = vec4(color, 1.0);
}
