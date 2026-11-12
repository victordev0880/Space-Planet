#include "test.h"

#include "core/config.h"

static Config cfg;   // static: a struct é grande demais para a pilha

static void config_reads_numbers_and_text(void)
{
    const char *text =
        "# comentario\n"
        "\n"
        "stars.count = 3000\n"
        "  camera.fov=60.5   # comentario no fim\n"
        "nome = Terra Fraturada\r\n"
        "negativo = -2e3";
    CHECK(config_parse(&cfg, text));
    CHECK(cfg.count == 4);
    CHECK(cfg.error_count == 0);

    const ConfigEntry *e = config_find(&cfg, "stars.count");
    CHECK(e != NULL && e->is_number && e->number == 3000.0);
    e = config_find(&cfg, "camera.fov");
    CHECK(e != NULL && e->is_number);
    if (e != NULL) CHECK_NEAR(e->number, 60.5, 1e-9);
    e = config_find(&cfg, "nome");
    CHECK(e != NULL && !e->is_number);
    if (e != NULL) CHECK_STR(e->text, "Terra Fraturada");
    e = config_find(&cfg, "negativo");
    CHECK(e != NULL && e->is_number && e->number == -2000.0);
    CHECK(config_find(&cfg, "nao.existe") == NULL);
}

static void config_reports_first_error_with_line(void)
{
    CHECK(!config_parse(&cfg, "a = 1\nsem igual\nb = 2\n= 5\n"));
    CHECK(cfg.error_count == 2);
    CHECK(strstr(cfg.error, "linha 2") != NULL);
    // As linhas boas continuam valendo.
    CHECK(config_find(&cfg, "a") != NULL);
    CHECK(config_find(&cfg, "b") != NULL);
}

static void config_rejects_bad_lines(void)
{
    CHECK(!config_parse(&cfg, "a = 1\na = 2\n"));
    CHECK(strstr(cfg.error, "repetida") != NULL);

    CHECK(!config_parse(&cfg, "chave = \n"));
    CHECK(!config_parse(&cfg, "chave com espaco = 1\n"));
    CHECK(!config_parse(&cfg, "uma_chave_muito_longa_que_passa_do_limite_de_caracteres = 1\n"));
}

static void config_number_needs_whole_value(void)
{
    CHECK(config_parse(&cfg, "a = 12abc\nb = 1 2\nc = 1e2\n"));
    CHECK(!config_find(&cfg, "a")->is_number);
    CHECK(!config_find(&cfg, "b")->is_number);
    CHECK(config_find(&cfg, "c")->is_number);
}

static void config_parse_replaces_previous_content(void)
{
    CHECK(config_parse(&cfg, "a = 1\n"));
    CHECK(config_parse(&cfg, "b = 2\n"));
    CHECK(cfg.count == 1);
    CHECK(config_find(&cfg, "a") == NULL);
}

static void config_skips_utf8_bom(void)
{
    CHECK(config_parse(&cfg, "\xEF\xBB\xBF" "a = 1\nb = 2\n"));
    CHECK(cfg.count == 2);
    CHECK(config_find(&cfg, "a") != NULL);

    CHECK(config_parse(&cfg, "\xEF\xBB\xBF" "# comentario\na = 1\n"));
    CHECK(cfg.count == 1);
}

static void config_missing_file_is_an_error(void)
{
    CHECK(!config_load_file(&cfg, "data/arquivo_que_nao_existe.txt"));
    CHECK(cfg.error[0] != '\0');
}

void suite_config(void)
{
    RUN(config_reads_numbers_and_text);
    RUN(config_reports_first_error_with_line);
    RUN(config_rejects_bad_lines);
    RUN(config_number_needs_whole_value);
    RUN(config_parse_replaces_previous_content);
    RUN(config_skips_utf8_bom);
    RUN(config_missing_file_is_an_error);
}
