# libyaml

A pure C, system-call-free, callback-free YAML parser and serializer state machine.

## Overview

`libyaml` is a thin "plumbing" library for YAML data. It parses YAML text from memory buffers into structured events, serializes events back to YAML text, and provides a knowledge tree for key-path scalar lookup. All I/O lives in the calling application.

## Features

- **Pure C11** — No C++ features, minimal dependencies
- **System-call free** — No file I/O, no sockets, no blocking
- **Callback free** — Progress is driven via `feed_input` / `next_event`
- **Event-driven** — Structured `yaml_event_t` events for all YAML constructs
- **Knowledge tree** — Automatic key-path scalar lookup built during parsing
- **Round-trip capable** — Parse → events → serialize produces valid YAML
- **Configurable** — `yaml_config_t` for queue size, document size, strict mode
- **Agent-ready** — Full documentation scaffold (AGENTS.md, ARCHITECTURE.md, ADRs)

## Building

```bash
cmake -B build -S . && cmake --build build
```

### Running Tests

```bash
ctest --test-dir build --output-on-failure
```

## Basic Usage (Parser)

```c
#include "yaml.h"

yaml_ctx_t *ctx = yaml_create(YAML_ROLE_PARSER);
yaml_feed_input(ctx, yaml_bytes, yaml_len);

yaml_event_t ev;
while (yaml_next_event(ctx, &ev)) {
    if (ev.type == YAML_EVENT_SCALAR) {
        printf("scalar: %.*s\n", (int)ev.data.scalar.value_len, ev.data.scalar.value);
    }
}

const char *val = yaml_lookup_scalar(ctx, "server.port", NULL);
yaml_destroy(ctx);
```

## Basic Usage (Serializer)

```c
yaml_ctx_t *ctx = yaml_create(YAML_ROLE_SERIALIZER);
yaml_emit_stream_start(ctx);
yaml_emit_document_start(ctx, 1);
yaml_emit_mapping_start(ctx, NULL, NULL);
yaml_serialize_kv(ctx, "key", "value", 5);
yaml_emit_mapping_end(ctx);
yaml_emit_document_end(ctx, 1);
yaml_emit_stream_end(ctx);

uint8_t buf[4096];
size_t n = yaml_get_output(ctx, buf, sizeof(buf));
yaml_destroy(ctx);
```

## Documentation

- `AGENTS.md` — Agent entry point and build commands
- `ARCHITECTURE.md` — Architecture, invariants, and design decisions
- `docs/decisions/` — Architecture Decision Records (ADRs)

## License

MIT License. See `LICENSE` for details.
