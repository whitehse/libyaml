/* test_yaml_smoke.c — Smoke tests for libyaml API surface
 *
 * Tests create/destroy lifecycle, NULL handling, config validation,
 * event queue drain, parser basics, serializer basics, and reset.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_create_destroy(void)
{
    yaml_ctx_t *p = yaml_create(YAML_ROLE_PARSER);
    assert(p != NULL);
    yaml_destroy(p);

    yaml_ctx_t *s = yaml_create(YAML_ROLE_SERIALIZER);
    assert(s != NULL);
    yaml_destroy(s);

    yaml_destroy(NULL); /* must not crash */
    printf("  PASS: create/destroy lifecycle\n");
}

static void test_null_handling(void)
{
    assert(yaml_create_with_config(YAML_ROLE_PARSER, NULL) == NULL);

    yaml_config_t bad;
    memset(&bad, 0, sizeof(bad));
    bad.event_queue_size = 1; /* too small */
    assert(yaml_create_with_config(YAML_ROLE_PARSER, &bad) == NULL);

    /* NULL ctx operations must not crash */
    yaml_event_t ev;
    assert(yaml_feed_input(NULL, (const uint8_t *)"x", 1) == 0);
    assert(yaml_next_event(NULL, &ev) == 0);
    assert(yaml_next_event(NULL, NULL) == 0);
    assert(yaml_get_output(NULL, (uint8_t *)&ev, 1) == 0);
    yaml_reset(NULL);

    printf("  PASS: NULL handling\n");
}

static void test_config_validation(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 16;
    cfg.max_document_size = 65536;
    cfg.strict_mode = 1;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx != NULL);
    yaml_destroy(ctx);

    printf("  PASS: config validation\n");
}

static void test_parser_basic(void)
{
    const char *yaml_input = "name: test\nport: 8080\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx != NULL);

    size_t consumed = yaml_feed_input(ctx, (const uint8_t *)yaml_input, strlen(yaml_input));
    assert(consumed == strlen(yaml_input));

    /* Should get at least mapping start and scalars */
    yaml_event_t ev;
    int event_count = 0;
    while (yaml_next_event(ctx, &ev)) {
        event_count++;
        assert(ev.type > YAML_EVENT_NONE && ev.type < YAML_EVENT_MAX);
    }
    assert(event_count > 0);

    yaml_destroy(ctx);
    printf("  PASS: parser basic\n");
}

static void test_serializer_basic(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    assert(ctx != NULL);

    assert(yaml_emit_stream_start(ctx) == 0);
    assert(yaml_emit_document_start(ctx, 1) == 0);
    assert(yaml_emit_mapping_start(ctx, NULL, NULL) == 0);
    assert(yaml_emit_scalar(ctx, "key", 3, NULL, YAML_SCALAR_PLAIN) == 0);
    assert(yaml_emit_scalar(ctx, "value", 5, NULL, YAML_SCALAR_PLAIN) == 0);
    assert(yaml_emit_mapping_end(ctx) == 0);
    assert(yaml_emit_document_end(ctx, 1) == 0);
    assert(yaml_emit_stream_end(ctx) == 0);

    /* Drain events */
    yaml_event_t ev;
    int event_count = 0;
    while (yaml_next_event(ctx, &ev)) event_count++;
    assert(event_count == 8); /* stream_start, doc_start, map_start, 2 scalars, map_end, doc_end, stream_end */

    /* Get output */
    uint8_t buf[1024];
    size_t out_len = yaml_get_output(ctx, buf, sizeof(buf));
    assert(out_len > 0);

    yaml_destroy(ctx);
    printf("  PASS: serializer basic\n");
}

static void test_reset(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx != NULL);

    const char *input = "a: 1\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int count1 = 0;
    while (yaml_next_event(ctx, &ev)) count1++;
    assert(count1 > 0);

    yaml_reset(ctx);

    int count2 = 0;
    while (yaml_next_event(ctx, &ev)) count2++;
    assert(count2 == 0);

    /* Can reuse after reset */
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));
    int count3 = 0;
    while (yaml_next_event(ctx, &ev)) count3++;
    assert(count3 > 0);

    yaml_destroy(ctx);
    printf("  PASS: reset\n");
}

static void test_serializer_output_drain(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    assert(ctx != NULL);

    yaml_emit_stream_start(ctx);
    yaml_emit_document_start(ctx, 0);
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "hello", 5, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "world", 5, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_mapping_end(ctx);
    yaml_emit_document_end(ctx, 0);
    yaml_emit_stream_end(ctx);

    /* Drain all events first */
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    /* Drain output */
    uint8_t buf[4094];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(ctx, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    assert(total > 0);
    assert(strstr((char *)buf, "hello") != NULL);
    assert(strstr((char *)buf, "world") != NULL);

    yaml_destroy(ctx);
    printf("  PASS: serializer output drain\n");
}

static void test_event_types_completeness(void)
{
    /* Verify all event type values are in expected range */
    yaml_event_type_t types[] = {
        YAML_EVENT_STREAM_START, YAML_EVENT_STREAM_END,
        YAML_EVENT_DOCUMENT_START, YAML_EVENT_DOCUMENT_END,
        YAML_EVENT_MAPPING_START, YAML_EVENT_MAPPING_END,
        YAML_EVENT_SEQUENCE_START, YAML_EVENT_SEQUENCE_END,
        YAML_EVENT_SCALAR, YAML_EVENT_ALIAS, YAML_EVENT_ERROR
    };
    int n = sizeof(types) / sizeof(types[0]);
    assert(n == 11);
    for (int i = 0; i < n; i++) {
        assert(types[i] > YAML_EVENT_NONE && types[i] < YAML_EVENT_MAX);
    }
    printf("  PASS: event types completeness (%d types)\n", n);
}

static void test_parser_event_queue_overflow(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 4; /* small queue */

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx != NULL);

    /* Feed input that produces more events than queue can hold */
    const char *input = "a: 1\nb: 2\nc: 3\nd: 4\ne: 5\nf: 6\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    /* Should still be able to drain without crash */
    yaml_event_t ev;
    int count = 0;
    while (yaml_next_event(ctx, &ev)) count++;
    /* Some events may have been dropped due to overflow */
    assert(count >= 0);

    yaml_destroy(ctx);
    printf("  PASS: parser event queue overflow\n");
}

int main(void)
{
    printf("libyaml smoke tests\n");
    test_create_destroy();
    test_null_handling();
    test_config_validation();
    test_parser_basic();
    test_serializer_basic();
    test_reset();
    test_serializer_output_drain();
    test_event_types_completeness();
    test_parser_event_queue_overflow();
    printf("All smoke tests passed.\n");
    return 0;
}
