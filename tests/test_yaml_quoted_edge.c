/* test_yaml_quoted_edge.c — Edge cases in quoted scalar parsing
 *
 * Tests empty quoted strings, escaped quotes, newlines, tabs,
 * trailing backslashes, and special YAML characters inside quotes.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Helper: parse YAML, find a scalar by key in a simple key: value mapping.
 * Returns 1 if found and copies value into out_val (up to out_max).
 * Also optionally returns value_len. */
static int find_value_for_key(yaml_ctx_t *ctx, const char *key,
                              char *out_val, size_t out_max,
                              size_t *out_len)
{
    yaml_event_t ev;
    int found_key = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_SCALAR) {
            if (!found_key && strcmp(ev.data.scalar.value, key) == 0) {
                found_key = 1;
                continue;
            }
            if (found_key) {
                /* This is the value scalar */
                size_t copy_len = ev.data.scalar.value_len;
                if (copy_len >= out_max) copy_len = out_max - 1;
                memcpy(out_val, ev.data.scalar.value, copy_len);
                out_val[copy_len] = '\0';
                if (out_len) *out_len = ev.data.scalar.value_len;
                return 1;
            }
        }
    }
    return 0;
}

/* Test 1: Empty double-quoted string: key: "" */
static void test_empty_double_quoted(void)
{
    const char *input = "key: \"\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    size_t vlen = 0;
    int found = find_value_for_key(ctx, "key", val, sizeof(val), &vlen);
    assert(found);
    assert(vlen == 0);
    assert(strlen(val) == 0);

    yaml_destroy(ctx);
    printf("  PASS: empty double-quoted string\n");
}

/* Test 2: Empty single-quoted string: key: '' */
static void test_empty_single_quoted(void)
{
    const char *input = "key: ''\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    size_t vlen = 0;
    int found = find_value_for_key(ctx, "key", val, sizeof(val), &vlen);
    assert(found);
    assert(vlen == 0);
    assert(strlen(val) == 0);

    yaml_destroy(ctx);
    printf("  PASS: empty single-quoted string\n");
}

/* Test 3: Escaped quotes within double-quoted string: key: "say \"hello\"" */
static void test_escaped_quotes_in_double_quoted(void)
{
    const char *input = "key: \"say \\\"hello\\\"\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    int found = find_value_for_key(ctx, "key", val, sizeof(val), NULL);
    assert(found);
    /* After unescaping, should be: say "hello" */
    assert(strcmp(val, "say \"hello\"") == 0);

    yaml_destroy(ctx);
    printf("  PASS: escaped quotes in double-quoted\n");
}

/* Test 4: Newlines in quoted strings: key: "line1\nline2" */
static void test_newline_in_quoted(void)
{
    /* In YAML double-quoted strings, \n becomes a literal newline */
    const char *input = "key: \"line1\\nline2\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    int found = find_value_for_key(ctx, "key", val, sizeof(val), NULL);
    assert(found);
    /* The \n escape should be decoded to a real newline (0x0a) */
    assert(strlen(val) > 0);
    /* Check that a newline character is present */
    int has_newline = 0;
    for (size_t i = 0; i < strlen(val); i++) {
        if (val[i] == '\n') has_newline = 1;
    }
    assert(has_newline);

    yaml_destroy(ctx);
    printf("  PASS: newline in quoted string\n");
}

/* Test 5: Tab in quoted string: key: "col1\tcol2" */
static void test_tab_in_quoted(void)
{
    const char *input = "key: \"col1\\tcol2\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    int found = find_value_for_key(ctx, "key", val, sizeof(val), NULL);
    assert(found);
    /* The \t escape should be decoded to a real tab (0x09) */
    int has_tab = 0;
    for (size_t i = 0; i < strlen(val); i++) {
        if (val[i] == '\t') has_tab = 1;
    }
    assert(has_tab);

    yaml_destroy(ctx);
    printf("  PASS: tab in quoted string\n");
}

/* Test 6: Backslash at end: key: "path\\" */
static void test_backslash_at_end(void)
{
    const char *input = "key: \"path\\\\\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    int found = find_value_for_key(ctx, "key", val, sizeof(val), NULL);
    assert(found);
    /* \\ at end of double-quoted → single trailing backslash */
    assert(strlen(val) >= 4);
    assert(val[strlen(val) - 1] == '\\');
    assert(strncmp(val, "path", 4) == 0);

    yaml_destroy(ctx);
    printf("  PASS: backslash at end\n");
}

/* Test 7: Special YAML chars in quotes: key: "a: b # c [d] {e}" */
static void test_special_yaml_chars_in_quotes(void)
{
    const char *input = "key: \"a: b # c [d] {e}\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    int found = find_value_for_key(ctx, "key", val, sizeof(val), NULL);
    assert(found);
    /* All special chars should be preserved literally */
    assert(strcmp(val, "a: b # c [d] {e}") == 0);

    yaml_destroy(ctx);
    printf("  PASS: special YAML chars in quotes\n");
}

/* Test 8: Single-quoted preserves content literally (no escape processing
 * except '' → '). Current parser reads content between quotes. */
static void test_single_quoted_literal(void)
{
    /* A single-quoted string preserves everything between the quotes */
    const char *input = "key: 'hello world'\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    int found = find_value_for_key(ctx, "key", val, sizeof(val), NULL);
    assert(found);
    assert(strcmp(val, "hello world") == 0);

    yaml_destroy(ctx);
    printf("  PASS: single-quoted literal content\n");
}

/* Test 9: Double-quoted with backslash-n produces newline */
static void test_backslash_n_produces_newline(void)
{
    const char *input = "key: \"abc\\ndef\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    size_t vlen = 0;
    int found = find_value_for_key(ctx, "key", val, sizeof(val), &vlen);
    assert(found);
    /* Should be 7 bytes: a b c \n d e f */
    assert(vlen == 7);
    assert(val[3] == '\n');
    assert(val[0] == 'a');
    assert(val[6] == 'f');

    yaml_destroy(ctx);
    printf("  PASS: backslash-n produces newline\n");
}

/* Test 10: Multiple escape sequences in one string */
static void test_multiple_escapes(void)
{
    const char *input = "key: \"tab\\there, newline\\nthere\"\n";
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx);

    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    char val[256];
    int found = find_value_for_key(ctx, "key", val, sizeof(val), NULL);
    assert(found);

    int has_tab = 0, has_newline = 0;
    for (size_t i = 0; i < strlen(val); i++) {
        if (val[i] == '\t') has_tab = 1;
        if (val[i] == '\n') has_newline = 1;
    }
    assert(has_tab);
    assert(has_newline);

    yaml_destroy(ctx);
    printf("  PASS: multiple escapes\n");
}

int main(void)
{
    printf("libyaml quoted scalar edge-case tests\n");
    test_empty_double_quoted();
    test_empty_single_quoted();
    test_escaped_quotes_in_double_quoted();
    test_newline_in_quoted();
    test_tab_in_quoted();
    test_backslash_at_end();
    test_special_yaml_chars_in_quotes();
    test_single_quoted_literal();
    test_backslash_n_produces_newline();
    test_multiple_escapes();
    printf("All quoted scalar edge-case tests passed.\n");
    return 0;
}
