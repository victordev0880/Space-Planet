# Decisões técnicas

Registro curto das decisões que moldam o projeto: o que foi decidido e por quê.
As mais novas ficam no fim.

## Marco 1: Fundação (2026-11-11)

### Build com Makefile, não CMake
A máquina de desenvolvimento tem gcc (MSYS2 UCRT64), `mingw32-make` e git, mas
não tem CMake. Com Makefile o jogo compila com o que já existe, em um comando:
`mingw32-make`. Os comandos do Makefile rodam no `cmd.exe` no Windows e em
shell POSIX no Linux. Todos os caminhos são relativos, porque o GNU make não
lida bem com espaços e a pasta do projeto tem espaços no nome.

### raylib 5.5 fixo, baixado pelo Makefile
A regra é "raylib 5.x"; a 5.5 é a última da série (já existem 6.0 e 6.2). Na
primeira compilação o Makefile baixa só a pasta `src/` da tag `5.5` para
`vendor/raylib` (fora do git) e gera uma biblioteca estática. O executável é
ligado com `-static`, então roda em outra máquina sem DLLs do MSYS2.

### Lógica separada da renderização, garantida pelo linker
- Pastas de lógica: `src/core`, `src/game`, `src/voxel`, `src/tools`.
- Pastas que usam raylib: `src/render`, `src/ui`, `src/audio`, `src/platform`.

O executável de testes é ligado **sem** o raylib. Se um módulo de lógica chamar
uma função do raylib, `mingw32-make test` falha na ligação. A regra não depende
de disciplina.

A lógica usa `raymath.h` (vetores e matrizes), que é header-only e funciona
sozinho. Ele entra sempre por `core/vmath.h`. Arquivos que usam `raylib.h`
precisam incluí-lo antes, porque os dois definem os mesmos tipos.

### Loop de passo fixo com interpolação
A lógica avança em fatias de 1/60 s (`core/loop.c`), não importa a taxa de
quadros. O que sobra de tempo vira o `alpha`, e a tela desenha um ponto
intermediário entre o estado anterior e o atual. Se o jogo travar, no máximo
`sim.max_steps` passos rodam em um quadro e o resto é descartado, para não
entrar em uma espiral de atraso.

### Camada de input por ações
A lógica nunca consulta teclas: enxerga ações (`ACT_FORWARD`, `ACT_BOOST`...)
em um `InputState`. Quem preenche pode ser o teclado e o mouse
(`platform/platform_input.c`), o roteiro do autoplay ou, depois, um gamepad.
O `InputTracker` faz a ponte entre "uma amostra por quadro" e "um estado por
passo de lógica", sem perder toques rápidos nem aplicar o mesmo movimento de
mouse duas vezes.

### Autoplay com dois modos
- **Determinístico (padrão):** um passo de lógica por quadro, sem vsync. O
  mesmo roteiro dá sempre o mesmo `state_hash`, e os tempos mostram o custo
  real de cada quadro.
- **Tempo real (`--realtime`):** relógio de verdade e vsync, como o jogador
  joga. Serve para medir o ritmo percebido e exercitar a interpolação.

O relatório traz `frame_ms_p99` e `frames_over_budget` além do pior quadro. Um
único quadro ruim causado por outro programa estraga o "pior quadro" (visto na
prática: 48 ms em uma execução, 4 ms nas outras), mas quase não mexe no p99.

Os screenshots não usam `TakeScreenshot()`: no raylib 5.5 ele descarta a pasta
do nome (grava sempre no diretório atual) e lê um tamanho errado em telas com
escala de DPI. Usamos `rlReadScreenPixels` + `ExportImage`, que é o que ele faz
por dentro.

### Tuning estrito
`data/tuning.txt` e a tabela de `src/game/tuning.c` precisam bater: chave
faltando, chave sobrando, valor não numérico ou fora da faixa é erro. Ao
iniciar, o jogo se recusa a abrir e diz o motivo. No F5, os valores antigos são
mantidos e o erro aparece na tela. Assim um erro de digitação nunca vira um
comportamento estranho e silencioso.

### Campo de estrelas em uma malha só, sem shader
Seis mil estrelas são uma única malha estática, desenhada em uma chamada, sobre
uma esfera centrada na câmera. O tamanho máximo das estrelas é limitado
(`stars.size_max`): acima disso a projeção em perspectiva as estica em ovais
perto das bordas da tela.

### Escala do mundo
Uma unidade do mundo equivale a um voxel do planeta. Com a grade de 128³ e raio
de 45%, o planeta terá raio de cerca de 57,6 unidades; por isso `camera.near` é
0,1 e `camera.far` é 4000.

### Sanitizers
No Windows os testes compilam com UBSan em modo "trap"
(`-fsanitize=undefined -fsanitize-trap=undefined`), que não precisa de
biblioteca de runtime. ASan não existe no MinGW e o WSL não está instalado;
fica pendente. No Linux o Makefile já usa `-fsanitize=address,undefined` nos
testes.

## Marco 2: Planeta estático (2026-11-11)

### Surface nets, não marching cubes
A malha de cada chunk sai de `src/voxel/mesher.c` por surface nets: um vértice
por célula cortada pela superfície, um quadrado por aresta da grade que liga
um ponto sólido a um vazio. Motivos: cerca de metade dos triângulos do
marching cubes (o alvo é uma GPU integrada), nenhuma tabela de 256 casos para
errar, e vértices já compartilhados. A desvantagem é arredondar cantos vivos.

Emendas entre chunks: cada chunk gera também os vértices da camada de células
vizinha de que precisa. A posição de um vértice depende só das 8 densidades da
sua célula e é calculada a partir da coordenada global, então dois chunks
chegam ao mesmo número, bit a bit. Os testes conferem isso de forma indireta e
forte: a malha do planeta inteiro é fechada (toda aresta tem sua parceira),
obedece à fórmula de Euler para uma esfera, tem todos os triângulos voltados
para fora e encerra o volume esperado.

### Densidade: 8 bits, superfície em 127,5, variando 32 por voxel
Sólido é densidade >= 128. A densidade varia 32 unidades por voxel de
distância, ou seja, vai de 0 a 255 em uma faixa de 8 voxels em volta da
superfície. A primeira versão usava 64 por voxel (faixa de 4) e produzia
**anéis de sombra** concêntricos nos oceanos e nas calotas: a normal é
calculada comparando densidades a até uns 3 voxels do ponto, e quando alguma
já estava travada em 0 ou 255 a normal saía torta, com erro periódico.

Efeito colateral inofensivo: a soma das densidades passa do volume real da
malha por um fator (1 + w²/r²), com w = 4 voxels. No planeta do jogo isso dá
0,5%. O HP é uma razão entre volumes, então não é afetado.

### Camada de cima com a mesma espessura em terra e no mar
A profundidade do mar só escolhe o tom da água (rasa, média, funda). O material
"água" ocupa a mesma espessura que o bioma em terra (`planet.surface_depth`).
A primeira versão punha areia ou crosta logo abaixo de uma lâmina de água de
um ou dois voxels, e isso aparecia como contorno escuro nas costas e faixas de
cor no oceano: duas camadas empilhadas mais finas que a resolução da grade não
têm como ser representadas sem serrilhado.

### Cor do vértice = material amostrado um pouco abaixo da superfície
A cor é a mistura suave (trilinear) dos materiais dos 8 voxels em volta de um
ponto 0,6 voxel abaixo do vértice. Muda aos poucos ao longo da superfície e
não depende do alinhamento com a grade. Vale também para crateras: a cor vai
revelar crosta, manto e núcleo conforme a profundidade.

### Iluminação assada nos vértices, com o Sol fixo
O shader padrão do raylib não tem iluminação, e o jogo precisa funcionar com
`--no-shaders` de qualquer forma. A cor final de cada vértice (material x luz
do Sol naquele ponto) é calculada quando a malha do chunk é gerada, em
`src/render/planet_render.c`. Consequência: mover o Sol exigiria remalhar tudo,
então ele é fixo, e é a câmera que passa do dia para a noite.

### Buffers de chunk com folga, reescritos no lugar
Cada chunk tem buffers próprios na placa de vídeo, criados 50% maiores que a
malha. Remalhar um chunk reescreve os dados (`UpdateMeshBuffer`); só recria os
buffers se a malha nova não couber. Medido na Vega 8: nem reescrever nem
recriar deixa o desenho mais lento depois.

O desenho não usa `DrawMesh` por chunk: prepara shader, matrizes e textura uma
vez e só troca o buffer entre um chunk e outro. Com 240 chunks, repetir a
preparação custaria mais que desenhar.

### Atmosfera pelo caminho da linha de visão no ar
O brilho em cada ponto depende de quanto ar a linha de visão atravessa: pouco
no meio do disco, máximo raspando a borda do planeta, caindo a zero no topo da
atmosfera. A malha são anéis concêntricos em volta do eixo de visão,
recalculados na CPU a cada quadro (cerca de 1500 vértices). A primeira versão,
uma esfera com brilho de borda mais um halo, parecia uma casca de vidro com o
pico de brilho longe da superfície.

