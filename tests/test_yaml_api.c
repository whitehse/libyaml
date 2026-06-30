/* test_yaml_api.c — Tests for API query functions, error events, and strict mode
 *
 * Tests: yaml_parser_state, yaml_depth, yaml_has_pending_events,
 *        yaml_event_count, yaml_output_pending, yaml_queue_available,
 *        yaml_queue_capacity, error events for malformed input,
 *        strict mode tag validation.
 */

#include "yaml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_parser_state(void)
{
    /* NULL ctx */
    assert(yaml_parser_state(NULL) == NULL);

    /* Serializer should return NULL */
    yaml_ctx_t *ser = yaml_create(YAML_ROLE_SERIALIZER);
    assert(yaml_parser_state(ser) == NULL);
    yaml_destroy(ser);

    /* Parser starts in IDLE */
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx != NULL);
    const char *s = yaml_parser_state(ctx);
    assert(s != NULL);
    assert(strcmp(s, "IDLE") == 0);

    /* Feed input -> transitions to DOCUMENT state */
    const char *input = "key: value\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));
    /* After feeding and parsing, state should be DONE (input fully consumed) */
    s = yaml_parser_state(ctx);
    assert(s != NULL);
    /* State is DONE after implicit document end */
    assert(strcmp(s, "DONE") == 0);

    yaml_destroy(ctx);
    printf("  PASS: yaml_parser_state\n");
}

static void test_depth(void)
{
    /* NULL ctx */
    assert(yaml_depth(NULL) == -1);

    /* Parser: depth should be 0 at top level before parsing */
    yaml_ctx_t *p = yaml_create(YAML_ROLE_PARSER);
    assert(yaml_depth(p) == 0);
    yaml_destroy(p);

    /* Serializer: depth starts at 0 */
    yaml_ctx_t *s = yaml_create(YAML_ROLE_SERIALIZER);
    assert(yaml_depth(s) == 0);

    yaml_emit_stream_start(s);
    yaml_emit_document_start(s, 1);
    assert(yaml_depth(s) == 0);

    yaml_emit_mapping_start(s, NULL, NULL);
    assert(yaml_depth(s) == 1);

    yaml_emit_sequence_start(s, NULL, NULL);
    assert(yaml_depth(s) == 2);

    yaml_emit_sequence_end(s);
    assert(yaml_depth(s) == 1);

    yaml_emit_mapping_end(s);
    assert(yaml_depth(s) == 0);

    yaml_emit_document_end(s, 1);
    yaml_emit_stream_end(s);

    yaml_destroy(s);
    printf("  PASS: yaml_depth\n");
}

static void test_pending_events(void)
{
    /* NULL handling */
    assert(yaml_has_pending_events(NULL) == 0);
    assert(yaml_event_count(NULL) == 0);

    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(yaml_has_pending_events(ctx) == 0);
    assert(yaml_event_count(ctx) == 0);

    /* Feed simple input to generate events */
    const char *input = "a: 1\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    /* Should have pending events now */
    assert(yaml_has_pending_events(ctx) == 1);
    size_t count = yaml_event_count(ctx);
    assert(count > 0);

    /* Drain events */
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {}

    assert(yaml_has_pending_events(ctx) == 0);
    assert(yaml_event_count(ctx) == 0);

    yaml_destroy(ctx);
    printf("  PASS: yaml_has_pending_events / yaml_event_count\n");
}

static void test_output_pending(void)
{
    /* NULL handling */
    assert(yaml_output_pending(NULL) == 0);
    assert(yaml_output_size(NULL) == 0);

    /* Parser has no output */
    yaml_ctx_t *p = yaml_create(YAML_ROLE_PARSER);
    assert(yaml_output_pending(p) == 0);
    yaml_destroy(p);

    /* Serializer accumulates output */
    yaml_ctx_t *s = yaml_create(YAML_ROLE_SERIALIZER);
    assert(yaml_output_pending(s) == 0);

    yaml_emit_stream_start(s);
    yaml_emit_document_start(s, 1);
    yaml_emit_mapping_start(s, NULL, NULL);
    yaml_emit_scalar(s, "key", 3, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_scalar(s, "value", 5, NULL, YAML_SCALAR_PLAIN);
    yaml_emit_mapping_end(s);
    yaml_emit_document_end(s, 1);
    yaml_emit_stream_end(s);

    size_t pending = yaml_output_pending(s);
    assert(pending > 0);
    assert(yaml_output_size(s) == pending);

    /* Drain output */
    uint8_t buf[4096];
    yaml_get_output(s, buf, sizeof(buf));
    assert(yaml_output_pending(s) == 0);

    yaml_destroy(s);
    printf("  PASS: yaml_output_pending / yaml_output_size\n");
}

static void test_queue_status(void)
{
    /* NULL handling */
    assert(yaml_queue_available(NULL) == -1);
    assert(yaml_queue_capacity(NULL) == 0);

    /* Default queue capacity is 8 */
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(yaml_queue_capacity(ctx) == 8);
    assert(yaml_queue_available(ctx) == 8);

    /* Feed input to fill some slots */
    const char *input = "a: 1\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    size_t ev_count = yaml_event_count(ctx);
    assert(ev_count > 0);
    assert(yaml_queue_available(ctx) == (int)(8 - ev_count));

    /* Custom queue size */
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 16;
    yaml_ctx_t *ctx2 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(yaml_queue_capacity(ctx2) == 16);
    assert(yaml_queue_available(ctx2) == 16);

    yaml_destroy(ctx);
    yaml_destroy(ctx2);
    printf("  PASS: yaml_queue_available / yaml_queue_capacity\n");
}

static void test_unclosed_quote_error(void)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    assert(ctx != NULL);

    /* Unclosed double quote */
    const char *input = "key: \"unclosed\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    /* Should find an error event */
    yaml_event_t ev;
    int found_error = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_ERROR) {
            found_error = 1;
            assert(ev.data.error.message_len > 0);
            assert(strstr(ev.data.error.message, "unclosed") != NULL);
        }
    }
    assert(found_error);

    yaml_destroy(ctx);
    printf("  PASS: unclosed quote error event\n");
}

