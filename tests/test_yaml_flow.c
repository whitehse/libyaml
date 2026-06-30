/* test_yaml_flow.c — Tests for flow collection parsing ([item1, item2] and {k: v}) */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_simple_flow_sequence(void)
{
    const char *input = "[a, b, c]\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int seq_starts = 0, seq_ends = 0, scalars = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SEQUENCE_START) seq_starts++;
        if (ev.type == YAML_EVENT_SEQUENCE_END) seq_ends++;
        if (ev.type == YAML_EVENT_SCALAR) scalars++;
    }
    assert(seq_starts == 1);
    assert(seq_ends == 1);
    assert(scalars == 3);  /* a, b, c */

    yaml_destroy(ctx);
    printf("  PASS: simple flow sequence\n");
}

static void test_flow_mapping_standalone(void)
{
    const char *input = "{key: value, key2: value2}\n";
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
    assert(map_starts == 1);
    assert(map_ends == 1);
    /* key + value + key2 + value2 = 4 scalars */
    assert(scalars == 4);

    yaml_destroy(ctx);
    printf("  PASS: flow mapping standalone\n");
}

static void test_flow_sequence_values(void)
{
    const char *input = "items: [first, second, third]\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    const char *expected[] = {"items", "first", "second", "third"};
    int idx = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) {
            assert(idx < 4);
            assert(strcmp(ev.data.scalar.value, expected[idx]) == 0);
            idx++;
        }
    }
    assert(idx == 4);

    yaml_destroy(ctx);
    printf("  PASS: flow sequence values\n");
}

static void test_flow_mapping_values(void)
{
    const char *input = "data: {host: localhost, port: 8080}\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_host = 0, found_port = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) {
            if (strcmp(ev.data.scalar.value, "localhost") == 0) found_host = 1;
            if (strcmp(ev.data.scalar.value, "8080") == 0) found_port = 1;
        }
    }
    assert(found_host);
    assert(found_port);

    yaml_destroy(ctx);
    printf("  PASS: flow mapping values\n");
}

static void test_nested_flow(void)
{
    const char *input = "data: {items: [1, 2, 3]}\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int map_starts = 0, seq_starts = 0, scalars = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
        if (ev.type == YAML_EVENT_SEQUENCE_START) seq_starts++;
        if (ev.type == YAML_EVENT_SCALAR) scalars++;
    }
    /* outer mapping + flow mapping = 2 mapping starts */
    assert(map_starts == 2);
    /* flow sequence = 1 sequence start */
    assert(seq_starts == 1);
    /* data + items + 1 + 2 + 3 = 5 scalars */
    assert(scalars == 5);

    yaml_destroy(ctx);
    printf("  PASS: nested flow\n");
}

static void test_flow_in_mapping_value(void)
{
    const char *input = "key: [a, b]\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int map_starts = 0, seq_starts = 0, scalars = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
        if (ev.type == YAML_EVENT_SEQUENCE_START) seq_starts++;
        if (ev.type == YAML_EVENT_SCALAR) {
            scalars++;
        }
    }
    /* outer mapping = 1 mapping start */
    assert(map_starts == 1);
    /* flow sequence = 1 sequence start */
    assert(seq_starts == 1);
    /* key + a + b = 3 scalars */
    assert(scalars == 3);

    yaml_destroy(ctx);
    printf("  PASS: flow in mapping value\n");
}

static void test_empty_flow_sequence(void)
{
    const char *input = "items: []\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int seq_starts = 0, seq_ends = 0, scalars = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SEQUENCE_START) seq_starts++;
        if (ev.type == YAML_EVENT_SEQUENCE_END) seq_ends++;
        if (ev.type == YAML_EVENT_SCALAR) scalars++;
    }
    assert(seq_starts == 1);
    assert(seq_ends == 1);
    /* Only "items" key scalar — no items in empty sequence */
    assert(scalars == 1);

    yaml_destroy(ctx);
    printf("  PASS: empty flow sequence\n");
}

static void test_empty_flow_mapping(void)
{
    const char *input = "data: {}\n";
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
    /* outer mapping + flow mapping = 2 mapping starts */
    assert(map_starts == 2);
    assert(map_ends == 2);
    /* Only "data" key scalar — no entries in empty mapping */
    assert(scalars == 1);

    yaml_destroy(ctx);
    printf("  PASS: empty flow mapping\n");
}

static void test_quoted_flow_items(void)
{
    const char *input = "items: [\"hello world\", 'single']\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_hello = 0, found_single = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) {
            if (strcmp(ev.data.scalar.value, "hello world") == 0) found_hello = 1;
            if (strcmp(ev.data.scalar.value, "single") == 0) found_single = 1;
        }
    }
    assert(found_hello);
    assert(found_single);

    yaml_destroy(ctx);
    printf("  PASS: quoted flow items\n");
}

static void test_knowledge_tree_flow_mapping(void)
{
    const char *input = "data: {host: localhost, port: 8080}\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    /* Drain all events */
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    /* Look up values in knowledge tree */
    size_t len = 0;
    const char *host = yaml_lookup_scalar(ctx, "data.host", &len);
    assert(host != NULL);
    assert(strncmp(host, "localhost", len) == 0);

    const char *port = yaml_lookup_scalar(ctx, "data.port", &len);
    assert(port != NULL);
    assert(strncmp(port, "8080", len) == 0);

    yaml_destroy(ctx);
    printf("  PASS: knowledge tree flow mapping\n");
}

static void test_knowledge_tree_flow_sequence(void)
{
    const char *input = "items: [first, second, third]\n";
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    /* Drain all events */
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    /* Look up indexed items in knowledge tree */
    size_t len = 0;
    const char *item0 = yaml_lookup_scalar(ctx, "items.0", &len);
    assert(item0 != NULL);
    assert(strncmp(item0, "first", len) == 0);

    const char *item1 = yaml_lookup_scalar(ctx, "items.1", &len);
    assert(item1 != NULL);
    assert(strncmp(item1, "second", len) == 0);

    const char *item2 = yaml_lookup_scalar(ctx, "items.2", &len);
    assert(item2 != NULL);
    assert(strncmp(item2, "third", len) == 0);

    yaml_destroy(ctx);
    printf("  PASS: knowledge tree flow sequence\n");
}

int main(void)
{
    printf("libyaml flow collection tests\n");
    test_simple_flow_sequence();
    test_flow_mapping_standalone();
    test_flow_sequence_values();
    test_flow_mapping_values();
    test_nested_flow();
    test_flow_in_mapping_value();
    test_empty_flow_sequence();
    test_empty_flow_mapping();
    test_quoted_flow_items();
    test_knowledge_tree_flow_mapping();
    test_knowledge_tree_flow_sequence();
    printf("All flow collection tests passed.\n");
    return 0;
}
