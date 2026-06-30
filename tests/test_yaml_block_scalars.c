/* test_yaml_block_scalars.c — Tests for literal (|) and folded (>) block scalars */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_literal_block_scalar(void)
{
    const char *input =
        "description: |\n"
        "  This is line one.\n"
        "  This is line two.\n"
        "  This is line three.\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_value = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR && ev.data.scalar.value_len > 0) {
            if (ev.data.scalar.style == YAML_SCALAR_LITERAL) {
                found_value = 1;
                /* Literal should preserve newlines */
                assert(strstr(ev.data.scalar.value, "line one.") != NULL);
                assert(strstr(ev.data.scalar.value, "line two.") != NULL);
                assert(strstr(ev.data.scalar.value, "line three.") != NULL);
                /* Should contain newlines between lines */
                assert(strstr(ev.data.scalar.value, "\n") != NULL);
                printf("  Literal value: [%s]\n", ev.data.scalar.value);
            }
        }
    }
    assert(found_value);

    yaml_destroy(ctx);
    printf("  PASS: literal block scalar\n");
}

static void test_folded_block_scalar(void)
{
    const char *input =
        "description: >\n"
        "  This is line one.\n"
        "  This is line two.\n"
        "  This is line three.\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_value = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR && ev.data.scalar.value_len > 0) {
            if (ev.data.scalar.style == YAML_SCALAR_FOLDED) {
                found_value = 1;
                /* Folded should join lines with spaces */
                printf("  Folded value: [%s]\n", ev.data.scalar.value);
                assert(strstr(ev.data.scalar.value, "line one.") != NULL);
                assert(strstr(ev.data.scalar.value, "line two.") != NULL);
            }
        }
    }
    assert(found_value);

    yaml_destroy(ctx);
    printf("  PASS: folded block scalar\n");
}

static void test_literal_in_mapping(void)
{
    const char *input =
        "name: test\n"
        "content: |\n"
        "  hello\n"
        "  world\n"
        "version: 1\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int scalar_count = 0;
    int literal_found = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) {
            scalar_count++;
            if (ev.data.scalar.style == YAML_SCALAR_LITERAL) {
                literal_found = 1;
                assert(strstr(ev.data.scalar.value, "hello") != NULL);
                assert(strstr(ev.data.scalar.value, "world") != NULL);
            }
        }
    }
    /* name, test, content, hello\nworld\n, version, 1 = 6 scalars */
    assert(scalar_count == 6);
    assert(literal_found);

    yaml_destroy(ctx);
    printf("  PASS: literal block scalar in mapping\n");
}

int main(void)
{
    printf("libyaml block scalar tests\n");
    test_literal_block_scalar();
    test_folded_block_scalar();
    test_literal_in_mapping();
    printf("All block scalar tests passed.\n");
    return 0;
}
