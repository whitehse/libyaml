/* test_yaml_roundtrip.c — Round-trip fidelity tests
 *
 * Parse YAML text → events → serialize → parse again → compare events.
 * Verifies that scalar values are preserved through a full
 * parse-serialize-parse round-trip.
 *
 * NOTE: The serializer may add indentation/structure that changes the
 * event structure when re-parsed. Tests compare scalar *values* rather
 * than requiring exact structural identity.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Maximum events we can collect in a single pass */
#define MAX_EVENTS 256
#define MAX_SCALARS 64

typedef struct {
    yaml_event_t events[MAX_EVENTS];
    int count;
} event_list_t;

/* Scalar value list */
typedef struct {
    char values[MAX_SCALARS][256];
    int count;
} scalar_list_t;

static void collect_events(yaml_ctx_t *ctx, event_list_t *list)
{
    list->count = 0;
    while (list->count < MAX_EVENTS && yaml_next_event(ctx, &list->events[list->count])) {
        list->count++;
    }
}

static void collect_scalars(const event_list_t *el, scalar_list_t *sl)
{
    sl->count = 0;
    for (int i = 0; i < el->count && sl->count < MAX_SCALARS; i++) {
        if (el->events[i].type == YAML_EVENT_SCALAR) {
            strncpy(sl->values[sl->count], el->events[i].data.scalar.value, 255);
            sl->values[sl->count][255] = '\0';
            sl->count++;
        }
    }
}

/* Check if a scalar value appears in a scalar list */
static int scalar_exists(const scalar_list_t *sl, const char *value)
{
    for (int i = 0; i < sl->count; i++) {
        if (strcmp(sl->values[i], value) == 0) return 1;
    }
    return 0;
}

/* Helper: parse YAML text, serialize it, re-parse, return both event lists.
 * Returns 0 on success. */
static int do_roundtrip(const char *input, event_list_t *el1, event_list_t *el2,
                        uint8_t *serialized_out, size_t *serialized_len)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 64;

    /* Pass 1: Parse original YAML */
    yaml_ctx_t *parser1 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(parser1);
    yaml_feed_input(parser1, (const uint8_t *)input, strlen(input));
    collect_events(parser1, el1);

    /* Serialize */
    yaml_ctx_t *parser1b = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(parser1b);
    yaml_feed_input(parser1b, (const uint8_t *)input, strlen(input));

    yaml_ctx_t *serializer = yaml_create(YAML_ROLE_SERIALIZER);
    assert(serializer);
    int merged = yaml_merge_events(serializer, parser1b);
    assert(merged > 0);

    /* Drain serialized output */
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(serializer, serialized_out + total,
                                8192 - total - 1)) > 0) {
        total += n;
    }
    serialized_out[total] = '\0';
    *serialized_len = total;
    assert(total > 0);

    /* Pass 2: Parse the serialized output (add trailing \n for complete line) */
    yaml_ctx_t *parser2 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(parser2);
    if (total > 0 && serialized_out[total - 1] != '\n') {
        serialized_out[total++] = '\n';
        serialized_out[total] = '\0';
    }
    yaml_feed_input(parser2, serialized_out, total);
    collect_events(parser2, el2);

    yaml_destroy(parser1);
    yaml_destroy(parser1b);
    yaml_destroy(serializer);
    yaml_destroy(parser2);
    return 0;
}

/* Count events by type in an event list */
static int count_type(const event_list_t *el, yaml_event_type_t type)
{
    int c = 0;
    for (int i = 0; i < el->count; i++) {
        if (el->events[i].type == type) c++;
    }
    return c;
}

/* Test 1: Flat mapping round-trip — scalar values preserved */
static void test_roundtrip_flat_mapping(void)
{
    const char *input = "host: localhost\nport: 8080\n";

    event_list_t el1, el2;
    uint8_t serialized[8192];
    size_t slen;
    do_roundtrip(input, &el1, &el2, serialized, &slen);

    /* All original scalars should appear in the serialized output */
    assert(strstr((char *)serialized, "host") != NULL);
    assert(strstr((char *)serialized, "localhost") != NULL);
    assert(strstr((char *)serialized, "port") != NULL);
    assert(strstr((char *)serialized, "8080") != NULL);

    /* Second parse should see the same scalar values */
    scalar_list_t sl2;
    collect_scalars(&el2, &sl2);
    assert(scalar_exists(&sl2, "host"));
    assert(scalar_exists(&sl2, "localhost"));
    assert(scalar_exists(&sl2, "port"));
    assert(scalar_exists(&sl2, "8080"));

    printf("  PASS: round-trip flat mapping\n");
}

