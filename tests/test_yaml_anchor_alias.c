/* test_yaml_anchor_alias.c — Anchor and Alias tests
 *
 * Tests anchor definition, alias reference, round-trip with anchors/aliases,
 * multiple anchors, and alias-without-anchor edge case.
 *
 * NOTE: The current parser does not expose anchor/tag metadata on events,
 * so tests focus on: (a) correct structural parsing of anchored nodes,
 * (b) correct ALIAS event emission, and (c) round-trip preservation of
 * content through anchor/alias structures.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Helper: parse YAML, collect events into arrays for inspection */
#define MAX_EVENTS 128

typedef struct {
    yaml_event_t events[MAX_EVENTS];
    int count;
} event_list_t;

static void collect_events(yaml_ctx_t *ctx, event_list_t *list)
{
    list->count = 0;
    while (list->count < MAX_EVENTS && yaml_next_event(ctx, &list->events[list->count])) {
        list->count++;
    }
}

/* Test 1: Anchor definition on a scalar value: &anchor value
 * The parser should parse the YAML without error; the anchored scalar
 * value should be accessible.
 */
static void test_anchor_on_scalar(void)
{
    const char *input = "key: &myanchor myvalue\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    event_list_t el;
    collect_events(ctx, &el);
    assert(el.count > 0);

    /* Verify no ERROR events */
    for (int i = 0; i < el.count; i++) {
        assert(el.events[i].type != YAML_EVENT_ERROR);
    }

    /* Find the scalar event with value "myvalue" */
    int found = 0;
    for (int i = 0; i < el.count; i++) {
        if (el.events[i].type == YAML_EVENT_SCALAR) {
            if (strcmp(el.events[i].data.scalar.value, "myvalue") == 0) {
                found = 1;
            }
        }
    }
    assert(found);

    yaml_destroy(ctx);
    printf("  PASS: anchor on scalar\n");
}

/* Test 2: Alias reference: *anchor
 * The parser should emit an ALIAS event for *defaults.
 */
static void test_alias_reference(void)
{
    const char *input =
        "defaults: &defaults\n"
        "  timeout: 30\n"
        "  retries: 3\n"
        "service:\n"
        "  <<: *defaults\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 64;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    event_list_t el;
    collect_events(ctx, &el);
    assert(el.count > 0);

    /* Look for an alias event referencing "defaults" */
    int found_alias = 0;
    for (int i = 0; i < el.count; i++) {
        if (el.events[i].type == YAML_EVENT_ALIAS) {
            if (strcmp(el.events[i].data.alias.anchor, "defaults") == 0) {
                found_alias = 1;
            }
        }
    }
    assert(found_alias);

    yaml_destroy(ctx);
    printf("  PASS: alias reference\n");
}

/* Test 3: Anchor on a mapping: &m key: value
 * The parser should parse the structure correctly (2 mapping starts).
 */
static void test_anchor_on_mapping(void)
{
    const char *input =
        "base: &base_map\n"
        "  a: 1\n"
        "  b: 2\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    event_list_t el;
    collect_events(ctx, &el);
    assert(el.count > 0);

    /* Verify no ERROR events */
    for (int i = 0; i < el.count; i++) {
        assert(el.events[i].type != YAML_EVENT_ERROR);
    }

    /* Should have 2 mapping starts (outer + anchored inner) */
    int map_starts = 0;
    int found_a = 0, found_b = 0;
    for (int i = 0; i < el.count; i++) {
        if (el.events[i].type == YAML_EVENT_MAPPING_START) map_starts++;
        if (el.events[i].type == YAML_EVENT_SCALAR) {
            if (strcmp(el.events[i].data.scalar.value, "a") == 0) found_a = 1;
            if (strcmp(el.events[i].data.scalar.value, "b") == 0) found_b = 1;
        }
    }
    assert(map_starts == 2);
    assert(found_a);
    assert(found_b);

    yaml_destroy(ctx);
    printf("  PASS: anchor on mapping\n");
}

