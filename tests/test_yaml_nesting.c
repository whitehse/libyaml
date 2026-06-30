/* test_yaml_nesting.c — Tests for indentation-based nesting
 *
 * Tests parsing of nested mappings, mixed sequences/mappings,
 * multi-level indentation, and round-trip fidelity.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_nested_mapping(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int map_starts = 0, map_ends = 0, scalars = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
        if (ev.type == YAML_EVENT_MAPPING_END) map_ends++;
        if (ev.type == YAML_EVENT_SCALAR) scalars++;
    }
    /* Expect: outer mapping + inner mapping = 2 mapping starts */
    assert(map_starts == 2);
    assert(map_ends == 2);
    /* Expect: server, host, localhost, port, 8080 = 5 scalars */
    assert(scalars == 5);

    yaml_destroy(ctx);
    printf("  PASS: nested mapping\n");
}

static void test_nested_mapping_knowledge(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    /* Knowledge tree should have top-level "server" key with no scalar value
     * (its value is a mapping, not a scalar).
     * The nested keys are NOT at the top level, so "host" should not be found
     * as a top-level key. */
    const char *val = yaml_lookup_scalar(ctx, "server", NULL);
    /* server's value is a collection, not a scalar — should be NULL */
    assert(val == NULL);

    yaml_destroy(ctx);
    printf("  PASS: nested mapping knowledge tree\n");
}

static void test_triple_nested(void)
{
    const char *input =
        "level1:\n"
        "  level2:\n"
        "    level3: deep_value\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int map_starts = 0, map_ends = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
        if (ev.type == YAML_EVENT_MAPPING_END) map_ends++;
    }
    assert(map_starts == 3);
    assert(map_ends == 3);

    yaml_destroy(ctx);
    printf("  PASS: triple nested mapping\n");
}

static void test_mixed_sequence_of_mappings(void)
{
    const char *input =
        "- name: first\n"
        "  value: 1\n"
        "- name: second\n"
        "  value: 2\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int seq_starts = 0, seq_ends = 0;
    int map_starts = 0, map_ends = 0;
    int scalar_count = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SEQUENCE_START) seq_starts++;
        if (ev.type == YAML_EVENT_SEQUENCE_END) seq_ends++;
        if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
        if (ev.type == YAML_EVENT_MAPPING_END) map_ends++;
        if (ev.type == YAML_EVENT_SCALAR) scalar_count++;
    }
    /* 1 sequence containing 2 mappings, each with 2 key-value pairs (8 scalars) */
    assert(seq_starts == 1);
    assert(seq_ends == 1);
    assert(map_starts == 2);
    assert(map_ends == 2);
    assert(scalar_count == 8);

    yaml_destroy(ctx);
    printf("  PASS: mixed sequence of mappings\n");
}

static void test_mapping_with_sequence_value(void)
{
    const char *input =
        "items:\n"
        "  - alpha\n"
        "  - beta\n"
        "  - gamma\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int seq_starts = 0, map_starts = 0, scalar_count = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SEQUENCE_START) seq_starts++;
        if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
        if (ev.type == YAML_EVENT_SCALAR) scalar_count++;
    }
    /* 1 mapping, 1 sequence, 4 scalars (items + alpha + beta + gamma) */
    assert(seq_starts == 1);
    assert(map_starts == 1);
    assert(scalar_count == 4);

    yaml_destroy(ctx);
    printf("  PASS: mapping with sequence value\n");
}

static void test_multiple_top_level_keys_with_nesting(void)
{
    const char *input =
        "database:\n"
        "  host: db.example.com\n"
        "  port: 5432\n"
        "server:\n"
        "  host: api.example.com\n"
        "  port: 3000\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int map_starts = 0, map_ends = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
        if (ev.type == YAML_EVENT_MAPPING_END) map_ends++;
    }
    /* outer mapping + 2 nested mappings = 3 mapping starts */
    assert(map_starts == 3);
    assert(map_ends == 3);

    yaml_destroy(ctx);
    printf("  PASS: multiple top-level keys with nesting\n");
}

static void test_nesting_round_trip(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_ctx_t *parser = yaml_create(YAML_ROLE_PARSER);
    yaml_ctx_t *serializer = yaml_create(YAML_ROLE_SERIALIZER);
    assert(parser && serializer);

    yaml_feed_input(parser, (const uint8_t *)input, strlen(input));

    int merged = yaml_merge_events(serializer, parser);
    assert(merged > 0);

    /* Get serialized output */
    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(serializer, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    assert(total > 0);

    /* Verify key content is present */
    assert(strstr((char *)buf, "server") != NULL);
    assert(strstr((char *)buf, "host") != NULL);
    assert(strstr((char *)buf, "localhost") != NULL);

    yaml_destroy(parser);
    yaml_destroy(serializer);
    printf("  PASS: nesting round trip\n");
}

int main(void)
{
    printf("libyaml nesting tests\n");
    test_nested_mapping();
    test_nested_mapping_knowledge();
    test_triple_nested();
    test_mixed_sequence_of_mappings();
    test_mapping_with_sequence_value();
    test_multiple_top_level_keys_with_nesting();
    test_nesting_round_trip();
    printf("All nesting tests passed.\n");
    return 0;
}