*Atualização (marco 4): com a câmera dentro da atmosfera a mesma conta passa
a valer para o céu inteiro em volta dela; ver "Atmosfera vista de dentro".*

### Luzes de cidades como uma malha de pontos
Cinco mil quadradinhos brilhantes deitados no solo, em uma única malha com
mistura aditiva, sorteados em aglomerados sobre terra habitável e apenas onde
é noite. Apagar as luzes onde a superfície for destruída fica para o marco 3.

### Limitações conhecidas do marco 2
- **De perto a superfície é borrada.** As cores são por vértice, um vértice
  por voxel. Da órbita fica bom; em voo rasante vai parecer liso demais.
  *Atualização (2026-11-12): o shader do planeta acrescentou detalhe por
  pixel e resolveu a maior parte; ver "Shader do planeta" abaixo. Sem shader
  a limitação continua.*
- **Gerar o planeta leva cerca de 0,5 s** e gerar todas as malhas, de 130 a
  220 ms (0,5 a 0,9 ms por chunk com superfície). Esse último número é o que
  vai limitar quantos chunks dá para remalhar por quadro no marco 3.

### Medições (Vega 8, 1280x720, antisserrilhado 4x)
Feitas com `roteiros/desempenho.txt` (600 quadros, metade com o planeta
ocupando a tela toda).
- Planeta: 240 dos 512 chunks têm malha; 127 mil triângulos.
- Sem vsync: mediana de 4,0 a 4,5 ms por quadro, p99 de 6 a 8 ms, pior quadro
  abaixo de 10 ms. Nenhum quadro acima de 16,7 ms.
- Com vsync: 60 FPS, mediana de 16,75 ms. De 0 a 3 quadros em 600 perdem o
  vsync, em quadros diferentes a cada execução. Como a simulação é idêntica
  entre execuções, isso é ruído da máquina, não do jogo; o relatório lista os
  quadros lentos (`slow_frames`) justamente para permitir essa conferência.

### Lição: média engana, mediana não
Durante os testes o overlay mostrou "41 FPS" depois de um F5 e pareceu que
reescrever os buffers tinha deixado o jogo lento. Não tinha: o overlay
mostrava a média dos últimos 120 quadros, e a janela incluía o único quadro de
quase 1 segundo do próprio F5. O overlay agora mostra a mediana, e o relatório
do autoplay traz `frame_ms_median` e `frame_ms_p99` além da média.

## Shader do planeta (2026-11-12)

Com shaders liberados, o planeta ganhou `assets/shaders/planet.vs` e
`planet.fs`. O que o shader faz além da versão sem shader:

- luz do Sol por pixel, e não por vértice;
- **detalhe de perto:** um ruído 3D em duas escalas varia a cor e inclina a
  normal de cada pixel, o que dá textura a uma malha que só tem uma cor por
  voxel. O ruído some aos poucos quando o grão fica menor que um pixel
  (câmera longe ou superfície vista de raspão), em vez de virar chuvisco;
- reflexo do Sol na água e no gelo, e água que ondula devagar;
- manto e núcleo com aspecto de magma em movimento e pulso de brilho.

A malha leva a cor do material (com a emissão no canal alfa), a normal e dois
parâmetros por vértice: brilho especular e profundidade abaixo do nível do
mar. No modo sem shader (`--no-shaders`, ou se o shader não compilar) a mesma
conta de luz é feita por vértice ao gerar a malha, sem detalhe, reflexo nem
pulso. A simulação não depende do modo: o `state_hash` é idêntico nos dois.

Custo medido na Vega 8: cerca de 1,2 ms a mais por quadro com o planeta
enchendo a tela.

O F5 relê também os arquivos de shader, o que permite ajustá-los com o jogo
aberto. Os comentários dos shaders não têm acentos de propósito: alguns
drivers recusam caracteres fora do ASCII no código de um shader.

### Sombra do planeta e fundo das crateras
Duas correções que valem nos dois modos e nasceram de capturas com defeito:

- **Sombra do próprio planeta.** A conta de luz olha só para onde a superfície
  aponta. A encosta de uma cratera no lado da noite, virada para o Sol,
  aparecia iluminada. Agora o lado oposto ao Sol é noite, não importa a
  inclinação do terreno.
- **Fundo das crateras mais escuro.** Vista de cima, uma cratera iluminada
  parecia uma bola cinza: sem sombras, côncavo e convexo ficam iguais. A luz
  agora diminui com a profundidade abaixo do nível do mar
  (`cavity.darkness`).

## Marco 3: Planeta destrutível (2026-11-12)

### Cratera = subtrair uma esfera do campo de densidade
`planet_carve` limita cada voxel perto do impacto à densidade que ele teria
se ali houvesse a superfície de uma esfera vazia. A densidade cai aos poucos
na borda, igual à superfície do planeta, então a cratera sai lisa e com
normais corretas. Só se retira matéria.

Uma arma define a cratera pelo **raio da boca e pela profundidade**;
`crater_sphere` converte isso na esfera certa (rasa e larga = esfera grande
com o centro acima do solo; funda e estreita = centro abaixo do solo).

O volume (HP) é atualizado na hora, só com a diferença. O contador nunca é
recalculado; os testes conferem que ele bate com a recontagem da grade depois
de dezenas de crateras.

**Consequência a lembrar no balanceamento:** a faixa suave em volta da esfera
também perde densidade. Por isso a densidade retirada é o volume da esfera
vezes (1 + w²/r²), com w = 4 voxels. Uma cratera de raio 5 custa 1,6 vez o
seu volume aparente em HP; uma de raio 2, cinco vezes. O dano das armas deve
ser ajustado olhando o HP medido, não a geometria.

### Remalhamento por versão, com orçamento
Cada chunk do planeta tem um contador de versão que a lógica incrementa
quando algo muda a malha dele (o próprio chunk ou um vizinho a até 2 voxels).
O renderizador guarda a versão que já virou malha. A cada quadro ele compara,
escolhe os chunks desatualizados mais próximos da câmera e remalha até bater
no orçamento (`remesh.max_chunks` ou `remesh.budget_ms`, o que vier primeiro).
O resto fica para os quadros seguintes.

A lógica não conhece o renderizador e não há fila compartilhada: só um número
por chunk. Enquanto um chunk espera, a malha antiga continua na tela; o
clarão e a poeira do impacto cobrem esse instante.

O mesher lê uma cópia local das densidades do chunk e de duas camadas em
volta, em vez de consultar a grade ponto a ponto, e calcula a normal a partir
dessa cópia. Medido: **cerca de 0,4 ms por chunk** em média.

### Impactos como eventos numerados
O mundo guarda os últimos 32 impactos em uma fila circular, cada um com um
número de ordem. O renderizador lembra até qual já reagiu e, a cada quadro,
trata os novos: soma tremor à câmera e apaga as luzes de cidades cujo solo
sumiu. A lógica não sabe que existe câmera ou luz.

### Partículas simuladas na lógica, em array fixo
Pedras, poeira, brasas e o clarão são só efeito visual, mas rodam nos passos
fixos do mundo para o autoplay produzir sempre as mesmas imagens. Ficam em um
array de 8000 posições reservado na arena; as vivas ficam juntas no começo e,
quando uma morre, a última viva ocupa o lugar dela. Se o array enche, as
novas não nascem.

A poeira tem teto de tamanho e de quantidade simultânea: nuvens translúcidas
grandes são caras para a placa de vídeo, porque cada uma repinta todos os
pixels que cobre. Antes do teto, um bombardeio dobrava o custo do quadro.

### Derrota
O planeta colapsa quando a massa restante cai abaixo de `defeat.planet_hp`
(35%) ou o núcleo cai abaixo de `defeat.core_hp` (60%). No marco 3 isso só
mostra o aviso; o fim de jogo de verdade vem no marco 5.

### Ferramentas de debug
Clique esquerdo abre uma cratera no ponto sob o cursor (um raio da câmera
percorre o campo de densidade até achar matéria); o botão direito troca entre
quatro tamanhos; R restaura o planeta. No autoplay, `crater` e `barrage`
fazem o mesmo de forma determinística.

### Medições (Vega 8, roteiro `cratera.txt`, 310 impactos)
- Simulação: menos de 1 ms por quadro em média.
- Quadro: mediana de 6 ms sem vsync; 60 FPS com vsync.
- Remalhamento: 0,4 ms por chunk; fila máxima de cerca de 400 chunks depois
  de 60 impactos no mesmo quadro, esvaziada em menos de um segundo.
- Os únicos quadros acima de 16,7 ms que se repetem em toda execução são os
  três em que o roteiro despeja 40 crateras gigantes de uma só vez (27 ms de
  simulação). É um caso artificial, mas indica o limite: ataques que escavam
  muito (o laser do Rompe-Mundos) precisam distribuir o dano por vários passos.