/* Test 4: Round-trip — parse YAML with anchors → serialize → verify output */
static void test_anchor_round_trip(void)
{
    const char *input =
        "defaults: &d\n"
        "  timeout: 30\n"
        "service:\n"
        "  name: mysvc\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 64;

    /* Phase 1: Parse */
    yaml_ctx_t *parser = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(parser);
    yaml_feed_input(parser, (const uint8_t *)input, strlen(input));

    /* Phase 2: Serialize via merge_events */
    yaml_ctx_t *serializer = yaml_create(YAML_ROLE_SERIALIZER);
    assert(serializer);
    int merged = yaml_merge_events(serializer, parser);
    assert(merged > 0);

    /* Phase 3: Drain serialized output */
    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(serializer, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';

    /* Verify key content survived round-trip */
    assert(strstr((char *)buf, "defaults") != NULL);
    assert(strstr((char *)buf, "timeout") != NULL);
    assert(strstr((char *)buf, "service") != NULL);

    yaml_destroy(parser);
    yaml_destroy(serializer);
    printf("  PASS: anchor round-trip\n");
}

/* Test 5: Multiple anchors and aliases */
static void test_multiple_anchors_aliases(void)
{
    const char *input =
        "a1: &anchor1 val1\n"
        "a2: &anchor2 val2\n"
        "ref1: *anchor1\n"
        "ref2: *anchor2\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 64;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    event_list_t el;
    collect_events(ctx, &el);
    assert(el.count > 0);

    /* Count aliases */
    int alias_count = 0;
    int found_ref1 = 0, found_ref2 = 0;
    for (int i = 0; i < el.count; i++) {
        if (el.events[i].type == YAML_EVENT_ALIAS) {
            alias_count++;
            if (strcmp(el.events[i].data.alias.anchor, "anchor1") == 0)
                found_ref1 = 1;
            if (strcmp(el.events[i].data.alias.anchor, "anchor2") == 0)
                found_ref2 = 1;
        }
    }
    assert(alias_count >= 2);
    assert(found_ref1);
    assert(found_ref2);

    yaml_destroy(ctx);
    printf("  PASS: multiple anchors and aliases\n");
}

/* Test 6: Alias without preceding anchor (should still emit alias event) */
static void test_alias_without_anchor(void)
{
    const char *input = "ref: *nonexistent\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    event_list_t el;
    collect_events(ctx, &el);
    assert(el.count > 0);

    /* The parser should emit an alias event even if no matching anchor exists. */
    int found_alias = 0;
    for (int i = 0; i < el.count; i++) {
        if (el.events[i].type == YAML_EVENT_ALIAS) {
            if (strcmp(el.events[i].data.alias.anchor, "nonexistent") == 0) {
                found_alias = 1;
            }
        }
    }
    /* At minimum we should see the alias event */
    assert(found_alias);

    yaml_destroy(ctx);
    printf("  PASS: alias without anchor\n");
}

/* Test 7: Anchor on sequence */
static void test_anchor_on_sequence(void)
{
    const char *input =
        "list: &list_anchor\n"
        "  - one\n"
        "  - two\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 32;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    event_list_t el;
    collect_events(ctx, &el);
    assert(el.count > 0);

    /* Verify structure parses: 1 mapping, 1 sequence, 3 scalars (list, one, two) */
    int map_starts = 0, seq_starts = 0, scalars = 0;
    for (int i = 0; i < el.count; i++) {
        if (el.events[i].type == YAML_EVENT_MAPPING_START) map_starts++;
        if (el.events[i].type == YAML_EVENT_SEQUENCE_START) seq_starts++;
        if (el.events[i].type == YAML_EVENT_SCALAR) scalars++;
    }
    assert(map_starts == 1);
    assert(seq_starts == 1);
    assert(scalars == 3);

    yaml_destroy(ctx);
    printf("  PASS: anchor on sequence\n");
}

/* Test 8: Serialize with yaml_emit_alias */
static void test_serialize_alias(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    assert(ctx);

    yaml_emit_stream_start(ctx);
    yaml_emit_document_start(ctx, 1);
    yaml_emit_mapping_start(ctx, NULL, NULL);
    yaml_emit_scalar(ctx, "key", 3, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_alias(ctx, "myanchor");
    yaml_emit_mapping_end(ctx);
    yaml_emit_document_end(ctx, 1);
    yaml_emit_stream_end(ctx);

    /* Drain events */
    yaml_event_t ev;
    int event_count = 0;
    int alias_count = 0;
    while (yaml_next_event(ctx, &ev)) {
        event_count++;
        if (ev.type == YAML_EVENT_ALIAS) alias_count++;
    }
    assert(event_count == 8);
    assert(alias_count == 1);

    /* Drain output */
    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(ctx, buf + total, sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    assert(strstr((char *)buf, "myanchor") != NULL);

    yaml_destroy(ctx);
    printf("  PASS: serialize alias\n");
}

int main(void)
{
    printf("libyaml anchor/alias tests\n");
    test_anchor_on_scalar();
    test_alias_reference();
    test_anchor_on_mapping();
    test_anchor_round_trip();
    test_multiple_anchors_aliases();
    test_alias_without_anchor();
    test_anchor_on_sequence();
    test_serialize_alias();
    printf("All anchor/alias tests passed.\n");
    return 0;
}
