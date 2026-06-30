/* yaml_parse_example.c — Demonstrates parsing YAML and querying knowledge tree
 *
 * Pure C, no sockets. Shows the event-driven parsing workflow
 * with deep nested key-path lookup.
 */

#include "yaml.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *yaml_text =
        "server:\n"
        "  host: localhost\n"
        "  port: 8080\n"
        "database:\n"
        "  name: mydb\n"
        "  pool_size: 10\n"
        "features:\n"
        "  - auth\n"
        "  - logging\n"
        "  - metrics\n";

    /* Create parser */
    yaml_ctx_t *parser = yaml_create(YAML_ROLE_PARSER);
    if (!parser) {
        fprintf(stderr, "Failed to create parser\n");
        return 1;
    }

    /* Feed YAML text */
    size_t consumed = yaml_feed_input(parser,
        (const uint8_t *)yaml_text, strlen(yaml_text));
    printf("Consumed %zu bytes of YAML input\n", consumed);

    /* Process events */
    yaml_event_t ev;
    int event_count = 0;
    while (yaml_next_event(parser, &ev)) {
        event_count++;
        switch (ev.type) {
            case YAML_EVENT_STREAM_START:
                printf("[EVENT] Stream start\n");
                break;
            case YAML_EVENT_STREAM_END:
                printf("[EVENT] Stream end\n");
                break;
            case YAML_EVENT_DOCUMENT_START:
                printf("[EVENT] Document start (implicit=%d)\n",
                       ev.data.document_start.implicit);
                break;
            case YAML_EVENT_DOCUMENT_END:
                printf("[EVENT] Document end\n");
                break;
            case YAML_EVENT_MAPPING_START:
                printf("[EVENT] Mapping start\n");
                break;
            case YAML_EVENT_MAPPING_END:
                printf("[EVENT] Mapping end\n");
                break;
            case YAML_EVENT_SEQUENCE_START:
                printf("[EVENT] Sequence start\n");
                break;
            case YAML_EVENT_SEQUENCE_END:
                printf("[EVENT] Sequence end\n");
                break;
            case YAML_EVENT_SCALAR:
                printf("[EVENT] Scalar: \"%.*s\" (len=%zu)\n",
                       (int)ev.data.scalar.value_len,
                       ev.data.scalar.value,
                       ev.data.scalar.value_len);
                break;
            default:
                printf("[EVENT] Type %d\n", ev.type);
                break;
        }
    }
    printf("\nTotal events: %d\n", event_count);

    /* Knowledge tree lookup — deep nested key-path */
    printf("\n--- Knowledge Tree Lookups ---\n");
    size_t vlen;
    const char *val;

    /* Top-level keys */
    val = yaml_lookup_scalar(parser, "server", &vlen);
    printf("server = %s (len=%zu)\n", val ? val : "(null)", vlen);

    val = yaml_lookup_scalar(parser, "database", &vlen);
    printf("database = %s (len=%zu)\n", val ? val : "(null)", vlen);

    /* Deep nested key-path lookup (server.host, server.port) */
    val = yaml_lookup_scalar(parser, "server.host", &vlen);
    printf("server.host = %s (len=%zu)\n", val ? val : "(null)", vlen);

    val = yaml_lookup_scalar(parser, "server.port", &vlen);
    printf("server.port = %s (len=%zu)\n", val ? val : "(null)", vlen);

    val = yaml_lookup_scalar(parser, "database.name", &vlen);
    printf("database.name = %s (len=%zu)\n", val ? val : "(null)", vlen);

    val = yaml_lookup_scalar(parser, "database.pool_size", &vlen);
    printf("database.pool_size = %s (len=%zu)\n", val ? val : "(null)", vlen);

    /* Sequence index paths */
    val = yaml_lookup_scalar(parser, "features.0", &vlen);
    printf("features.0 = %s (len=%zu)\n", val ? val : "(null)", vlen);

    val = yaml_lookup_scalar(parser, "features.1", &vlen);
    printf("features.1 = %s (len=%zu)\n", val ? val : "(null)", vlen);

    /* Knowledge tree enumeration */
    printf("\n--- Knowledge Tree Enumeration ---\n");
    size_t child_count = yaml_get_child_count(parser, "");
    printf("Root children: %zu\n", child_count);

    for (size_t i = 0; i < child_count; i++) {
        const char *key;
        size_t key_len;
        if (yaml_get_child_key(parser, "", i, &key, &key_len) == 0) {
            printf("  Child %zu: %.*s\n", i, (int)key_len, key);
        }
    }

    /* Knowledge tree statistics */
    printf("\n--- Knowledge Tree Statistics ---\n");
    printf("Total key-value pairs: %zu\n", yaml_knowledge_size(parser));
    printf("Tree depth: %zu\n", yaml_knowledge_depth(parser));

    yaml_destroy(parser);
    return 0;
}