### Limitações conhecidas do marco 3
- As divisas entre materiais continuam suaves de perto (uma cor por voxel).
  Dá para recortá-las com ruído no shader, ou aumentar a grade.
- Sem sombras de verdade: só a sombra do planeta inteiro e o escurecimento
  por profundidade.
- A matéria arrancada some; fragmentos em órbita e mineração são do marco 9.

## Marco 4: Voo (2026-11-12)

### Orientação por quatérnion, voo em seis eixos
A nave não tem "para cima" fixo: em volta de um planeta ela precisa poder
apontar para qualquer lado sem travar nos polos, o que ângulos de guinada e
arfagem não permitem. A orientação é um quatérnion; o mouse gira o nariz em
relação à própria nave, e Q/E rolam. Perto do planeta ela se endireita sozinha
(o topo volta a apontar para longe do chão), para o jogador não ficar de
cabeça para baixo sem querer. As câmeras passaram a usar a mesma
representação.

### Aderência: virar a nave vira o voo
A velocidade tem inércia, mas a cada passo o rumo gira um pouco em direção ao
nariz, sem perder velocidade (`ship.grip`). É o que separa "arcade" de
"simulador": no espaço de verdade, virar a nave não muda para onde ela vai. O
drift (Ctrl) desliga a aderência: a nave segue reto enquanto o nariz gira,
para atirar para trás.

### Gravidade em uma velocidade separada, com sustentação
A gravidade não entra na velocidade de voo. Se entrasse, a aderência a
redirecionaria para a frente e uma nave parada sairia planando. Ela alimenta
uma "velocidade de queda" própria, com freio, cujo teto é baixo
(`ship.gravity / ship.fall_drag`, uns 4 por segundo). E some quando a nave voa
rápido ao longo da superfície (`ship.lift_speed`), como a sustentação de um
avião: quem voa não afunda, quem para afunda devagar.

### Voo rasante: a nave acompanha a curvatura
O planeta tem raio 57,6 e a nave voa a 46 por segundo. Em linha reta junto ao
chão, ela sai pela tangente em um segundo, e o chão some da tela. Então,
dentro da atmosfera, o giro que leva a vertical de onde a nave estava à
vertical de onde ela está agora é aplicado também à orientação e à
velocidade: voar nivelado contorna o planeta. Acima da atmosfera o efeito some
aos poucos (`ship.hug_fade`) e o voo volta a ser livre, para mirar em alvos
distantes sem o nariz baixar sozinho. Abaixo do nível do mar também some:
dentro de túneis o voo é livre.

No mesmo trecho a câmera inclina para o chão (`chase.ground_tilt`); sem isso o
horizonte ficava no pé da tela e não se via o relevo passar.

### Colisão por amostragem da densidade
A nave confere a densidade em sete pontos (centro, nariz, cauda, pontas das
asas, topo, fundo). Perto da superfície a densidade cresce em proporção à
profundidade, então o próprio valor diz quanto o ponto entrou; a nave é
empurrada para fora pela normal do campo. É o mesmo campo que as crateras
alteram: voar dentro de uma cratera ou de um túnel funciona sem nenhum código
a mais. Em alta velocidade o passo é subdividido para não atravessar paredes.

O dano de uma batida depende só da velocidade **contra** a superfície: raspar
no chão a toda custa pouco, bater de frente com boost destrói a nave.

### Câmera em terceira pessoa
Fica atrás e acima da nave e a segue com atraso; o atraso é o que dá a
sensação de aceleração, e tem teto para a nave não sumir ao longe no boost. O
campo de visão abre com o boost e com o estilingue. Ela olha para um ponto à
frente da nave, não para a nave, e recua para perto dela se houver terreno (ou
a Lua) no caminho. A mira é desenhada onde o nariz aponta, projetada na tela,
porque com o atraso esse ponto nem sempre é o centro.

### Nave montada em código
O modelo são cerca de cem triângulos gerados por funções (fuselagem em fatias,
cabine, asas com espessura, motores, derivas), só a metade direita, espelhada.
Cada face é iluminada na CPU e desenhada dos dois lados, usando a normal
virada para a câmera; isso dispensa cuidar da ordem dos vértices e resolve as
peças finas. A primeira versão, com 17 triângulos, parecia um avião de papel.

### Atmosfera vista de dentro
Com a câmera dentro do ar, os anéis da malha passam a cobrir o céu todo: do
ponto logo abaixo da câmera até o horizonte (névoa sobre o chão distante) e do
horizonte ao zênite (a faixa azul que se apaga para cima). Os anéis do céu
ficam a passos iguais de brilho, não de ângulo; com passos de ângulo aparecia
uma borda dura onde o brilho zerava. A névoa sobre o chão é desenhada logo
acima do relevo mais alto, para as montanhas não a furarem, e a atmosfera
passou a ser desenhada antes da nave. Abaixo do nível do mar ela some.

### Lua e estilingue
A Lua é uma esfera lisa para a colisão e percorre uma órbita circular
inclinada. Não gira sobre si mesma e o Sol não se mexe, então a luz de cada
ponto dela nunca muda: a iluminação (com crateras sorteadas e "mares") é
calculada uma vez, ao criar a malha, e guardada na cor dos vértices.

O estilingue é uma regra de jogo, não física: um ganho real de velocidade
seria apagado pelo limite de velocidade da nave. Passar perto da Lua acima de
metade da velocidade máxima arma um bônus (`moon.sling_boost`) que vale
enquanto a nave está perto e por mais alguns segundos. A Lua também puxa a
nave de leve, pela mesma velocidade de queda da gravidade do planeta.

### Ferramentas de roteiro novas
`view ship|orbit` troca entre a câmera de jogo e a de apresentação (os
roteiros antigos usam `view orbit` para fotografar só o planeta); `ship x y z
tx ty tz [vel]` põe a nave em um ponto, apontada para um alvo. Um `cam` no
mesmo quadro fotografa a nave de fora.

### Medições (Vega 8, roteiro `voo.txt`)
- Quadro: mediana de 4,5 a 5 ms sem vsync; em tempo real, 60 FPS sem nenhum
  quadro acima do limite.
- Simulação: 0,05 ms por quadro. A nave custa quase nada.
- Com a tela inteira coberta pelo planeta (nave parada no fundo de um túnel):
  mediana de 4,75 ms, pior quadro de 10,7 ms.
- Os dois ou três quadros acima de 16,7 ms que o relatório lista vêm logo
  depois de uma captura de tela com a tela cheia; acompanham a captura quando
  ela muda de quadro, então são efeito da própria captura.

### Limitações conhecidas do marco 4
- Teclado e mouse de verdade só foram exercitados por quem joga; o autoplay
  cobre a mesma lógica, mas não a sensação do mouse (`ship.mouse_sens`,
  `ship.turn_smooth`).
- A nave nasce de frente para o planeta: segurar W com boost sem desviar
  termina em batida. É de propósito (a primeira imagem é o planeta inteiro),
  mas pode pedir ajuste em `ship.spawn_pitch`.
- De muito perto, as luzes de cidades viram manchas grandes.
- Sem gamepad e sem som; ficam para o polimento.

## Marco 5: Combate e fatia vertical (2026-11-12)

Pedido do Victor ao aprovar o plano: inimigos de bom tamanho, com velocidades
e mecânicas comedidas, vindo em hordas (nada de 300 de uma vez), e um design
caprichado.

### Hordas pequenas e legíveis
A maior horda de `data/waves.txt` põe 2 Bombardeiros e 17 drones em cena,
chegando em grupos ao longo de 20 segundos. Todos os inimigos são mais
lentos que a nave (Enxame 26, Bombardeiro 11; a nave faz 46), então o
jogador sempre alcança e sempre foge. O teste do tuning cobra isso.

### Enxame: passadas rasantes, em fila
Cada bando escolhe um ponto do planeta. Cada drone voa até um ponto de
entrada antes desse alvo, desce apontando para ele, atira enquanto ele está
na frente do nariz (cada tiro abre uma cratera pequena), passa por cima e
sobe; o alvo do bando então anda alguns graus. Os pontos de entrada ficam
escalonados, então o bando ataca em fila e não em bolo. A primeira versão
mergulhava reto e quase não atirava: a janela entre "começar a atirar" e
"sair do mergulho" era curta demais. Dentro do bando os drones mantêm
distância uns dos outros ("boids"). Fora da passada, atiram na nave se ela
fica na frente deles.

### Bombardeiro: blindado na frente, frágil atrás, sempre avisando
Tiros na frente e dos lados tiram só 25% do dano; o motor, atrás, brilha em
magenta e leva 2,5 vezes. Dar a volta nele é a escolha que o jogo pede.
Antes de cada bomba, o alvo aparece no solo como um anel vermelho, com um
anel externo que se fecha durante os 1,8 s de aviso, e soa um alarme. A bomba
cai exatamente na marca e pode ser derrubada no ar (2 tiros). Destruir o
Bombardeiro enquanto ele arma cancela a bomba.

