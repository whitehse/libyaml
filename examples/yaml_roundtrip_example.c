/* yaml_roundtrip_example.c — Parse YAML, serialize events, verify round-trip
 *
 * Demonstrates parse → events → serialize → verify workflow.
 * Pure C, no sockets.
 */

#include "yaml.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *yaml_input =
        "name: libyaml\n"
        "version: 0.1.0\n"
        "features:\n"
        "  - parsing\n"
        "  - serialization\n"
        "  - knowledge-tree\n"
        "config:\n"
        "  strict: true\n"
        "  max_size: 1048576\n";

    printf("=== YAML Round-Trip Example ===\n\n");
    printf("Input:\n%s\n", yaml_input);

    /* Step 1: Parse YAML into events */
    printf("--- Step 1: Parse YAML ---\n");
    yaml_ctx_t *parser = yaml_create(YAML_ROLE_PARSER);
    if (!parser) {
        fprintf(stderr, "Failed to create parser\n");
        return 1;
    }

    yaml_feed_input(parser, (const uint8_t *)yaml_input, strlen(yaml_input));

    /* Step 2: Merge events into serializer */
    printf("--- Step 2: Serialize Events ---\n");
    yaml_ctx_t *serializer = yaml_create(YAML_ROLE_SERIALIZER);
    if (!serializer) {
        fprintf(stderr, "Failed to create serializer\n");
        yaml_destroy(parser);
        return 1;
    }

    int merged = yaml_merge_events(serializer, parser);
    printf("Merged %d events from parser to serializer\n", merged);

    /* Step 3: Get serialized output */
    printf("--- Step 3: Get Output ---\n");
    uint8_t buf[4096];
    size_t total = 0;
    size_t n;
    while ((n = yaml_get_output(serializer, buf + total,
                                sizeof(buf) - total - 1)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    printf("Serialized output:\n%s\n", (char *)buf);

    /* Step 4: Verify round-trip by parsing the output again */
    printf("--- Step 4: Verify Round-Trip ---\n");
    yaml_ctx_t *parser2 = yaml_create(YAML_ROLE_PARSER);
    if (!parser2) {
        fprintf(stderr, "Failed to create second parser\n");
        yaml_destroy(parser);
        yaml_destroy(serializer);
        return 1;
    }

    yaml_feed_input(parser2, buf, total);

    /* Compare events from both parses */
    yaml_event_t ev1, ev2;
    int match = 1;
    int count1 = 0, count2 = 0;

    /* Count events from first parse */
    yaml_feed_input(parser, (const uint8_t *)yaml_input, strlen(yaml_input));
    while (yaml_next_event(parser, &ev1)) count1++;

    /* Count events from round-trip parse */
    while (yaml_next_event(parser2, &ev2)) count2++;

    printf("Original parse: %d events\n", count1);
    printf("Round-trip parse: %d events\n", count2);

    if (count1 == count2) {
        printf("✓ Event counts match!\n");
    } else {
        printf("✗ Event counts differ!\n");
        match = 0;
    }

    /* Verify knowledge tree survives round-trip */
    printf("\n--- Knowledge Tree Verification ---\n");
    size_t vlen;
    const char *val;

    val = yaml_lookup_scalar(parser2, "name", &vlen);
    printf("name = %s\n", val ? val : "(null)");

    val = yaml_lookup_scalar(parser2, "version", &vlen);
    printf("version = %s\n", val ? val : "(null)");

    val = yaml_lookup_scalar(parser2, "config.strict", &vlen);
    printf("config.strict = %s\n", val ? val : "(null)");

    if (match) {
        printf("\n✓ Round-trip verification passed!\n");
    } else {
        printf("\n✗ Round-trip verification failed!\n");
    }

    yaml_destroy(parser);
    yaml_destroy(serializer);
    yaml_destroy(parser2);

    return match ? 0 : 1;
}
