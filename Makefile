# ============================================================================
#  TERRA FRATURADA - build
#
#    mingw32-make             compila o jogo (terra.exe na raiz do projeto)
#    mingw32-make run         compila e executa   (ARGS="..." repassa argumentos)
#    mingw32-make test        compila e roda os testes da lógica (sem janela)
#    mingw32-make clean       apaga build/ e o executável
#    mingw32-make distclean   apaga também o raylib baixado
#
#  Na primeira vez o raylib (tag fixa) é baixado para vendor/raylib e compilado.
#  Todos os caminhos são relativos de propósito: o GNU make não lida bem com
#  espaços em caminhos absolutos, e a pasta do projeto pode ter espaços.
# ============================================================================

RAYLIB_TAG := 5.5
RAYLIB_DIR := vendor/raylib
RAYLIB_SRC := $(RAYLIB_DIR)/src
RAYLIB_LIB := $(RAYLIB_SRC)/libraylib.a

CC   := gcc
CSTD := -std=c11
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
OPT  ?= -O2 -g

# -isystem: os headers do raylib entram como "de sistema", então os warnings
# deles não poluem (nem quebram, por causa do -Werror) o nosso build.
INCLUDES := -Isrc -isystem $(RAYLIB_SRC)
# -ffp-contract=off: o compilador não funde a*b+c em FMA (um arredondamento
# a menos), o que daria resultados diferentes conforme a máquina.
CFLAGS   = $(CSTD) $(WARN) $(OPT) -ffp-contract=off $(PLATFORM_DEFS) $(INCLUDES) -MMD -MP

# ---------------------------------------------------------------------------
#  Diferenças de plataforma. No Windows os comandos rodam no cmd.exe.
# ---------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
  SHELL   := cmd.exe
  X       := .exe
  LIBS    := -lopengl32 -lgdi32 -lwinmm -lws2_32
  NET_LIBS := -lws2_32
  PLATFORM_DEFS :=
  LDFLAGS := -static
  # UBSan em modo "trap": aborta no comportamento indefinido sem precisar da
  # biblioteca de runtime, que não existe no MinGW.
  TEST_SAN := -fsanitize=undefined -fsanitize-trap=undefined
  fix    = $(subst /,\,$1)
  mkdir  = if not exist "$(call fix,$1)" mkdir "$(call fix,$1)"
  rmdir  = if exist "$(call fix,$1)" rmdir /s /q "$(call fix,$1)"
  rmfile = if exist "$(call fix,$1)" del /q "$(call fix,$1)"
else
  X       :=
  LIBS    := -lGL -lm -lpthread -ldl -lrt -lX11
  NET_LIBS :=
  # Com -std=c11 a glibc esconde o que não é C puro (getifaddrs, fcntl...).
  PLATFORM_DEFS := -D_DEFAULT_SOURCE
  LDFLAGS :=
  TEST_SAN := -fsanitize=address,undefined
  fix    = $1
  mkdir  = mkdir -p "$1"
  rmdir  = rm -rf "$1"
  rmfile = rm -f "$1"
endif

# ---------------------------------------------------------------------------
#  Fontes. A divisão em dois grupos é a regra de arquitetura do projeto:
#    LOGIC  -> não pode depender do raylib (só de raymath.h, que é header-only)
#    RENDER -> pode usar raylib à vontade
#  Os testes são ligados SEM o raylib; se um módulo de lógica chamar o raylib,
#  "mingw32-make test" falha na ligação.
# ---------------------------------------------------------------------------
LOGIC_DIRS  := src/core src/game src/voxel src/tools src/net
RENDER_DIRS := src/render src/ui src/audio src/platform

LOGIC_SRC  := $(foreach d,$(LOGIC_DIRS),$(wildcard $(d)/*.c))
RENDER_SRC := $(foreach d,$(RENDER_DIRS),$(wildcard $(d)/*.c))
GAME_SRC   := src/main.c $(LOGIC_SRC) $(RENDER_SRC)
TEST_SRC   := $(wildcard tests/*.c)

GAME         := terra$(X)
OBJ_DIR      := build/obj
TEST_OBJ_DIR := build/test-obj
TEST_BIN     := build/tests$(X)

GAME_OBJ := $(patsubst %.c,$(OBJ_DIR)/%.o,$(GAME_SRC))

# Matemática determinística (src/core/detmath.h): na lógica e nos testes, as
# funções trigonométricas da biblioteca C são trocadas pelas nossas, para
# Windows e Linux simularem a partida bit a bit igual (o online é lockstep).
DET_MATH   := -include src/core/detmath.h
det_for     = $(if $(filter $(addsuffix /%,$(LOGIC_DIRS)),$1),$(DET_MATH))
TEST_OBJ := $(patsubst %.c,$(TEST_OBJ_DIR)/%.o,$(LOGIC_SRC) $(TEST_SRC))

TEST_CFLAGS = $(CSTD) $(WARN) -O1 -g -ffp-contract=off $(TEST_SAN) $(PLATFORM_DEFS) $(INCLUDES) -Itests -MMD -MP $(DET_MATH)

.PHONY: all run test clean distclean
all: $(GAME)

# ---------------------------------------------------------------------------
#  Jogo
# ---------------------------------------------------------------------------
$(GAME): $(GAME_OBJ) $(RAYLIB_LIB)
	$(CC) $(GAME_OBJ) -o $@ $(LDFLAGS) $(RAYLIB_LIB) $(LIBS)

$(OBJ_DIR)/%.o: %.c Makefile | $(RAYLIB_SRC)/raylib.h
	@$(call mkdir,$(patsubst %/,%,$(dir $@)))
	$(CC) $(CFLAGS) $(call det_for,$<) -c $< -o $@

run: $(GAME)
	$(call fix,./$(GAME)) $(ARGS)

# ---------------------------------------------------------------------------
#  Testes da lógica (sem raylib na ligação)
# ---------------------------------------------------------------------------
$(TEST_BIN): $(TEST_OBJ)
	$(CC) $(TEST_OBJ) -o $@ $(TEST_SAN) -lm $(NET_LIBS)

$(TEST_OBJ_DIR)/%.o: %.c Makefile | $(RAYLIB_SRC)/raylib.h
	@$(call mkdir,$(patsubst %/,%,$(dir $@)))
	$(CC) $(TEST_CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	$(call fix,./$(TEST_BIN))

# ---------------------------------------------------------------------------
#  raylib: baixa só a pasta src/ da tag fixa e compila como biblioteca estática
# ---------------------------------------------------------------------------
$(RAYLIB_SRC)/raylib.h:
	git clone --depth 1 --branch $(RAYLIB_TAG) --filter=blob:none --sparse -c advice.detachedHead=false https://github.com/raysan5/raylib.git $(RAYLIB_DIR)
	git -C $(RAYLIB_DIR) sparse-checkout set src

$(RAYLIB_LIB): | $(RAYLIB_SRC)/raylib.h
	$(MAKE) -C $(RAYLIB_SRC) PLATFORM=PLATFORM_DESKTOP

# ---------------------------------------------------------------------------
clean:
	$(call rmdir,build)
	$(call rmfile,$(GAME))

distclean: clean
	$(call rmdir,$(RAYLIB_DIR))

# Dependências de headers geradas pelo compilador (-MMD). Os objetos dependem
# também do Makefile: mudar uma flag (como a matemática determinística)
# recompila tudo, em vez de misturar objetos velhos e novos.
-include $(GAME_OBJ:.o=.d)
-include $(TEST_OBJ:.o=.d)