### Os tiros do jogador não cavam o planeta
Só os inimigos destroem o planeta. Se os tiros da nave cavassem, quem mais
estragaria o planeta seria o próprio jogador, errando os alvos.

### Ajuda de mira
Inimigos (e bombas) até 4 graus do nariz, mais o tamanho aparente deles,
puxam os tiros, já compensando o movimento do alvo. Sem isso, acertar um
drone de 3 unidades a 100 de distância com o mouse controlando uma nave
seria frustrante. O alvo travado ganha cantos na tela.

### Hit-stop
Destruir um Bombardeiro, ou levar um golpe forte, congela a partida por
alguns passos (70 e 40 ms). O tempo de jogo continua correndo, então o
tremor de câmera segue animado. O que o jogador faz nesse instante (mouse,
teclas) é guardado e aplicado assim que a partida volta.

### Eventos em vez de chamadas diretas
A lógica anota o que aconteceu (tiro, acerto, abate, bomba armando, horda
começando...) numa fila numerada, como já fazia com os impactos. Som,
tremor de câmera e textos do HUD leem a fila; a lógica não sabe que eles
existem. O HUD não guarda estado: a idade de cada evento diz quanto tempo um
aviso ainda fica na tela.

### Pool com handles
Inimigos e projéteis moram em listas de tamanho fixo (64 e 256 vagas). Uma
referência que precisa durar de um passo para outro é um handle (índice mais
"geração" da vaga), que deixa de valer quando o objeto morre, em vez de
apontar para o objeto que ocupou a vaga depois.

### Sem grade espacial, por enquanto (desvio do CLAUDE.md)
O CLAUDE.md pede uma grade espacial para colisões. Com hordas deste tamanho
(até ~20 inimigos e ~100 projéteis), conferir todos contra todos custa
microssegundos: a simulação inteira gasta 0,08 ms por quadro em média. A
grade seria complexidade sem ganho. As colisões ficam todas em
`game/combat.c`, então dá para pôr a grade lá se as hordas crescerem.

### Áudio sintetizado, já neste marco
Tiro sem som não tem impacto, então o áudio básico veio antes do marco 10.
Os 22 efeitos são gerados em código na abertura (ondas, envelopes, filtros),
sem arquivos e sem biblioteca nova. Volume cai com a distância e o lado vem
do pan; avisos (bomba armando, horda chegando) têm volume mínimo. O
autoplay é sempre mudo.

### Piloto automático de teste
Um roteiro de teclas não acerta inimigos que se movem. O comando
`autopilot on` passa a nave para um piloto simples, que mira no inimigo mais
urgente (bombas, depois Bombardeiros armando, depois o mais perto) usando a
mesma camada de input do jogador. Com ele, `roteiros/onda1.txt` joga a
partida inteira, e um teste confirma que uma horda pequena pode ser vencida.

### Dados
Inimigos em `data/enemies.txt` e hordas em `data/waves.txt`, ambos relidos
pelo F5 (as hordas novas valem a partir da próxima partida). O leitor do
tuning agora tem uma tabela por arquivo, e um erro diz em qual arquivo está.

### Medições (Vega 8)
- `onda1.txt` (as 4 hordas): mediana de 3,25 ms sem vsync; em tempo real,
  60 FPS com p99 de 18 ms. Simulação: 0,08 ms em média, 2 ms no pior quadro.
- Os poucos quadros lentos mudam de lugar entre execuções com o mesmo hash:
  ruído da máquina.
- O piloto automático vence as 4 hordas em cerca de 2 minutos e meio.

### O problema da escala do HP (decisão pendente com o Victor)
A maior horda, sem nenhuma defesa por 2 minutos, abre 886 crateras; o
planeta fica visivelmente castigado, mas perde só 4,6% da massa. Com o
colapso em 35% (o valor do CLAUDE.md), seria preciso mais de 20 minutos de
ataque livre para perder. Quase todo o volume do planeta está no interior,
e o que o jogador vê (a superfície) é uma parte pequena dele. Nesta fatia,
portanto, não dá para perder pelo planeta. Ver as opções no relatório do
marco 5.

### Resposta do Victor sobre o HP (2026-11-12)
"Destruir um pouco mais do HP total, mas sem facilitar o colapso subindo a
porcentagem." O limiar continua em 35%. O que mudou foi o dano: crateras
maiores (Enxame 3,4/2,6, Bombardeiro 13/10,5, Kamikaze 7/6) e quatro
inimigos novos que tiram massa de outros jeitos (túnel, feixe, explosão,
mais drones). Resultado medido no marco 6: uma partida do piloto automático
termina com cerca de 75% do planeta (antes, 91%), e o núcleo passa a ser a
ameaça real (o Escavador vai direto para ele).

## Marco 6: Ondas e os oito inimigos (2026-11-12)

Pedido junto: ré e pairar na nave, nave mais bonita, inimigos com variação,
hordas maiores começando em 10.

### Ré e pairar
- **Ré:** segurar S com a nave quase parada (abaixo de 2 u/s para a frente)
  passa a empurrar para trás, com força e velocidade máximas menores que as
  da frente (`ship.reverse_thrust`, `ship.reverse_max`). Freio e ré ficam na
  mesma tecla, como em carro: primeiro freia, depois dá ré.
- **Pairar:** sem nenhuma tecla de movimento, abaixo de `ship.hover_speed` os
  estabilizadores entram em proporção (`hover` vai de 0 a 1): anulam a
  gravidade e a puxada da Lua e freiam o resto do movimento. A regra do
  CLAUDE.md ("só afunda quem está parado") foi trocada: parado de propósito,
  a nave fica onde está; quem afunda é quem perde velocidade fazendo outra
  coisa (drift, por exemplo). O HUD mostra RÉ e PAIRANDO.

### Nave redesenhada e luz dos modelos
O modelo low-poly ganhou seções de fuselagem, cabine, carenagens, anel do
motor aceso, luzes de navegação (vermelha à esquerda, verde à direita, como
em avião) e retrofoguetes acesos na ré. A luz dos modelos ganhou três termos,
ainda calculados na CPU por face: reflexo do Sol (Blinn), luz de borda (rim)
e a luz que o planeta devolve de baixo. Isso vale para nave e inimigos, e
funciona igual com e sem `--no-shaders`.

### Uma IA por arquivo
`enemies.c` ficou só com a infraestrutura (pool, nascimento, passo,
orientação, dano). Cada comportamento mora em `ai_<tipo>.c` e devolve um
`Steer` (velocidade desejada + flags: desviar do chão, bater no planeta, ou
"já me movi sozinho"). O cabeçalho `enemy_ai.h` é interno: o resto do jogo
não o enxerga.

### O inimigo pede, o mundo cava
A IA não chama `planet_carve`: anota pedidos de escavação (`CarveRequest`),
que o mundo executa depois do passo dos inimigos. Assim os inimigos não
dependem do planeta mutável no meio do próprio passo, e a ordem é a mesma
sempre (determinismo). Essas escavações geram impactos "quietos": apagam
cidades e entram no HP, mas não tremem a câmera nem tocam o som de explosão,
porque a broca e o feixe mordem várias vezes por segundo.

### Os seis tipos novos
- **Kamikaze:** mergulha no planeta (cratera média) ou, se a nave passa a
  menos de `aggro_range`, trava nela com um bipe e a persegue; junto dela
  explode (dano decresce com a distância, mínimo de 35%). Não dá pontos ao
  explodir: só quem o derruba ganha.
- **Escavador:** desce sem desviar do chão, pousa e cava um túnel reto até o
  núcleo (`drill_speed`, cerca de 27 s). A boca do túnel ganha um anel
  laranja no solo, e o HUD mostra a profundidade em %. É o jeito de ameaçar o
  núcleo de verdade.
- **Sifão:** paira a `altitude` e morde o solo embaixo com um feixe a cada
  `drain_interval`, andando devagar (`wander`): deixa um sulco. Fica sempre de
  pé (cúpula para cima), girando devagar.
- **Atirador:** mantém `range` da nave, circulando. Carrega por
  `charge_time` com uma linha de mira fina que segue a nave; nos últimos
  `lock_time` a mira **trava** (linha grossa, aviso "DESVIE!") e o tiro sai
  no ponto travado. Quem se mexe depois da trava escapa. O cano aponta para a
  mira durante a carga.
- **Escudeiro:** acompanha o grupo aliado mais próximo e protege quem está a
  menos de `shield_radius`: tiros da nave param no escudo (faísca turquesa e
  um "tim"). Escudeiros não protegem uns aos outros, então ele é sempre
  vulnerável: é o alvo prioritário. Cabos de energia e bolhas mostram quem
  está protegido.
- **Porta-drones:** orbita alto, acende o hangar por `launch_warning` e
  lança `launch_count` drones do Enxame, até haver `max_swarm` drones em
  cena (16). Esse teto segura a regra das hordas legíveis: com 36, a horda 9
  virou uma nuvem de drones.

