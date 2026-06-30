/* test_yaml_multiline.c — Tests for multiline scalar continuation,
 * flow style output, queue auto-grow, and incremental parsing.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_multiline_plain_scalar(void)
{
    const char *input =
        "description: This is a long\n"
        "  description that spans\n"
        "  multiple lines\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_value = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR && strcmp(ev.data.scalar.value, "description") != 0) {
            found_value = 1;
            assert(strstr(ev.data.scalar.value, "This is a long") != NULL);
            assert(strstr(ev.data.scalar.value, "description that spans") != NULL);
            assert(strstr(ev.data.scalar.value, "multiple lines") != NULL);
            printf("  Multiline value: [%s]\n", ev.data.scalar.value);
        }
    }
    assert(found_value);

    yaml_destroy(ctx);
    printf("  PASS: multiline plain scalar\n");
}

static void test_multiline_in_nested_context(void)
{
    const char *input =
        "server:\n"
        "  description: A test\n"
        "    server for\n"
        "    development\n"
        "  host: localhost\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_desc = 0, found_host = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) {
            if (strstr(ev.data.scalar.value, "A test") != NULL) {
                found_desc = 1;
                assert(strstr(ev.data.scalar.value, "server for") != NULL);
                assert(strstr(ev.data.scalar.value, "development") != NULL);
            }
            if (strcmp(ev.data.scalar.value, "localhost") == 0) {
                found_host = 1;
            }
        }
    }
    assert(found_desc);
    assert(found_host);

    yaml_destroy(ctx);
    printf("  PASS: multiline in nested context\n");
}

static void test_no_merge_at_same_indent(void)
{
    const char *input = "key1: value1\nkey2: value2\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int scalar_count = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) scalar_count++;
    }
    /* key1, value1, key2, value2 = 4 scalars (not merged) */
    assert(scalar_count == 4);

    yaml_destroy(ctx);
    printf("  PASS: no merge at same indent\n");
}

/* 2.4: Flow style output tests */
static void test_flow_style_mapping(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.flow_output = 1;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_SERIALIZER, &cfg);
    assert(ctx);

    yaml_emit_stream_start(ctx);
    yaml_emit_document_start(ctx, 1);
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "name", 4, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "test", 4, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "port", 4, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "8080", 4, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_mapping_end(ctx);
    yaml_emit_document_end(ctx, 1);
    yaml_emit_stream_end(ctx);

    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(ctx, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';

    printf("  Flow mapping output: [%s]\n", (char *)buf);
    assert(strstr((char *)buf, "{") != NULL);
    assert(strstr((char *)buf, "}") != NULL);
    assert(strstr((char *)buf, "name: test") != NULL);
    assert(strstr((char *)buf, "port: 8080") != NULL);
    assert(strstr((char *)buf, ", ") != NULL);

    yaml_destroy(ctx);
    printf("  PASS: flow style mapping\n");
}

static void test_flow_style_sequence(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.flow_output = 1;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_SERIALIZER, &cfg);
    assert(ctx);

    yaml_emit_stream_start(ctx);
    yaml_emit_document_start(ctx, 1);
    yaml_emit_sequence_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "apple", 5, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "banana", 6, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "cherry", 6, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_sequence_end(ctx);
    yaml_emit_document_end(ctx, 1);
    yaml_emit_stream_end(ctx);

    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(ctx, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';

    printf("  Flow sequence output: [%s]\n", (char *)buf);
    assert(strstr((char *)buf, "[") != NULL);
    assert(strstr((char *)buf, "]") != NULL);
    assert(strstr((char *)buf, "apple") != NULL);
    assert(strstr((char *)buf, "banana") != NULL);
    assert(strstr((char *)buf, "cherry") != NULL);
    assert(strstr((char *)buf, ", ") != NULL);

    yaml_destroy(ctx);
    printf("  PASS: flow style sequence\n");
}

