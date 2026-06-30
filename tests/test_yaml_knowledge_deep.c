/* test_yaml_knowledge_deep.c — Deep nested key-path lookup and knowledge API tests */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Helper: parse YAML and drain all events */
static void parse_and_drain(yaml_ctx_t *ctx, const char *input)
{
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}
}

static void test_deep_nested_lookup(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    size_t vlen = 0;
    const char *val;

    val = yaml_lookup_scalar(ctx, "server.host", &vlen);
    assert(val != NULL);
    assert(strcmp(val, "localhost") == 0);
    assert(vlen == 9);

    val = yaml_lookup_scalar(ctx, "server.port", &vlen);
    assert(val != NULL);
    assert(strcmp(val, "8080") == 0);
    assert(vlen == 4);

    /* Non-existent nested key */
    val = yaml_lookup_scalar(ctx, "server.missing", NULL);
    assert(val == NULL);

    yaml_destroy(ctx);
    printf("  PASS: deep nested lookup\n");
}

static void test_triple_nested_lookup(void)
{
    const char *input =
        "a:\n"
        "  b:\n"
        "    c: deep_value\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    size_t vlen = 0;
    const char *val;

    val = yaml_lookup_scalar(ctx, "a.b.c", &vlen);
    assert(val != NULL);
    assert(strcmp(val, "deep_value") == 0);
    assert(vlen == 10);

    yaml_destroy(ctx);
    printf("  PASS: triple nested lookup\n");
}

static void test_sibling_nested_keys(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n"
        "database:\n"
        "  name: mydb\n"
        "  user: admin\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    const char *val;

    val = yaml_lookup_scalar(ctx, "server.host", NULL);
    assert(val && strcmp(val, "localhost") == 0);

    val = yaml_lookup_scalar(ctx, "server.port", NULL);
    assert(val && strcmp(val, "8080") == 0);

    val = yaml_lookup_scalar(ctx, "database.name", NULL);
    assert(val && strcmp(val, "mydb") == 0);

    val = yaml_lookup_scalar(ctx, "database.user", NULL);
    assert(val && strcmp(val, "admin") == 0);

    yaml_destroy(ctx);
    printf("  PASS: sibling nested keys\n");
}

static void test_sequence_index_paths(void)
{
    const char *input =
        "items:\n"
        "  - name: first\n"
        "  - name: second\n"
        "  - name: third\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    const char *val;

    val = yaml_lookup_scalar(ctx, "items.0.name", NULL);
    assert(val != NULL);
    assert(strcmp(val, "first") == 0);

    val = yaml_lookup_scalar(ctx, "items.1.name", NULL);
    assert(val != NULL);
    assert(strcmp(val, "second") == 0);

    val = yaml_lookup_scalar(ctx, "items.2.name", NULL);
    assert(val != NULL);
    assert(strcmp(val, "third") == 0);

    yaml_destroy(ctx);
    printf("  PASS: sequence index paths\n");
}

static void test_sequence_with_multiple_keys(void)
{
    const char *input =
        "servers:\n"
        "  - name: web\n"
        "    port: 80\n"
        "  - name: db\n"
        "    port: 5432\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    const char *val;

    val = yaml_lookup_scalar(ctx, "servers.0.name", NULL);
    assert(val && strcmp(val, "web") == 0);

    val = yaml_lookup_scalar(ctx, "servers.0.port", NULL);
    assert(val && strcmp(val, "80") == 0);

    val = yaml_lookup_scalar(ctx, "servers.1.name", NULL);
    assert(val && strcmp(val, "db") == 0);

    val = yaml_lookup_scalar(ctx, "servers.1.port", NULL);
    assert(val && strcmp(val, "5432") == 0);

    yaml_destroy(ctx);
    printf("  PASS: sequence with multiple keys\n");
}

static void test_child_count_at_root(void)
{
    const char *input =
        "host: localhost\n"
        "port: 8080\n"
        "debug: true\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    /* Root children (empty path) */
    size_t count = yaml_get_child_count(ctx, "");
    assert(count == 3);

    yaml_destroy(ctx);
    printf("  PASS: child count at root\n");
}

static void test_child_count_nested(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    /* "server" should have 2 children: "host" and "port" */
    size_t count = yaml_get_child_count(ctx, "server");
    assert(count == 2);

    /* Non-existent path */
    count = yaml_get_child_count(ctx, "missing");
    assert(count == 0);

    yaml_destroy(ctx);
    printf("  PASS: child count nested\n");
}

static void test_child_key_iteration(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    size_t count = yaml_get_child_count(ctx, "server");
    assert(count == 2);

    const char *key = NULL;
    size_t key_len = 0;
    int rc;

    rc = yaml_get_child_key(ctx, "server", 0, &key, &key_len);
    assert(rc == 0);
    assert(key != NULL);
    assert(key_len > 0);

    /* Keys should be "host" and "port" in insertion order */
    int found_host = 0, found_port = 0;
    for (size_t i = 0; i < count; i++) {
        yaml_get_child_key(ctx, "server", i, &key, &key_len);
        if (key_len == 4 && memcmp(key, "host", 4) == 0) found_host = 1;
        if (key_len == 4 && memcmp(key, "port", 4) == 0) found_port = 1;
    }
    assert(found_host);
    assert(found_port);

    /* Out of range */
    rc = yaml_get_child_key(ctx, "server", 5, &key, &key_len);
    assert(rc == -1);

    /* Non-existent path */
    rc = yaml_get_child_key(ctx, "missing", 0, &key, &key_len);
    assert(rc == -1);

    yaml_destroy(ctx);
    printf("  PASS: child key iteration\n");
}