### Variações
Cada grupo sai em uma de três paletas do seu tipo (tinta das peças e cor das
partes acesas) e cada inimigo varia ±8% de tamanho. O ponto fraco do
Bombardeiro fica sempre magenta: é informação, não enfeite.

### Hordas e calmaria
Dez hordas em `data/waves.txt`, de 10 a 40 inimigos, chegando em grupos ao
longo de até 26 s. Cada tipo novo aparece primeiro com pouca companhia. Na
calmaria, Enter chama a próxima horda na hora (quem já está pronto não
precisa esperar).

### HUD
- Painel da horda com quantos de cada tipo, na cor do tipo, quebrando linha.
- Alertas abaixo da barra do planeta para o que fere o planeta ou mira na
  nave agora: Escavador (com profundidade), Sifão drenando, Atirador mirando
  ou travado.
- Etiquetas sobre os inimigos (BOMBA, LANÇANDO, ESCAVANDO 40%, DRENANDO,
  MIRANDO/TRAVOU, "!" no Kamikaze travado); fora da tela, a seta desses pisca.
- Escudeiro marcado com círculo, não losango. Barra de vida nos resistentes.

### Áudio
Nove sons novos, todos sintetizados: trava do Kamikaze, explosão dele,
broca, feixe, carga do Atirador (dura o mesmo que a carga: dá para "ouvir"
quando o tiro vai sair), o tiro dele, o "tim" no escudo, o hangar abrindo e o
Enter da calmaria.

### Roteiros
- `inimigos.txt`: os seis modelos de perto e cada tipo em ação, com a nave
  pairando. Usa o comando novo `clear` (tira inimigos e tiros de cena).
- `onda1.txt`: agora a partida inteira, 10 minutos.

### Piloto automático no túnel
Contra um Escavador cavando, o piloto vai até um ponto 22 unidades acima da
boca do túnel, para sem tocar em nada (segurar S para frear conta como mexer
e desligaria os estabilizadores: a nave cairia túnel abaixo) e atira reto
para baixo. Sem isso, a horda com Escavador nunca terminava. Há um teste.

### Medições (Vega 8, `onda1.txt`, 10 minutos)
- Sem vsync: mediana de 5,25 ms, p99 de 18 ms; simulação de 0,14 ms em
  média. Em tempo real: 60 FPS, p99 de 18,25 ms (igual ao marco 5).
- Partida do piloto: chega à horda 10, 225 abates, planeta em 79%, núcleo
  em 91%. Antes do aumento das crateras, 91% de planeta na horda 9.
- Picos isolados de ~300 ms aparecem em quadros diferentes a cada execução
  com o mesmo hash: ruído da máquina. O remalhamento respeita os 4 ms por
  quadro; um chunk sozinho às vezes passa disso (envio à placa).

### Limitações conhecidas do marco 6
- O planeta de 128³ ainda faz o colapso por massa ser difícil: o caminho
  real para a derrota é o núcleo. Se ficar fácil demais com jogadores reais,
  dá para subir `defeat.core_hp` sem mexer no limiar de massa.
- O Escudeiro protege qualquer aliado no raio, não só o grupo que segue.
- O hash do estado passou a incluir a orientação dos inimigos (o cano do
  Atirador decide de onde sai o tiro).

## Ajustes depois do marco 6 (2026-11-13)