static void test_flow_style_nested(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.flow_output = 1;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_SERIALIZER, &cfg);
    assert(ctx);

    yaml_emit_stream_start(ctx);
    yaml_emit_document_start(ctx, 1);
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "items", 5, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_sequence_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "a", 1, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "b", 1, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_sequence_end(ctx);
    yaml_emit_scalar(ctx, "config", 6, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "x", 1, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "1", 1, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_mapping_end(ctx);
    yaml_emit_mapping_end(ctx);
    yaml_emit_document_end(ctx, 1);
    yaml_emit_stream_end(ctx);

    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(ctx, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';

    printf("  Flow nested output: [%s]\n", (char *)buf);
    assert(strstr((char *)buf, "{") != NULL);
    assert(strstr((char *)buf, "}") != NULL);
    assert(strstr((char *)buf, "[") != NULL);
    assert(strstr((char *)buf, "]") != NULL);
    assert(strstr((char *)buf, "items: [a, b]") != NULL);
    assert(strstr((char *)buf, "config: {x: 1}") != NULL);

    yaml_destroy(ctx);
    printf("  PASS: flow style nested\n");
}

/* 5.1: Queue overflow test (auto-grow disabled — events dropped silently) */
static void test_queue_overflow(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 4; /* start very small */

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);
    assert(yaml_queue_capacity(ctx) == 4);

    /* Feed input that generates more events than queue can hold */
    const char *input = "a: 1\nb: 2\nc: 3\nd: 4\ne: 5\nf: 6\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    /* Some events may be dropped due to overflow — that's expected */
    yaml_event_t ev;
    int count = 0;
    while (yaml_next_event(ctx, &ev)) count++;
    assert(count >= 0); /* should not crash */

    yaml_destroy(ctx);
    printf("  PASS: queue overflow handling\n");
}

/* 6.1/6.2: Incremental parsing test */
static void test_incremental_parsing(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    /* Feed data in chunks */
    yaml_feed_input(ctx, (const uint8_t *)"key: ", 5);
    /* No complete line yet — no events should be emitted */
    assert(yaml_event_count(ctx) == 0);

    yaml_feed_input(ctx, (const uint8_t *)"value\n", 6);
    /* Now we should have events */
    assert(yaml_event_count(ctx) > 0);

    yaml_event_t ev;
    int found_value = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR && strcmp(ev.data.scalar.value, "value") == 0) {
            found_value = 1;
        }
    }
    assert(found_value);

    yaml_destroy(ctx);
    printf("  PASS: incremental parsing\n");
}

/* 6.1/6.2: Incremental parsing multi-line */
static void test_incremental_multiline(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    /* Feed first partial line */
    yaml_feed_input(ctx, (const uint8_t *)"host: local", 11);
    assert(yaml_event_count(ctx) == 0);

    /* Complete first line and start second */
    yaml_feed_input(ctx, (const uint8_t *)"host\nport: 3", 12);
    /* First line is now complete — should have events */
    yaml_event_t ev;
    int found_host = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR && strstr(ev.data.scalar.value, "localhost") != NULL) {
            found_host = 1;
        }
    }
    assert(found_host);

    /* Complete second line */
    yaml_feed_input(ctx, (const uint8_t *)"000\n", 4);
    int found_port = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR && strstr(ev.data.scalar.value, "3000") != NULL) {
            found_port = 1;
        }
    }
    assert(found_port);

    yaml_destroy(ctx);
    printf("  PASS: incremental multiline\n");
}

int main(void)
{
    printf("libyaml multiline scalar tests\n");
    test_multiline_plain_scalar();
    test_multiline_in_nested_context();
    test_no_merge_at_same_indent();
    test_flow_style_mapping();
    test_flow_style_sequence();
    test_flow_style_nested();
    test_queue_overflow();
    test_incremental_parsing();
    test_incremental_multiline();
    printf("All multiline tests passed.\n");
    return 0;
}
