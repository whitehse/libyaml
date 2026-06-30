/* fuzz_yaml_knowledge.c — libFuzzer harness for knowledge-tree / scalar lookup
 *
 * Feeds random YAML text to the parser, then exercises yaml_lookup_scalar
 * with random key paths. Also tests yaml_reset and re-parse, and deep
 * nesting scenarios.
 */

#include "yaml.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Generate a printable ASCII key from fuzz data.
 * Returns number of bytes consumed from data. */
static size_t make_key(const uint8_t *data, size_t size, size_t pos,
                       char *out, size_t out_max)
{
    size_t klen = 0;
    while (pos < size && klen < out_max - 1) {
        uint8_t c = data[pos];
        if (c == 0) break;
        /* Generate chars a-z, 0-9, dot */
        if (c < 26)
            out[klen++] = 'a' + c;
        else if (c < 36)
            out[klen++] = '0' + (c - 26);
        else if (c == 36)
            out[klen++] = '.';
        else
            break;
        pos++;
    }
    out[klen] = '\0';
    return klen > 0 ? (pos - (size - 0)) : 0;  /* bytes consumed */
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) return 0;

    /* Use first byte to choose test variant */
    uint8_t variant = data[0];
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;

    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = 64;

    yaml_ctx_t *ctx = yaml_create_with_config(YAML_ROLE_PARSER, &cfg);
    if (!ctx) return 0;

    /*
     * Variant 0-3: Feed raw fuzz bytes as YAML text, lookup with random keys
     * Variant 4-5: Feed raw fuzz bytes, reset, re-parse, lookup again
     * Variant 6-7: Build a deep nesting string then parse
     */
    switch (variant & 7) {
    case 0: case 1: case 2: case 3: {
        /* Feed raw bytes as YAML input */
        yaml_feed_input(ctx, payload, payload_size);

        /* Drain all events */
        yaml_event_t ev;
        while (yaml_next_event(ctx, &ev)) {}

        /* Try a few random key lookups */
        for (int i = 0; i < 4 && i < (int)payload_size; i++) {
            char key[32];
            size_t klen = 0;
            size_t p = (size_t)i;
            while (p < payload_size && klen < sizeof(key) - 1) {
                uint8_t c = payload[p++];
                if (c == 0 || c > 127) break;
                key[klen++] = (c % 36 < 26) ? ('a' + c % 36) : ('0' + c % 36 - 26);
            }
            key[klen] = '\0';
            if (klen > 0) {
                yaml_lookup_scalar(ctx, key, NULL);
            }
        }

        /* Also try dot-separated path lookup */
        if (payload_size > 2) {
            char path[64];
            size_t plen = 0;
            for (size_t p = 0; p < payload_size && plen < sizeof(path) - 2; p++) {
                uint8_t c = payload[p];
                if (c == 0) break;
                if (c & 1)
                    path[plen++] = '.';
                else
                    path[plen++] = (char)((c % 26) + 'a');
            }
            path[plen] = '\0';
            if (plen > 0) {
                yaml_lookup_scalar(ctx, path, NULL);
            }
        }
        break;
    }

    case 4: case 5: {
        /* Parse, reset, re-parse */
        yaml_feed_input(ctx, payload, payload_size);

        yaml_event_t ev;
        while (yaml_next_event(ctx, &ev)) {}

        /* Lookup before reset */
        yaml_lookup_scalar(ctx, "x", NULL);
        yaml_lookup_scalar(ctx, "y", NULL);

        /* Reset and re-parse */
        yaml_reset(ctx);

        yaml_feed_input(ctx, payload, payload_size);
        while (yaml_next_event(ctx, &ev)) {}

        /* Lookup after re-parse */
        yaml_lookup_scalar(ctx, "x", NULL);
        yaml_lookup_scalar(ctx, "y", NULL);
        break;
    }

    case 6: case 7: {
        /* Build a deeply nested YAML structure from fuzz data */
        char deep_buf[512];
        size_t dlen = 0;
        int max_depth = (payload_size > 0) ? (payload[0] % 10) + 1 : 3;

        for (int i = 0; i < max_depth && dlen < sizeof(deep_buf) - 32; i++) {
            /* Add indentation */
            for (int j = 0; j < i && dlen < sizeof(deep_buf) - 20; j++) {
                deep_buf[dlen++] = ' ';
                deep_buf[dlen++] = ' ';
            }
            /* Add key */
            deep_buf[dlen++] = 'k';
            deep_buf[dlen++] = '0' + (i % 10);
            deep_buf[dlen++] = ':';

            if (i < max_depth - 1) {
                /* Value is the next mapping */
                deep_buf[dlen++] = '\n';
            } else {
                /* Leaf value */
                deep_buf[dlen++] = ' ';
                deep_buf[dlen++] = 'v';
                deep_buf[dlen++] = '\n';
            }
        }
        deep_buf[dlen] = '\0';

        yaml_feed_input(ctx, (const uint8_t *)deep_buf, dlen);

        yaml_event_t ev;
        int map_starts = 0, map_ends = 0, scalars = 0;
        while (yaml_next_event(ctx, &ev)) {
            if (ev.type == YAML_EVENT_MAPPING_START) map_starts++;
            if (ev.type == YAML_EVENT_MAPPING_END) map_ends++;
            if (ev.type == YAML_EVENT_SCALAR) scalars++;
        }

        /* Try lookup on the root key */
        yaml_lookup_scalar(ctx, "k0", NULL);

        /* Try dot-path to deepest key */
        if (max_depth >= 2) {
            /* Build path like "k0.k1" etc. */
            char dotpath[128];
            size_t dplen = 0;
            for (int i = 0; i < max_depth - 1 && dplen < sizeof(dotpath) - 4; i++) {
                if (i > 0) dotpath[dplen++] = '.';
                dotpath[dplen++] = 'k';
                dotpath[dplen++] = '0' + (i % 10);
            }
            dotpath[dplen] = '\0';
            yaml_lookup_scalar(ctx, dotpath, NULL);
        }
        break;
    }
    }

    yaml_destroy(ctx);
    return 0;
}