static void test_child_count_at_sequence_path(void)
{
    const char *input =
        "items:\n"
        "  - name: first\n"
        "    value: 42\n"
        "  - name: second\n"
        "    value: 99\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    /* "items" is a sequence with 2 items */
    size_t count = yaml_get_child_count(ctx, "items");
    assert(count == 2);

    /* First item has 2 keys */
    count = yaml_get_child_count(ctx, "items.0");
    assert(count == 2);

    /* Second item has 2 keys */
    count = yaml_get_child_count(ctx, "items.1");
    assert(count == 2);

    yaml_destroy(ctx);
    printf("  PASS: child count at sequence path\n");
}

static void test_knowledge_size(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    /* 2 key-value pairs: server.host and server.port */
    size_t size = yaml_knowledge_size(ctx);
    assert(size == 2);

    yaml_destroy(ctx);
    printf("  PASS: knowledge size\n");
}

static void test_knowledge_size_flat(void)
{
    const char *input = "a: 1\nb: 2\nc: 3\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    size_t size = yaml_knowledge_size(ctx);
    assert(size == 3);

    yaml_destroy(ctx);
    printf("  PASS: knowledge size flat\n");
}

static void test_knowledge_size_sequence(void)
{
    const char *input =
        "items:\n"
        "  - name: first\n"
        "  - name: second\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    size_t size = yaml_knowledge_size(ctx);
    assert(size == 2);

    yaml_destroy(ctx);
    printf("  PASS: knowledge size sequence\n");
}

static void test_knowledge_depth_flat(void)
{
    const char *input = "name: value\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    /* Depth 1 for flat key */
    size_t depth = yaml_knowledge_depth(ctx);
    assert(depth == 1);

    yaml_destroy(ctx);
    printf("  PASS: knowledge depth flat\n");
}

static void test_knowledge_depth_nested(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    /* Depth 2 for server.host */
    size_t depth = yaml_knowledge_depth(ctx);
    assert(depth == 2);

    yaml_destroy(ctx);
    printf("  PASS: knowledge depth nested\n");
}

static void test_knowledge_depth_triple_nested(void)
{
    const char *input =
        "a:\n"
        "  b:\n"
        "    c: value\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    size_t depth = yaml_knowledge_depth(ctx);
    assert(depth == 3);

    yaml_destroy(ctx);
    printf("  PASS: knowledge depth triple nested\n");
}

static void test_knowledge_after_reset(void)
{
    const char *input =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    /* Verify data exists */
    const char *val = yaml_lookup_scalar(ctx, "server.host", NULL);
    assert(val && strcmp(val, "localhost") == 0);
    assert(yaml_knowledge_size(ctx) == 2);
    assert(yaml_knowledge_depth(ctx) == 2);

    /* Reset */
    yaml_reset(ctx);

    /* Verify data is gone */
    val = yaml_lookup_scalar(ctx, "server.host", NULL);
    assert(val == NULL);
    val = yaml_lookup_scalar(ctx, "server.port", NULL);
    assert(val == NULL);
    assert(yaml_knowledge_size(ctx) == 0);
    assert(yaml_knowledge_depth(ctx) == 0);
    assert(yaml_get_child_count(ctx, "server") == 0);

    /* Feed new data after reset */
    const char *new_input = "key: new_value\n";
    parse_and_drain(ctx, new_input);
    val = yaml_lookup_scalar(ctx, "key", NULL);
    assert(val && strcmp(val, "new_value") == 0);

    yaml_destroy(ctx);
    printf("  PASS: knowledge after reset\n");
}

static void test_knowledge_mixed_flat_and_nested(void)
{
    const char *input =
        "name: top\n"
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n"
        "debug: true\n";

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);
    parse_and_drain(ctx, input);

    const char *val;

    /* Top-level keys still work */
    val = yaml_lookup_scalar(ctx, "name", NULL);
    assert(val && strcmp(val, "top") == 0);

    val = yaml_lookup_scalar(ctx, "debug", NULL);
    assert(val && strcmp(val, "true") == 0);

    /* Nested keys work */
    val = yaml_lookup_scalar(ctx, "server.host", NULL);
    assert(val && strcmp(val, "localhost") == 0);

    val = yaml_lookup_scalar(ctx, "server.port", NULL);
    assert(val && strcmp(val, "8080") == 0);

    /* Size: name, server.host, server.port, debug = 4 */
    assert(yaml_knowledge_size(ctx) == 4);

    /* Depth: 2 (server.host is deepest) */
    assert(yaml_knowledge_depth(ctx) == 2);

    yaml_destroy(ctx);
    printf("  PASS: knowledge mixed flat and nested\n");
}

int main(void)
{
    printf("libyaml deep knowledge tests\n");

    test_deep_nested_lookup();
    test_triple_nested_lookup();
    test_sibling_nested_keys();
    test_sequence_index_paths();
    test_sequence_with_multiple_keys();
    test_child_count_at_root();
    test_child_count_nested();
    test_child_key_iteration();
    test_child_count_at_sequence_path();
    test_knowledge_size();
    test_knowledge_size_flat();
    test_knowledge_size_sequence();
    test_knowledge_depth_flat();
    test_knowledge_depth_nested();
    test_knowledge_depth_triple_nested();
    test_knowledge_after_reset();
    test_knowledge_mixed_flat_and_nested();

    printf("All 17 deep knowledge tests passed.\n");
    return 0;
}