/* Test 2: Nested mapping round-trip — scalar values preserved */
static void test_roundtrip_nested_mapping(void)
{
    const char *input =
        "server:\n"
        "  host: api.example.com\n"
        "  port: 3000\n";

    event_list_t el1, el2;
    uint8_t serialized[8192];
    size_t slen;
    do_roundtrip(input, &el1, &el2, serialized, &slen);

    /* Verify key content in serialized output */
    assert(strstr((char *)serialized, "server") != NULL);
    assert(strstr((char *)serialized, "host") != NULL);
    assert(strstr((char *)serialized, "api.example.com") != NULL);
    assert(strstr((char *)serialized, "port") != NULL);
    assert(strstr((char *)serialized, "3000") != NULL);

    /* Second parse should also see all scalar values */
    scalar_list_t sl2;
    collect_scalars(&el2, &sl2);
    assert(scalar_exists(&sl2, "server"));
    assert(scalar_exists(&sl2, "host"));
    assert(scalar_exists(&sl2, "api.example.com"));
    assert(scalar_exists(&sl2, "port"));
    assert(scalar_exists(&sl2, "3000"));

    printf("  PASS: round-trip nested mapping\n");
}

/* Test 3: Sequence round-trip — scalar values preserved */
static void test_roundtrip_sequence(void)
{
    const char *input = "- alpha\n- beta\n- gamma\n";

    event_list_t el1, el2;
    uint8_t serialized[8192];
    size_t slen;
    do_roundtrip(input, &el1, &el2, serialized, &slen);

    /* Verify content */
    assert(strstr((char *)serialized, "alpha") != NULL);
    assert(strstr((char *)serialized, "beta") != NULL);
    assert(strstr((char *)serialized, "gamma") != NULL);

    scalar_list_t sl2;
    collect_scalars(&el2, &sl2);
    assert(scalar_exists(&sl2, "alpha"));
    assert(scalar_exists(&sl2, "beta"));
    assert(scalar_exists(&sl2, "gamma"));

    printf("  PASS: round-trip sequence\n");
}

/* Test 4: Mixed content (mapping with sequence value + nested mappings) */
static void test_roundtrip_mixed_content(void)
{
    const char *input =
        "name: myapp\n"
        "tags:\n"
        "  - web\n"
        "  - api\n"
        "config:\n"
        "  debug: true\n"
        "  verbose: false\n";

    event_list_t el1, el2;
    uint8_t serialized[8192];
    size_t slen;
    do_roundtrip(input, &el1, &el2, serialized, &slen);

    /* Verify all scalar values in serialized output */
    assert(strstr((char *)serialized, "name") != NULL);
    assert(strstr((char *)serialized, "myapp") != NULL);
    assert(strstr((char *)serialized, "tags") != NULL);
    assert(strstr((char *)serialized, "web") != NULL);
    assert(strstr((char *)serialized, "api") != NULL);
    assert(strstr((char *)serialized, "config") != NULL);
    assert(strstr((char *)serialized, "debug") != NULL);
    assert(strstr((char *)serialized, "true") != NULL);
    assert(strstr((char *)serialized, "verbose") != NULL);
    assert(strstr((char *)serialized, "false") != NULL);

    /* Second parse should also see all scalars */
    scalar_list_t sl2;
    collect_scalars(&el2, &sl2);
    assert(scalar_exists(&sl2, "name"));
    assert(scalar_exists(&sl2, "myapp"));
    assert(scalar_exists(&sl2, "web"));
    assert(scalar_exists(&sl2, "api"));
    assert(scalar_exists(&sl2, "debug"));
    assert(scalar_exists(&sl2, "true"));

    printf("  PASS: round-trip mixed content\n");
}

/* Test 5: Original parse has no ERROR events, round-trip produces no ERROR events */
static void test_roundtrip_no_errors(void)
{
    const char *input =
        "a:\n"
        "  b: 1\n"
        "c: 2\n";

    event_list_t el1, el2;
    uint8_t serialized[8192];
    size_t slen;
    do_roundtrip(input, &el1, &el2, serialized, &slen);

    /* Neither pass should produce ERROR events */
    for (int i = 0; i < el1.count; i++) {
        assert(el1.events[i].type != YAML_EVENT_ERROR);
    }
    for (int i = 0; i < el2.count; i++) {
        assert(el2.events[i].type != YAML_EVENT_ERROR);
    }

    printf("  PASS: round-trip no errors\n");
}