Pedido do Victor jogando: movimentos dos inimigos bruscos e estranhos,
dificuldade para acertar, ré fraca, Enxame frenético demais (queria ele "um
pouco mais parado, e indo para cima do jogador"), modelos repetidos.

### Movimento mais calmo
- O que dava o tranco: a inclinação nas curvas chegava a 57° e trocava de
  lado na hora em que a curva mudava; e, quase parados, os inimigos giravam o
  modelo atrás de uma velocidade minúscula, que muda de direção à toa.
- Agora a inclinação tem teto de ~32° e é filtrada à parte (leva ~1/3 s para
  acompanhar a curva); o modelo gira mais devagar; e, abaixo de 3 u/s, o
  modelo olha para um alvo dado pela IA (`look`: a nave, o solo) em vez da
  direção do voo. A separação entre drones cresce suave com a proximidade.
- O Atirador olha sempre para a nave (e, carregando, o cano segue a mira).

### Enxame: esperar, atacar, encarar
O Enxame dava laços: depois de cada passada o próximo ponto de entrada ficava
atrás dele e ele dava meia-volta. Agora:
- chega ao seu lugar numa formação em "V", no ponto de espera do bando, e
  fica ali parado, balançando de leve, virado para a nave;
- sai para a passada um de cada vez; ao terminar, o alvo do bando anda para
  a frente e o novo ponto de espera fica adiante: sem meia-volta;
- com a nave a menos de `aggro_range`, até metade do bando (`engage_share`)
  sai para encará-la: fica a `engage_distance` dela, circulando devagar e
  atirando, e volta ao bando depois de `engage_time`.
- Um pouco mais lento (22) e com aceleração menor (26).

### Acertar ficou mais fácil
- Folga de acerto: o tiro acerta quem passar a até `weapon.hit_margin` (0,8)
  do inimigo, das bombas e do ponto fraco.
- Ajuda de mira de 4° para 6,5°.
- Marcador de antecipação no HUD: no inimigo mais perto do nariz (até 25°),
  um círculo mostra onde ele vai estar quando o tiro chegar, com uma linha
  até ele. Mira em cima do círculo, acerta.

### Ré
S agora empurra a nave para trás o tempo todo: indo para a frente, isso
freia junto com o freio; abaixo de 6 u/s vira ré. Força 42 (era 26),
velocidade 30 (era 18; a da frente é 46). A câmera não deixa a nave chegar
mais perto que 80% da distância normal (de ré, ela vinha na direção da câmera).

### Modelos: três desenhos por tipo
- Kit de formas no `lowpoly` (casco por seções, placa com espessura, tubo,
  caixa, espinho, anel, cópias em volta de um eixo) e 24 modelos em
  `render/enemy_models.c`, cada um com 150 a 450 triângulos. Cada tipo tem uma
  paleta base; a variação do grupo escolhe o desenho e a tinta.
- Os pontos que a lógica usa ficam iguais nos três desenhos (ponto fraco do
  Bombardeiro atrás no centro, cano do Atirador em z = -1,7, hangar embaixo).
- Detalhes pequenos (antenas, rebites, faixas) só aparecem de perto.
- O desenho dos modelos ficou mais rápido: a rotação vira uma matriz uma vez
  por modelo, e a direção da câmera é calculada uma vez por modelo, não por
  face. Com tudo isso, a CPU gasta ~0,5 ms a mais por quadro na partida inteira.
- Roteiro `modelos.txt` (vitrine dos 24 desenhos e o Enxame em ação) e o
  `spawn` do autoplay aceita a variação: `spawn swarm 6 x y z 2`.

### Resultado medido (`onda1.txt`)
O piloto automático agora vence as 10 hordas em 10 minutos, com o planeta em
91,6% (antes chegava à horda 10 com 79%): acerta mais e o Enxame passa parte
do tempo encarando a nave em vez de atacar o solo. Mediana de 5 ms, p99 de
11,75 ms.

## Marco 7: Evolução (2026-11-13)

### XP, essência e cartas
- Cada abate dá XP e a mesma quantidade de Essência do tipo derrotado
  (`data/evolutions.txt`: 1 por drone, 8 por Bombardeiro, 10 por Porta-drones).
  O nível n pede `xp.base x xp.growth^(n-1)`: uns 12 níveis numa partida.
- A cada nível, 3 cartas sorteadas sem repetição. Peso de cada evolução:
  `base_weight + essence_weight x essência da linha` (0 se já está no máximo).
  Quem mais derrota Enxame vê mais cartas do Enxame. Um teste confere a
  frequência contra a conta.
- As cartas abrem 0,45 s depois do abate (dá para ver a explosão) e congelam
  a partida. Escolha com 1, 2, 3 ou clique (o cursor é solto). Uma tecla que
  já estava apertada quando as cartas abriram não vale: é preciso soltar e
  apertar (sem isso, segurar uma tecla escolheria às cegas).

### Tuning efetivo
As evoluções que só mudam números (casco, escudo, cadência, alcance, ajuda de
mira) não estão espalhadas em `if`s: a cada passo o mundo monta
`w->eff = base + evoluções` (`evolution_tune`) e usa esse tuning em tudo, e o
HUD também (as barras crescem com o Casco pesado). Copiar o tuning por passo
custa microssegundos. As que têm comportamento próprio (perfurar, explodir,
refletir, aliados) leem os níveis direto de `w->evo.rank`.

### As 16 evoluções (2 por linha)
| Linha | Evolução | O que faz |
|---|---|---|
| Enxame | Drones de escolta | drones em órbita da nave atiram em quem chega perto |
| Enxame | Tiro em leque | pares de tiros mais fracos, abertos para os lados |
| Kamikaze | Dash explosivo | o dash explode em volta da nave |
| Kamikaze | Reação em cadeia | quem você derruba explode nos vizinhos |
| Bombardeiro | Casco pesado | mais casco, já consertado |
| Bombardeiro | Ogivas | cada acerto fere quem está em volta |
| Escavador | Tiro perfurante | o tiro atravessa inimigos |
| Escavador | Quebra-blindagem | ignora a blindagem do Bombardeiro, mais dano nos pesados |
| Sifão | Roubo de vida | abates consertam o casco |
| Sifão | Sifão de energia | acertos devolvem energia (mais boost e dash) |
| Atirador | Acerto crítico | chance de dano triplo, em dobro de longe |
| Atirador | Mira longa | tiro mais rápido, de mais alcance, ajuda de mira maior |
| Escudeiro | Escudo maior | mais escudo e recarga mais rápida |
| Escudeiro | Reflexão | com escudo, tiros inimigos podem voltar |
| Porta-drones | Esquadrilha | caças aliados que caçam inimigos longe |
| Porta-drones | Hangar rápido | um drone de escolta a mais e aliados mais rápidos |

Sinergias (duas linhas combinadas) ficam para o marco 11, como no roadmap.

### Aliados
Drones de escolta orbitam a nave e atiram em quem chega a `escort.range`;
caças voam atrás de inimigos até `fighters.range`, passam atirando e voltam.
Não levam dano (são parte da nave) e somem enquanto ela está destruída. Os
tiros deles são tiros da nave (pontos e abates contam).

### A nave muda
Cada evolução acrescenta uma peça à nave (lâminas nas asas, canhões extras,
espinhos no nariz, blindagem lateral, casulos de mísseis, broca, cano de
atirador, emissores de escudo, hangares, antena), com um detalhe aceso na cor
da linha. O brilho dos motores e o rastro puxam para a cor da linha dominante,
mais quanto mais evoluções. HUD: barra de nível com um quadradinho colorido
por nível de evolução; a tela de fim lista as evoluções escolhidas.

### Roteiro e medições
- `evolucao.txt`: a nave base, com algumas e com todas as peças, as cartas,
  a escolha e o combate com aliados. Comandos novos de roteiro: `xp n` e
  `evolve <nome> <nível>`.
- Partida inteira do piloto automático (fica com a primeira carta): nível 12,
  11 níveis de evolução, vitória com o planeta em 93,5%. Mediana de 4 ms,
  p99 de 11,5 ms.

### Limitações conhecidas do marco 7
- As hordas não crescem com o nível da nave: com evoluções, a partida fica
  mais fácil no fim. O balanceamento é o marco 11.
- O piloto automático escolhe sempre a primeira carta; não reflete escolhas
  de um jogador.

## Ajustes antes do marco 8 (2026-11-13)

- **Inimigos mais espertos:** os leves (Enxame, Kamikaze, Atirador, Escudeiro)
  percebem quando a nave atira com o nariz neles e às vezes desviam para o
  lado (`enemies.dodge_*`). A primeira versão (55% de chance, a cada 2 s)
  tornava o Enxame quase impossível de acertar; ficou em 35%, a cada 4 s, um
  desvio curto. O Kamikaze vem em zigue-zague; o Escudeiro, com a nave
  perto, se põe entre ela e o grupo.
- **Mais dano no planeta (~1,5x por inimigo):** o Enxame atira mais, abre
  crateras maiores, espera menos e concentra as passadas (o alvo anda 16° em
  vez de 28°: as crateras se sobrepõem e cavam fundo); bombas a cada 6 s,
  Kamikaze com cratera maior, Sifão morde mais rápido, Escavador cava mais
  rápido. Na partida do piloto automático (que agora evolui e mata rápido) o
  efeito quase não aparece; num jogador comum, deve aparecer.
- **Habitantes:** o HP do planeta aparece como habitantes (volume x 8
  bilhões), o colapso como "extinção". Só o nome mudou.
- **Câmera:** distância 7,5 → 10, altura 2,2 → 2,9 e campo de visão 60° → 70°.
- **Piloto automático:** com o terreno entre ele e o alvo, ou rente ao chão,
  agora acelera para contornar. Antes, parado, ele pairava no fundo de uma
  cratera atirando na parede (apareceu quando a esquiva mudou a sequência de
  sorteios de um teste).

## Marco 8: Rompe-Mundos, o primeiro boss (2026-11-13)

### Um boss é uma entidade à parte, não um inimigo grande
O boss tem estrutura própria (`game/boss.c`, `Boss` dentro do `World`), não
uma entrada no pool de inimigos: tem várias peças com regras diferentes
(capacitores, núcleo, casco), uma máquina de ações e fases, e só existe um por
vez. Como os inimigos, não mexe no planeta nem cria entidades: deixa pedidos
(mordidas do laser, drones a soltar, explosões da morte) que o mundo executa.
Entra numa horda como um grupo de `data/waves.txt` (`breaker 1 ...`); a horda
não acaba enquanto ele estiver em cena. Números em `data/bosses.txt`.

### O ciclo do Rompe-Mundos
- **Descanso:** orbita e atira na nave com duas torretas (rajadas).
- **Carga:** desacelera; o traço do corte aparece no chão (um arco à frente
  dele, com a largura da vala que vai abrir) e quatro capacitores acendem nos
  flancos. Só nessa hora eles levam dano. Destruídos os quatro, o tiro falha.
- **Disparo:** o laser varre o traço em 3 s e abre uma vala (mordidas esféricas
  a cada 3 de arco). A nave que entra no feixe leva dano.
- **Atordoado:** com o tiro cancelado, as pétalas do dorso abrem e o núcleo leva
  o dano inteiro por 7 s. Fora disso, o núcleo fechado só deixa passar 8%.
- **Fases** pela vida (60% e 25%): cargas mais curtas, cortes mais longos (60°,
  75°, 90°), drones a partir da fase 2 e, na fase 3, o núcleo meio aberto (40%).
- **Morte:** 3,6 s de explosões pelo casco, que escurece e pega fogo; depois a
  explosão grande, pontos, XP e o núcleo como recompensa.

### O corte mede o chão uma vez, no começo da carga
A primeira versão media o solo a cada mordida. Como as mordidas se sobrepõem,
cada uma encontrava o chão já cavado pela anterior e descia mais: o corte virava
uma escada até o centro do planeta (o núcleo caiu para 64% numa luta). Agora o
relevo ao longo do traço é medido 65 vezes no começo da carga
(`Boss.cut_ground`), e toda mordida fica na mesma profundidade em relação a ele.
O desenho do traço usa a mesma medida (e não faz mais raycasts por quadro). Há
um teste para isso.

### A órbita gira antes de cada carga
Com a órbita num eixo fixo, todos os cortes caíam no mesmo grande círculo e a
vala só aprofundava. Antes de cada carga o eixo gira até 50° (sorteado,
`breaker.cut_turn`): cada corte é uma cicatriz nova, em outro lugar do planeta.

### Tamanho do corte
A vala tem uns 13 de largura e 20 de fundo (mordida de raio 11, centro 9 abaixo
do solo): passa da crosta (16 de espessura) e deixa o manto em brasa no fundo.
Cada corte tira de 2,5% a 4% do planeta. Menos que isso parecia um arranhão;
mais fundo ameaçaria o núcleo, que é a ameaça do Escavador.

### A recompensa: o núcleo Rompe-Mundos
Segurar o botão direito por 0,5 s solta um raio reto que atravessa tudo na
linha (inimigos, bombas, peças de boss) até 240 de distância, com recarga de
16 s. O HUD mostra o raio acima do painel da nave. O botão direito estava livre
(o lock-on da arma secundária ainda não existe); quando ela vier, as duas
precisam dividir o botão ou o raio muda de tecla.

### HUD e som
- Barra do boss no alto, logo abaixo da do planeta: nome, fase, marcas das
  fases, quadradinhos dos capacitores na carga; fica rosa com o núcleo aberto.
  Os avisos descem para baixo dela. (Embaixo, colidia com o painel da nave.)
- Na tela: losangos nos capacitores carregados, círculo "NÚCLEO" no núcleo
  aberto e seta na borda quando o boss está fora de vista.
- Avisos "CORTE EM n s: DESTRUA OS CAPACITORES (k/4)", "O LASER ESTÁ CORTANDO O
  PLANETA!" e "NÚCLEO EXPOSTO". Piscam clareando, nunca escurecendo: escuros,
  sumiam no espaço.
- Sons sintetizados para chegada (ronco e sirene), carga, laser, capacitor,
  atordoamento, fase, morte, recompensa e raio.

### Piloto automático e roteiros
- O casco tapa as peças de quase todos os lados. O piloto automático vai até um
  ponto de tiro de frente para a peça (acima do boss para o núcleo, ao lado
  para cada capacitor), contornando o boss se estiver do lado errado, e só
  atira de lá. Antes disso ele pairava embaixo do boss acertando o casco.
- Comandos novos: `boss breaker x y z`, `bosshp f` (pula para uma fase) e
  `cam boss acima atrás [lado]` (câmera que segue o boss; na carga e no disparo
  olha para o meio do corte). O relatório traz o boss em cena, os quadros em
  que cada boss caiu e se a nave tem o raio.
- `boss1.txt`: chegada, carga, corte, luta, fases 2 e 3, morte, recompensa e o
  raio atravessando um bando.

### Medições (Vega 8)
- `boss1.txt` (6900 quadros): mediana 3,75 ms, p99 8,75 ms; os cortes e os
  drones tiram 6,7% do planeta, núcleo intacto. Mesmo `state_hash` sem shaders.
- `onda1.txt` (partida inteira, boss na horda 5): vitória, planeta em 88%,
  núcleo intacto, boss derrotado aos 3,5 min; mediana 3,5 ms, p99 7,75 ms. Os
  quadros lentos mudam de lugar entre execuções (ruído da máquina).

### Limitações conhecidas do marco 8
- A vala, vista de lado, mostra mais a parede escura da crosta do que o manto;
  a lava aparece bem de cima. Um brilho de "rocha quente" nas bordas recém
  cortadas ficaria melhor (polimento, marco 10).
- A horda 10 ainda não é um boss (o próximo, Colmeia Lunar, é do marco 11).
- O piloto automático leva bem mais tempo que um jogador para matar o boss
  (perde capacitores no caminho); o roteiro usa `bosshp` para mostrar o fim.

## Marco 9, parte 1: rochas, raio trator e conserto do planeta (2026-11-13)

Pedido do Victor: "meteoros pra eu abduzir e levar pra consertar o planeta".
Virou o centro do ciclo de recursos do marco 9: destruição → fragmentos →
raio trator → conserto. A mineração com moeda e as defesas compráveis ficam
para a parte 2.

### Consertar = devolver o planeta original
O planeta guarda uma cópia da grade logo depois de gerada (`original_density`
e `original_material`, +4 MB no tamanho real). Consertar (`voxel/terraform.c`)
é o contrário da cratera: dentro de uma esfera, cada voxel volta à densidade e
ao material originais, com a borda misturada. Nunca passa do original: o
planeta não cresce, e consertar onde nada foi destruído não muda nada. O
relevo, a grama, a areia, o gelo e as luzes das cidades voltam como eram.
Gerar de novo a partir do ruído só onde se conserta seria caro demais (o
ruído é a parte cara da geração); a cópia custa só memória.

### As rochas
- **Fragmentos:** crateras com boca a partir de 5 (bomba, Kamikaze, o laser do
  boss) arrancam um fragmento a cada 1.100 voxels arrancados (somando
  impactos, no máximo 2 por impacto e 14 em órbita). O fragmento sobe em arco
  até uma órbita sorteada (18 a 34 de altura): é o cinturão de destroços. Tem
  o "topo" da cor de onde saiu (grama, areia, água, gelo).
- **Meteoros:** dois em cada calmaria e um a cada 55 s no meio das hordas,
  passando em linha reta do lado da nave, com cauda de fogo, e indo embora se
  ninguém pegar. São maiores e consertam mais.
- A nave bate nas rochas soltas (é empurrada e, rápida, leva dano).

### Raio trator (F)
F puxa a rocha que está à frente (até 28° do nariz, 45 de alcance); F de novo
solta. A rocha fica **pendurada à frente e abaixo da nave**, presa por uma mola,
com um cone de luz (uma abdução). A primeira versão a punha atrás da nave, no
lugar exato da câmera: ela nunca aparecia na tela. À frente e abaixo, ela fica
sobre o chão e dá para mirar a cratera antes de soltar. Solta, cai quase reto;
arrastada até encostar no chão, também conserta. Rebocar pesa: a nave perde até
30% da velocidade (pela rocha maior). Esticado demais, o raio arrebenta.

### O conserto acontece à vista
A esfera do conserto cresce em 8 passos ao longo de 1,2 s, a partir de um ponto
um pouco abaixo de onde era a superfície: o buraco se enche diante do jogador.
Ao terminar: "+N milhões de habitantes" sobe do lugar, pontos (2.500 por 1%)
e XP, e as luzes das cidades dali reacendem.

### Cicatrizes
Cada impacto marca uma região (impactos próximos se juntam). Uma por passo é
conferida: se o chão voltou, ela some. Rebocando, o HUD mostra as maiores como
círculos verdes, com "CRATERA" na melhor (grande e perto) e seta na borda se
ela estiver fora da tela. O piloto automático as usa como destino.

### O corte do boss passou a medir a superfície original
Com as calmarias consertando o planeta, a partida inteira revelou um problema
do marco 8: cada corte novo do Rompe-Mundos começa perto de onde o anterior
terminou (o boss anda na órbita enquanto descansa), e o "chão" medido ali já
era o fundo da vala antiga. Cada cruzamento cavava mais 20 e, numa luta longa,
o núcleo caía para 40% (uma partida terminou em derrota na horda 5). Agora o
corte é fundo em relação à superfície original: tira sempre uma fatia da mesma
espessura e não cava mais nada onde já cortou. Há um teste com seis cortes
seguidos.

### HUD, som e piloto automático
- Marcadores: meteoros ("METEORO", com seta na borda), fragmentos por perto, e
  colchetes verdes com "F puxa" na rocha ao alcance do raio. Avisos "METEORO À
  VISTA" e "REBOCANDO: solte (F) em cima de uma cratera". Na tela de fim, as
  rochas levadas e os habitantes devolvidos.
- Sons: meteoro chegando, fragmento arrancado, raio pegando, soltando, errando
  e arrebentando, rocha batendo no chão e um acorde de "cura".
- Piloto automático: sem inimigo para caçar (e com alguma cicatriz aberta),
  busca a rocha mais perto (meteoro primeiro) e leva até a maior cicatriz
  perto, soltando com a rocha em cima dela. Ele aperta F só em passos pares: F
  segurado de um passo para o outro não conta como aperto novo (na primeira
  versão ele pegava a rocha e não conseguia mais soltar, e voava para o
  espaço com ela).

### Calmarias mais longas
As calmarias das hordas 2 a 10 subiram de 8–14 s para 14–18 s: é nelas que dá
tempo de buscar um meteoro e consertar. Enter continua chamando a próxima horda.

### Medições (Vega 8)
- `conserto.txt` (dois atos, 7 rochas): o planeta volta a 99,97%, nenhuma
  cicatriz aberta; mediana 3,25 ms, p99 5,75 ms. Mesmo `state_hash` sem shaders.
- `onda1.txt`: vitória com o planeta em 88,7% e o núcleo intacto; 33 rochas
  devolveram 7,8% do planeta; boss derrotado aos 5 min; mediana 3,25 ms, p99
  7,75 ms.

### Limitações conhecidas
- Os tiros atravessam as rochas (a mineração da parte 2 deve mudar isso).
- A rocha a reboque pode tapar a mira se a nave apontar direto para o
  planeta (ela fica entre os dois).
- O piloto automático só conserta quando não há inimigo; um jogador pode
  escolher defender ou consertar.

## Marco 9, parte 2: mineração e defesas planetárias (2026-11-13)

### Cada rocha é uma escolha
Os tiros da nave quebram rochas (fragmento: 6 tiros, 4 cristais; meteoro: 16
tiros, 12 cristais). Quebrada, a rocha solta cristais de minério que flutuam,
são puxados pela nave a até 24 de distância e somem em 25 s. Assim cada rocha
é uma decisão: consertar o planeta (raio trator) ou virar minério para as
defesas (tiros). A rocha a reboque não leva tiro (fica bem na frente da arma).

### Só o jogador minera
Os tiros dos aliados e das torres passam pelas rochas (`Shot.source` diz de
quem é o tiro). Sem isso, um drone de escolta destruiria o meteoro que o
jogador ia rebocar.

### As defesas (teclas 4, 5 e 6)
Compradas com minério a qualquer hora e instaladas onde a nave está:
- **Torre orbital (25):** parada a 26 de altura, atira nos inimigos a até 95
  (não nos protegidos por um Escudeiro). Nasce 22 à frente da nave: na
  primeira versão nascia na altura da nave e, na hora da compra, tapava a
  câmera.
- **Escudo local (15):** cúpula de raio 22 sobre o solo sob a nave. Segura
  tudo o que cavaria o planeta ali dentro (tiros do Enxame, bombas, Kamikazes,
  a broca do Escavador, o feixe do Sifão, o laser do boss), cada golpe gastando
  o raio da cratera da vida dele (70), e se desfaz em 75 s. Pisca nos últimos
  segundos. É a resposta a um Escavador ou Sifão que o jogador não consegue
  alcançar a tempo.
- **Satélite (20):** orbita a 22 de altura e derruba a bomba (antes de tudo)
  ou o tiro inimigo mais perto, 1,6 por segundo.
Limites: 3 torres, 2 escudos, 3 satélites. Números em `data/tuning.txt`.

### O escudo intercepta no mundo, não nos inimigos
Todo dano ao planeta já passava por `world_impact` e `world_carve`; o escudo
é conferido ali. Nenhum inimigo precisou mudar, e qualquer coisa nova que
cavar o planeta já nasce coberta pelo escudo.

### HUD
Painel no canto de baixo à direita: o minério (que clareia ao coletar) e as
três defesas com tecla, preço e quantas há, apagadas quando não dá para
comprar. A confirmação ("TORRE ORBITAL INSTALADA") ou a recusa ("MINÉRIO
INSUFICIENTE", "LIMITE DESSA DEFESA") aparece no lugar do título por um
instante. Sons de tiro na rocha, cristal coletado, compra, recusa,
interceptação e do escudo segurando e quebrando.

### Piloto automático e roteiros
Sem inimigo à vista, ele compra o que der (satélite, torre, escudo) e, sem
cratera para consertar, atira nas rochas para minerar. Comando novo `ore n`;
o relatório traz o minério e as defesas. Roteiro `defesas.txt`: minerar um
meteoro, comprar as três defesas, a torre atirando, o escudo segurando
crateras e uma luta com as defesas ajudando.

### Medições (Vega 8)
A máquina estava mais lenta nesta sessão: o mesmo roteiro de referência
(`conserto.txt`) foi de 3,25 ms para 5,75 ms de mediana sem mudança no jogo.
Com isso:
- `defesas.txt`: mediana 6,25 ms, p99 13 ms; o planeta termina intacto.
  Mesmo `state_hash` sem shaders.
- `onda1.txt`: vitória com o planeta em 92%, núcleo intacto; 95 de minério,
  41 rochas levadas; mediana 6,75 ms, p99 14,75 ms. Os quadros lentos mudam de
  lugar entre execuções com o mesmo hash (ruído). O conserto remalha mais
  (7.656 chunks na partida, contra 3.197 antes): a esfera cresce em 8 passos e
  cada passo refaz os chunks dela. Dá 0,2 chunk por quadro, dentro do orçamento.

### Limitações conhecidas do marco 9
- O balanceamento (preços, quantas rochas aparecem, quanto o escudo aguenta)
  é um primeiro chute; o marco 11 é de balanceamento.
- As defesas não levam dano dos inimigos (a torre e o satélite são eternos).
  Se ficarem fortes demais, um caminho é deixá-las ser alvo.
- O piloto automático compra numa ordem fixa; não reflete escolhas de um
  jogador.

## Marco 10: polimento (2026-11-13)

Hit-stop, tremor de câmera, tela de fim, recorde e efeitos sonoros vieram dos
marcos 5 a 9. O marco 10 completou o resto: fonte, telas, opções, música,
controle e radar.

### Fonte
A fonte padrão do raylib é pixelada e passa cara de protótipo. Entrou a
Rajdhani (Google Fonts, licença OFL; o texto da licença vai junto em
`assets/fonts`), carregada em cinco tamanhos (18, 30 e 46 na SemiBold; 60 e
120 na Bold). Cada texto usa o menor tamanho que não precise ser ampliado.
`ui_draw_text` e `ui_measure_text` têm a assinatura de `DrawText` e
`MeasureText` e o mesmo sentido para o tamanho (as maiúsculas ocupam o mesmo
espaço), então o HUD trocou de fonte sem ser redesenhado. Abaixo do tamanho
12 a fonte suave fica ilegível, e esses textos sobem para 12. Sem os arquivos,
volta a fonte padrão. O overlay de debug continua pixelado.

### Telas
- O `Esc` fechava o jogo (padrão do raylib). Agora `SetExitKey(KEY_NULL)` e o
  `Esc` abre a pausa.
- O menu (`ui/menus.c`) não conhece o aplicativo: recebe o input já traduzido
  (setas, confirmar, voltar, mouse; teclado, mouse e controle viram a mesma
  coisa) e devolve um comando que o `main` executa.
- Título: a câmera de apresentação vira 20° de lado (só no desenho,
  `Renderer.yaw_offset`) e o planeta fica à direita do nome, que tem uma
  rachadura de lava.
- Pausa: a simulação não roda, nem o relógio dela. Ao sair, `input_resync`
  recomeça o input a partir do que está apertado sem gerar "apertou". O Enter
  que fecha a pausa também é "chamar a horda", e o clique viraria tiro.
- Transições em fade: recomeçar a partida (planeta gerado e remalhado
  inteiro) trava alguns décimos de segundo, e no escuro isso não aparece.
- Opções em `save/settings.txt` (chave = valor). Não são simulação: volume,
  sensibilidade e tremor nunca mudam o resultado, e o autoplay ignora o
  arquivo. A tela cheia é sem bordas (`ToggleBorderlessWindowed`), também no F11.

### Música
É sintetizada em código, como os efeitos: uma peça em ré menor, a 112 bpm, na
progressão i-VI-III-VII. As camadas:
- pad: três vozes de serras desafinadas que deslizam de um acorde para o outro;
- arpejo: sininho com eco pingue-pongue;
- baixo e bateria;
- metais, no boss;
- tique-taque de tensão.

O clima escolhe quais camadas soam. Elas entram e saem em 1,6 s, sempre no
compasso, então a música se transforma em vez de trocar de faixa. Quando o
planeta ou o núcleo chega a `audio.danger` da derrota, o fim da progressão
muda para sol menor e lá maior e entra o tique-taque. Na pausa, um filtro
abafa tudo.

A música roda na thread de áudio do raylib (`SetAudioStreamCallback`), então
um quadro travado (o recomeço da partida) não a faz engasgar. Só inteiros
atômicos atravessam as threads. Custo: 90 s de música em menos de 2 s de
processador. `--music-wav arquivo` grava todos os climas num .wav e mostra o
pico e o volume médio de cada trecho, assim os níveis se conferem sem ouvir.
No autoplay, que é mudo, a música nem começa.

### Controle
- O analógico esquerdo vira W/A/S/D (limiar `gamepad.move`). O direito vira
  "pixels de mouse" por segundo, com zona morta circular e curva de resposta
  (`input_stick`, testada). Os gatilhos atiram. O L3 liga o boost, que fica
  ligado até o jogador soltar o "frente", porque segurar o analógico apertado
  cansa.
- O direcional serve às compras e às cartas ao mesmo tempo: com as cartas na
  tela a partida congela e as compras não valem; fora delas, as cartas não
  valem.
- As dicas na tela (HUD, cartas, linha de controles, menus) trocam para os
  botões quando o jogador mexe no controle, e voltam ao teclado com qualquer
  tecla ou mouse. Na loja e nas cartas aparecem setas do direcional.
- **A vibração ficou de fora.** O raylib 5.5 no Windows (GLFW) não implementa
  `SetGamepadVibration`. Daria para chamar a XInput do Windows direto, mas é
  dependência nova, e a decisão fica com o Victor.
- Não há controle nesta máquina. O mapeamento foi conferido só pelo código e
  pelas capturas com `pad on` (as dicas).

### Radar
O radar mostra o planeta inteiro num disco no pé da tela, em projeção
azimutal equidistante centrada na nave: o centro é o chão embaixo dela, a
borda é o lado oposto do planeta, e a frente da nave aponta para cima. Uma
horda atacando do outro lado aparece perto da borda. Era a informação que
faltava para "defender ou caçar".

Aparecem no disco:
- inimigos na cor do tipo (os que ferem o planeta, com anel pulsante);
- boss, bombas, meteoros, cicatrizes e defesas;
- um anel a cada impacto no planeta.

A borda fica vermelha com o planeta perto do colapso. As setas de borda da
tela e a linha de controles subiram para não cobrir o radar.

### Autoplay
Comandos `menu title|pause|options|controls|game` e `pad on|off`; roteiros
`menus.txt` e `radar.txt`.

### Medições (Vega 8)
- `onda1.txt`: vitória com o planeta em 92%, mediana de 5,0 ms, p99 de
  12,5 ms. O resultado da partida é o mesmo de antes do marco: a simulação não
  mudou, e `evolucao.txt` também dá o mesmo hash de antes.

### Limitações conhecidas do marco 10
- Sem vibração (acima).
- Sem controle físico para testar: o primeiro teste de verdade é do Victor.
- A música é uma peça só, e as variações são de arranjo. Se cansar, dá para
  escrever uma segunda progressão para a segunda metade das hordas.

## Respostas do Victor (2026-11-11)

- **Shaders GLSL são permitidos** na competição. Continuam isolados em
  `assets/shaders/`, com a versão sem shader como alternativa (`--no-shaders`).
- **Não há prazo fixo.** O ritmo fica a critério do Claude; polimento antes de
  quantidade.
