/* fuzz_yaml.c — libFuzzer harness for libyaml parser */

#include "yaml.h"
#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
    if (!ctx) return 0;

    yaml_feed_input(ctx, data, size);

    yaml_event_t ev;
    while (yaml_next_event(ctx, &ev)) {
        /* Drain all events; fuzzer checks for crashes and memory errors */
    }

    /* Also test knowledge lookup on whatever was parsed */
    yaml_lookup_scalar(ctx, "test", NULL);

    yaml_destroy(ctx);
    return 0;
}