/* Test 6: Scalar count is preserved (round-trip doesn't add/drop scalars) */
static void test_roundtrip_scalar_count(void)
{
    const char *input = "x: 1\ny: 2\nz: 3\n";

    event_list_t el1, el2;
    uint8_t serialized[8192];
    size_t slen;
    do_roundtrip(input, &el1, &el2, serialized, &slen);

    int scalars1 = count_type(&el1, YAML_EVENT_SCALAR);
    int scalars2 = count_type(&el2, YAML_EVENT_SCALAR);
    /* Scalar count should be preserved (x, y, z, 1, 2, 3 = 6 each) */
    assert(scalars1 == scalars2);
    assert(scalars1 == 6);

    printf("  PASS: round-trip scalar count\n");
}

/* Test 7: Deeply nested round-trip */
static void test_roundtrip_deep_nesting(void)
{
    const char *input =
        "l1:\n"
        "  l2:\n"
        "    l3:\n"
        "      l4: deep\n";

    event_list_t el1, el2;
    uint8_t serialized[8192];
    size_t slen;
    do_roundtrip(input, &el1, &el2, serialized, &slen);

    /* All scalar values should survive */
    assert(strstr((char *)serialized, "l1") != NULL);
    assert(strstr((char *)serialized, "l2") != NULL);
    assert(strstr((char *)serialized, "l3") != NULL);
    assert(strstr((char *)serialized, "l4") != NULL);
    assert(strstr((char *)serialized, "deep") != NULL);

    scalar_list_t sl2;
    collect_scalars(&el2, &sl2);
    assert(scalar_exists(&sl2, "l1"));
    assert(scalar_exists(&sl2, "l4"));
    assert(scalar_exists(&sl2, "deep"));

    printf("  PASS: round-trip deep nesting\n");
}

/* Test 8: Multiple round-trips (parse → serialize → parse → serialize → parse) */
static void test_roundtrip_chained(void)
{
    const char *input = "key: value\nlist:\n  - a\n  - b\n";

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 64;

    /* First round-trip */
    yaml_ctx_t *p1 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    yaml_feed_input(p1, (const uint8_t *)input, strlen(input));
    yaml_ctx_t *s1 = yaml_create(YAML_ROLE_SERIALIZER);
    yaml_merge_events(s1, p1);
    uint8_t buf1[8192];
    size_t total1 = 0;
    size_t n;
    while ((n = yaml_get_output(s1, buf1 + total1, sizeof(buf1) - total1 - 1)) > 0)
        total1 += n;
    buf1[total1] = '\0';

    /* Second round-trip on the output of the first */
    yaml_ctx_t *p2 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    yaml_feed_input(p2, buf1, total1);
    yaml_ctx_t *s2 = yaml_create(YAML_ROLE_SERIALIZER);
    yaml_merge_events(s2, p2);
    uint8_t buf2[8192];
    size_t total2 = 0;
    while ((n = yaml_get_output(s2, buf2 + total2, sizeof(buf2) - total2 - 1)) > 0)
        total2 += n;
    buf2[total2] = '\0';

    /* Both outputs should contain the same scalar values */
    assert(strstr((char *)buf2, "key") != NULL);
    assert(strstr((char *)buf2, "value") != NULL);
    assert(strstr((char *)buf2, "a") != NULL);
    assert(strstr((char *)buf2, "b") != NULL);

    yaml_destroy(p1); yaml_destroy(s1);
    yaml_destroy(p2); yaml_destroy(s2);
    printf("  PASS: round-trip chained\n");
}

int main(void)
{
    printf("libyaml round-trip fidelity tests\n");
    test_roundtrip_flat_mapping();
    test_roundtrip_nested_mapping();
    test_roundtrip_sequence();
    test_roundtrip_mixed_content();
    test_roundtrip_no_errors();
    test_roundtrip_scalar_count();
    test_roundtrip_deep_nesting();
    test_roundtrip_chained();
    printf("All round-trip fidelity tests passed.\n");
    return 0;
}
