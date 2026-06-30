/* fuzz_yaml_serializer.c — libFuzzer harness for libyaml serializer
 *
 * Interprets input bytes as event type + data sequences and feeds them
 * to the serializer via yaml_emit_* functions. Drains output buffer.
 * Goal: find crashes in the serializer path.
 */

#include "yaml.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
    if (!ctx) return 0;

    size_t pos = 0;
    int depth = 0;  /* track nesting to emit matching end events */

    while (pos < size) {
        uint8_t op = data[pos++];

        switch (op % 12) {
        case 0:  /* stream start */
            yaml_emit_stream_start(ctx);
            depth++;
            break;

        case 1:  /* document start */
            yaml_emit_document_start(ctx, (pos < size) ? (data[pos++] & 1) : 1);
            depth++;
            break;

        case 2: { /* mapping start */
            const char *anchor = NULL;
            char anc_buf[8];
            if (pos < size && (data[pos] & 3) == 0) {
                /* occasionally emit an anchor */
                size_t anc_len = 0;
                while (pos < size && anc_len < sizeof(anc_buf) - 1) {
                    char c = (char)data[pos++];
                    if (c == 0) break;
                    anc_buf[anc_len++] = (c % 26) + 'a';
                }
                anc_buf[anc_len] = '\0';
                if (anc_len > 0) anchor = anc_buf;
            }
            yaml_emit_mapping_start(ctx, NULL, anchor);
            depth++;
            break;
        }

        case 3: { /* sequence start */
            const char *anchor = NULL;
            char anc_buf[8];
            if (pos < size && (data[pos] & 3) == 1) {
                size_t anc_len = 0;
                while (pos < size && anc_len < sizeof(anc_buf) - 1) {
                    char c = (char)data[pos++];
                    if (c == 0) break;
                    anc_buf[anc_len++] = (c % 26) + 'a';
                }
                anc_buf[anc_len] = '\0';
                if (anc_len > 0) anchor = anc_buf;
            }
            yaml_emit_sequence_start(ctx, NULL, anchor);
            depth++;
            break;
        }

        case 4: { /* scalar — generate value from remaining bytes */
            size_t val_len = 0;
            char val[64];
            while (pos < size && val_len < sizeof(val) - 1) {
                uint8_t c = data[pos++];
                if (c == 0) break;
                val[val_len++] = (char)((c % 94) + 33); /* printable ASCII */
            }
            val[val_len] = '\0';

            yaml_scalar_style_t style;
            if (pos < size) {
                style = (yaml_scalar_style_t)(data[pos++] % 5);
            } else {
                style = YAML_SCALAR_PLAIN;
            }

            yaml_emit_scalar(ctx, val, val_len, NULL, style);
            break;
        }

        case 5: { /* alias */
            char alias_buf[16];
            size_t alias_len = 0;
            while (pos < size && alias_len < sizeof(alias_buf) - 1) {
                char c = (char)data[pos++];
                if (c == 0) break;
                alias_buf[alias_len++] = (c % 26) + 'a';
            }
            alias_buf[alias_len] = '\0';
            if (alias_len > 0) {
                yaml_emit_alias(ctx, alias_buf);
            }
            break;
        }

        case 6:  /* mapping end */
            if (depth > 0) {
                yaml_emit_mapping_end(ctx);
                depth--;
            }
            break;

        case 7:  /* sequence end */
            if (depth > 0) {
                yaml_emit_sequence_end(ctx);
                depth--;
            }
            break;

        case 8:  /* document end */
            yaml_emit_document_end(ctx, (pos < size) ? (data[pos++] & 1) : 1);
            if (depth > 0) depth--;
            break;

        case 9:  /* stream end */
            yaml_emit_stream_end(ctx);
            if (depth > 0) depth--;
            break;

        case 10: { /* serialize_kv shorthand */
            char k[16], v[32];
            size_t klen = 0, vlen = 0;
            while (pos < size && klen < sizeof(k) - 1) {
                uint8_t c = data[pos++];
                if (c == 0) break;
                k[klen++] = (char)((c % 26) + 'a');
            }
            k[klen] = '\0';
            while (pos < size && vlen < sizeof(v) - 1) {
                uint8_t c = data[pos++];
                if (c == 0) break;
                v[vlen++] = (char)((c % 94) + 33);
            }
            v[vlen] = '\0';
            yaml_serialize_kv(ctx, k, v, vlen);
            break;
        }

        default:  /* case 11: no-op, just advance */
            break;
        }
    }

    /* Drain all events from the serializer */
    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {
        /* drain */
    }

    /* Drain output buffer — fuzzer only cares about crashes, not content */
    uint8_t out_buf[4096];
    while (yaml_get_output(ctx, out_buf, sizeof(out_buf)) > 0) {
        /* discard */
    }

    yaml_destroy(ctx);
    return 0;
}