static void test_strict_mode_tab_error(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.strict_mode = 1;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    assert(ctx != NULL);

    /* Tab indentation */
    const char *input = "\tkey: value\n";
    yaml_feed_input(ctx, (const uint8_t *)input, strlen(input));

    yaml_event_t ev;
    int found_error = 0;
    while (yaml_next_event(ctx, &ev)) {
        if (ev.type == YAML_EVENT_ERROR) {
            found_error = 1;
            assert(strstr(ev.data.error.message, "tab") != NULL);
        }
    }
    assert(found_error);

    yaml_destroy(ctx);
    printf("  PASS: strict mode tab indentation error\n");
}

static void test_strict_mode_tag_validation(void)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.strict_mode = 1;

    /* Standard tag !str should pass */
    yaml_ctx_t *ctx1 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    const char *input1 = "!str key: value\n";
    yaml_feed_input(ctx1, (const uint8_t *)input1, strlen(input1));
    yaml_event_t ev;
    int found_error1 = 0;
    while (yaml_next_event(ctx1, &ev)) {
        if (ev.type == YAML_EVENT_ERROR) found_error1 = 1;
    }
    assert(found_error1 == 0);
    yaml_destroy(ctx1);

    /* Standard tag !!int should pass */
    yaml_ctx_t *ctx2 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    const char *input2 = "key: !!int 42\n";
    yaml_feed_input(ctx2, (const uint8_t *)input2, strlen(input2));
    int found_error2 = 0;
    while (yaml_next_event(ctx2, &ev)) {
        if (ev.type == YAML_EVENT_ERROR) found_error2 = 1;
    }
    assert(found_error2 == 0);
    yaml_destroy(ctx2);

    /* Bare ! tag should pass */
    yaml_ctx_t *ctx3 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    const char *input3 = "! key: value\n";
    yaml_feed_input(ctx3, (const uint8_t *)input3, strlen(input3));
    int found_error3 = 0;
    while (yaml_next_event(ctx3, &ev)) {
        if (ev.type == YAML_EVENT_ERROR) found_error3 = 1;
    }
    assert(found_error3 == 0);
    yaml_destroy(ctx3);

    /* Non-standard tag like !@# should fail */
    yaml_ctx_t *ctx4 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    const char *input4 = "!@# key: value\n";
    yaml_feed_input(ctx4, (const uint8_t *)input4, strlen(input4));
    int found_error4 = 0;
    while (yaml_next_event(ctx4, &ev)) {
        if (ev.type == YAML_EVENT_ERROR) {
            found_error4 = 1;
            assert(strstr(ev.data.error.message, "tag") != NULL);
        }
    }
    assert(found_error4);
    yaml_destroy(ctx4);

    /* Verbatim tag !<uri> should pass */
    yaml_ctx_t *ctx5 = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    const char *input5 = "!<http://example.com> key: value\n";
    yaml_feed_input(ctx5, (const uint8_t *)input5, strlen(input5));
    int found_error5 = 0;
    while (yaml_next_event(ctx5, &ev)) {
        if (ev.type == YAML_EVENT_ERROR) found_error5 = 1;
    }
    assert(found_error5 == 0);
    yaml_destroy(ctx5);

    printf("  PASS: strict mode tag validation\n");
}

int main(void)
{
    printf("libyaml API tests\n");
    test_parser_state();
    test_depth();
    test_pending_events();
    test_output_pending();
    test_queue_status();
    test_unclosed_quote_error();
    test_strict_mode_tab_error();
    test_strict_mode_tag_validation();
    printf("All API tests passed.\n");
    return 0;
}
