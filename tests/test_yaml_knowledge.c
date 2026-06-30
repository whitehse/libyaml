/* test_yaml_knowledge.c — Knowledge tree and scalar lookup tests */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_lookup_simple_key(void)
{
    const char *input = "name: myhost\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    /* Drain events to ensure parsing completed */
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    size_t vlen = 0;
    const char *val = yaml_lookup_scalar(ctx, "name", &vlen);
    assert(val != NULL);
    assert(vlen == 6);
    assert(strcmp(val, "myhost") == 0);

    yaml_destroy(ctx);
    printf("  PASS: simple key lookup\n");
}

static void test_lookup_multiple_keys(void)
{
    const char *input = "host: localhost\nport: 5432\ndb: testdb\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    size_t vlen = 0;
    const char *val;

    val = yaml_lookup_scalar(ctx, "host", &vlen);
    assert(val && strcmp(val, "localhost") == 0);

    val = yaml_lookup_scalar(ctx, "port", &vlen);
    assert(val && strcmp(val, "5432") == 0);

    val = yaml_lookup_scalar(ctx, "db", &vlen);
    assert(val && strcmp(val, "testdb") == 0);

    /* Non-existent key */
    val = yaml_lookup_scalar(ctx, "missing", NULL);
    assert(val == NULL);

    yaml_destroy(ctx);
    printf("  PASS: multiple keys lookup\n");
}

static void test_lookup_after_reset(void)
{
    const char *input = "key: value\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    const char *val = yaml_lookup_scalar(ctx, "key", NULL);
    assert(val && strcmp(val, "value") == 0);

    yaml_reset(ctx);

    val = yaml_lookup_scalar(ctx, "key", NULL);
    assert(val == NULL);

    yaml_destroy(ctx);
    printf("  PASS: lookup after reset\n");
}

static void test_sequence_values(void)
{
    const char *input = "- one\n- two\n- three\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int scalar_count = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) scalar_count++;
    }
    assert(scalar_count == 3);

    yaml_destroy(ctx);
    printf("  PASS: sequence values\n");
}

static void test_kv_in_sequence_context(void)
{
    /* Each sequence item is a mapping */
    const char *input =
        "items:\n"
        "  - name: first\n"
        "  - name: second\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int event_count = 0;
    while (yaml_next_event(ctx, &ev)) event_count++;
    assert(event_count > 0);

    yaml_destroy(ctx);
    printf("  PASS: kv in sequence context\n");
}

int main(void)
{
    printf("libyaml knowledge tests\n");
    test_lookup_simple_key();
    test_lookup_multiple_keys();
    test_lookup_after_reset();
    test_sequence_values();
    test_kv_in_sequence_context();
    printf("All knowledge tests passed.\n");
    return 0;
}
