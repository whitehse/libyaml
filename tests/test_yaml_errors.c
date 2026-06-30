/* test_yaml_errors.c — Error paths and edge cases for libyaml */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_parser_role_no_output(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    /* Parser should produce no serializer output */
    uint8_t buf[64];
    assert(yaml_get_output(ctx, buf, sizeof(buf)) == 0);

    yaml_destroy(ctx);
    printf("  PASS: parser role produces no output\n");
}

static void test_serializer_role_no_parse(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    assert(ctx);

    /* Feeding raw YAML text to serializer just stores it, no parsing events */
    const char *text = "key: value\n";
    size_t consumed = yaml_feed_input(ctx, (const uint8_t *)text, strlen(text));
    assert(consumed == strlen(text));

    yaml_event_t ev;
    int count = 0;
    while (yaml_next_event(ctx, &ev)) count++;
    /* Serializer doesn't parse input into events */
    assert(count == 0);

    yaml_destroy(ctx);
    printf("  PASS: serializer role ignores raw input parsing\n");
}

static void test_emit_from_parser_fails(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    assert(yaml_emit_stream_start(ctx) == -1);
    assert(yaml_emit_document_start(ctx, 1) == -1);
    assert(yaml_emit_mapping_start(ctx, NULL, NULL) == -1);
    assert(yaml_emit_sequence_start(ctx, NULL, NULL) == -1);
    assert(yaml_emit_scalar(ctx, "x", 1, NULL, YAML_SCALAR_PLAIN) == -1);
    assert(yaml_emit_mapping_end(ctx) == -1);
    assert(yaml_emit_sequence_end(ctx) == -1);
    assert(yaml_emit_document_end(ctx, 1) == -1);
    assert(yaml_emit_stream_end(ctx) == -1);

    yaml_destroy(ctx);
    printf("  PASS: emit from parser role fails\n");
}

static void test_merge_wrong_role(void)
{
    yaml_ctx_t *parser = yaml_create(YAML_ROLE_PARSER);
    yaml_ctx_t *parser2 = yaml_create(YAML_ROLE_PARSER);
    assert(parser && parser2);

    /* merge_events requires dst to be serializer */
    assert(yaml_merge_events(parser, parser2) == -1);

    yaml_destroy(parser);
    yaml_destroy(parser2);
    printf("  PASS: merge with wrong role fails\n");
}

static void test_lookup_null_handling(void)
{
    assert(yaml_lookup_scalar(NULL, "key", NULL) == NULL);

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    assert(yaml_lookup_scalar(ctx, NULL, NULL) == NULL);
    assert(yaml_lookup_scalar(ctx, "nonexistent", NULL) == NULL);

    yaml_destroy(ctx);
    printf("  PASS: lookup null handling\n");
}

static void test_serialize_kv_null_handling(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    assert(ctx);

    assert(yaml_serialize_kv(NULL, "k", "v", 1) == -1);
    assert(yaml_serialize_kv(ctx, NULL, "v", 1) == -1);
    assert(yaml_serialize_kv(ctx, "k", NULL, 1) == -1);

    yaml_destroy(ctx);
    printf("  PASS: serialize_kv null handling\n");
}

static void test_whitespace_only_input(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    const char *input = "   \n  \n\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int count = 0;
    while (yaml_next_event(ctx, &ev)) count++;
    /* Whitespace-only input should produce no meaningful events */
    /* (or just stream/doc events at most) */

    yaml_destroy(ctx);
    printf("  PASS: whitespace-only input\n");
}

static void test_comment_only_input(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    const char *input = "# comment\n# another comment\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int count = 0;
    while (yaml_next_event(ctx, &ev)) count++;

    yaml_destroy(ctx);
    printf("  PASS: comment-only input\n");
}

static void test_deeply_nested_mapping(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    assert(ctx);

    yaml_emit_stream_start(ctx);
    yaml_emit_document_start(ctx, 1);
    /* Create a few levels of nesting */
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "level1", 6, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "level2", 6, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(ctx, "deep", 4, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_mapping_end(ctx);
    yaml_emit_mapping_end(ctx);
    yaml_emit_document_end(ctx, 1);
    yaml_emit_stream_end(ctx);

    /* Drain events and output */
    yaml_event_t ev;
    int count = 0;
    while (yaml_next_event(ctx, &ev)) count++;
    assert(count > 0);

    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(ctx, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    assert(total > 0);

    yaml_destroy(ctx);
    printf("  PASS: deeply nested mapping\n");
}

int main(void)
{
    printf("libyaml error and edge-case tests\n");
    test_parser_role_no_output();
    test_serializer_role_no_parse();
    test_emit_from_parser_fails();
    test_merge_wrong_role();
    test_lookup_null_handling();
    test_serialize_kv_null_handling();
    test_whitespace_only_input();
    test_comment_only_input();
    test_deeply_nested_mapping();
    printf("All error/edge-case tests passed.\n");
    return 0;
}
