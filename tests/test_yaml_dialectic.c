/* test_yaml_dialectic.c — Dialectic (paired buffer exchange) tests
 *
 * Parser context parses YAML text → events.
 * Serializer context emits events → YAML text output.
 * Both exchange data purely through in-memory buffers.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Round-trip: parse YAML text → events → serialize back → compare structure */
static void test_round_trip_mapping(void)
{
    const char *input = "host: localhost\nport: 8080\n";
    yaml_ctx_t *parser = yaml_create(YAML_ROLE_PARSER);
    yaml_ctx_t *serializer = yaml_create(YAML_ROLE_SERIALIZER);
    assert(parser && serializer);

    /* Feed input to parser */
    size_t consumed = yaml_feed_input(parser, (const uint8_t *)input, strlen(input));
    assert(consumed == strlen(input));

    /* Merge parser events into serializer */
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
    assert(strstr((char *)buf, "host") != NULL);
    assert(strstr((char *)buf, "localhost") != NULL);
    assert(strstr((char *)buf, "port") != NULL);

    yaml_destroy(parser);
    yaml_destroy(serializer);
    printf("  PASS: round-trip mapping\n");
}

static void test_round_trip_sequence(void)
{
    const char *input = "- alpha\n- beta\n- gamma\n";
    yaml_ctx_t *parser = yaml_create(YAML_ROLE_PARSER);
    yaml_ctx_t *serializer = yaml_create(YAML_ROLE_SERIALIZER);
    assert(parser && serializer);

    yaml_feed_input(parser, (const uint8_t *)input, strlen(input));
    int merged = yaml_merge_events(serializer, parser);
    assert(merged > 0);

    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(serializer, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    assert(total > 0);

    yaml_destroy(parser);
    yaml_destroy(serializer);
    printf("  PASS: round-trip sequence\n");
}

static void test_multi_document(void)
{
    const char *input = "---\na: 1\n---\nb: 2\n...\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *parser = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(parser);

    yaml_feed_input(parser, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int doc_starts = 0;
    int doc_ends = 0;
    while (yaml_next_event(parser, &ev)) {
        if (ev.type == YAML_EVENT_DOCUMENT_START) doc_starts++;
        if (ev.type == YAML_EVENT_DOCUMENT_END) doc_ends++;
    }
    assert(doc_starts >= 2);
    assert(doc_ends >= 2);

    yaml_destroy(parser);
    printf("  PASS: multi-document\n");
}

static void test_quoted_scalars(void)
{
    const char *input = "key: \"hello world\"\n";
    yaml_ctx_t *parser = yaml_create(YAML_ROLE_PARSER);
    assert(parser);

    yaml_feed_input(parser, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_value = 0;
    while (yaml_next_event(parser, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR && ev.data.scalar.value_len > 0) {
            if (strcmp(ev.data.scalar.value, "hello world") == 0) {
                found_value = 1;
            }
        }
    }
    assert(found_value);

    yaml_destroy(parser);
    printf("  PASS: quoted scalars\n");
}

static void test_kv_serialization(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    assert(ctx);

    yaml_emit_stream_start(ctx);
    yaml_emit_document_start(ctx, 1);
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_serialize_kv(ctx, "database", "postgres", 8);
    yaml_serialize_kv(ctx, "host", "localhost", 9);
    yaml_emit_mapping_end(ctx);
    yaml_emit_document_end(ctx, 1);
    yaml_emit_stream_end(ctx);

    /* Drain events */
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(ctx, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    assert(total > 0);
    assert(strstr((char *)buf, "database") != NULL);
    assert(strstr((char *)buf, "postgres") != NULL);

    yaml_destroy(ctx);
    printf("  PASS: kv serialization\n");
}

static void test_empty_input(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    /* Feed empty input */
    size_t consumed = yaml_feed_input(ctx, (const uint8_t *)"", 0);
    assert(consumed == 0);

    yaml_event_t ev;
    int count = 0;
    while (yaml_next_event(ctx, &ev)) count++;
    assert(count == 0);

    yaml_destroy(ctx);
    printf("  PASS: empty input\n");
}

static void test_incremental_feed(void)
{
    const char *part1 = "nam";
    const char *part2 = "e: test\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)part1, strlen(part1));

    yaml_event_t ev;
    int count1 = 0;
    while (yaml_next_event(ctx, &ev)) count1++;
    /* May have partial events or none yet */

    yaml_feed_input(ctx, (const uint8_t *)part2, strlen(part2));
    int count2 = 0;
    while (yaml_next_event(ctx, &ev)) count2++;
    assert(count2 > 0 || count1 > 0); /* at least one feed produced events */

    yaml_destroy(ctx);
    printf("  PASS: incremental feed\n");
}

int main(void)
{
    printf("libyaml dialectic tests\n");
    test_round_trip_mapping();
    test_round_trip_sequence();
    test_multi_document();
    test_quoted_scalars();
    test_kv_serialization();
    test_empty_input();
    test_incremental_feed();
    printf("All dialectic tests passed.\n");
    return 0;
}
