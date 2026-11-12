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

Limitação: com a câmera dentro da atmosfera nada é desenhado. Fica para o
marco 4, quando a nave puder voar baixo.

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

## Respostas do Victor (2026-11-11)

- **Shaders GLSL são permitidos** na competição. Continuam isolados em
  `assets/shaders/`, com a versão sem shader como alternativa (`--no-shaders`).
- **Não há prazo fixo.** O ritmo fica a critério do Claude; polimento antes de
  quantidade.
