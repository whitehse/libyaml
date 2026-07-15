# ARCHITECTURE.md — libyaml

## Core Principle

This is a **pure state machine** YAML parser and serializer. No function pointers for callbacks, no direct system calls, no hidden allocations beyond what's explicitly requested via config. The design follows the "plumbing" philosophy (ADR 006): the library is a thin YAML tokenizer/parser/serializer that emits structured events and unencapsulated data; the calling application owns all policy, decisions, and side effects. Data is passed via memory buffers; the library has no knowledge of files, sockets, or any I/O mechanism.

## Module Boundaries

- `include/yaml.h` — Public API. Opaque context, event types, config struct, parser/serializer/knowledge functions.
- `src/yaml.c` — Core YAML state machine implementation. Event queue, output buffer, input accumulator, knowledge tree, tokenizer, and all API functions.
- All modules follow identical patterns: `*_create` / `*_create_with_config`, `*_feed_input`, `*_next_event`, `*_get_output`, `*_destroy`, `*_reset`.

## Configuration and Initialization (ADR 006 / User Preference)

- Libraries use config structs at creation time for configurability (e.g., `yaml_config_t { size_t event_queue_size; size_t max_document_size; int strict_mode; }`).
- Default constructors (`yaml_create`) use sensible defaults (queue size 8, 1 MiB max document).
- `*_create_with_config` returns NULL on invalid config (event_queue_size < 2 when non-zero) or allocation failure — caller must check.

## Event-Driven Plumbing Model (ADR 006)

- Primary output mechanism: `int yaml_next_event(ctx, yaml_event_t *event)` — returns 1 if an event was dequeued and filled, 0 otherwise.
- Events deliver unencapsulated data (e.g., scalar values, collection boundaries, document markers).
- No auto-responses or policy decisions inside the library. Parsed data is returned raw for the caller to act upon.
- Serializer helpers (`yaml_emit_*`) queue events and append to the output buffer; they return 0 on success, -1 on error.
- `yaml_get_output` returns bytes copied (0 if none).
- `yaml_feed_input` returns bytes consumed; internally enqueues parse events and populates the knowledge tree.

## Knowledge Tree

- A hierarchical key-value store built automatically during parsing.
- Supports dot-separated key paths for lookup (e.g., `"server.port"`).
- Stores scalar values only (non-scalar events are tracked structurally but not stored).
- Reset clears the entire tree.
- The knowledge tree is a pure convenience: the caller can also build their own representation from the event stream.

## Invariants

- The library never blocks, never calls `read`/`write`/`socket`/`open` (malloc/realloc used internally for buffers and tree nodes; no I/O syscalls).
- All I/O integration is the responsibility of the caller. Caller feeds contiguous byte buffers into `yaml_feed_input` and drains via `yaml_get_output`.
- State transitions are deterministic. Error handling via events or return codes.
- Dialectic testing: all tests use paired parser/serializer contexts exchanging buffers in-memory.
- All event payload structs use embedded arrays (not pointers) for copy-safety in ring buffers.

## Deliberate Absences (by design)

- No file I/O or stream reading — caller provides buffers.
- No TLS/crypto handling.
- No dynamic memory allocation in hot paths (only during creation and tree building).
- No callbacks — progress is always pull-driven via next_event / state queries.
- No knowledge of specific event loops or syscalls.
- No YAML schema validation — the library is pure plumbing.

## Entry Points (Common Across Modules)

- Creation: `yaml_create(role)`, `yaml_create_with_config(role, config)` → ctx or NULL
- Teardown: `yaml_destroy(ctx)`, `yaml_reset(ctx)`
- I/O: `size_t yaml_feed_input(ctx, data, len)`, `size_t yaml_get_output(ctx, buf, max_len)`
- Events: `int yaml_next_event(ctx, yaml_event_t *event)` → 1/0
- Serializer: `yaml_emit_*` family of helpers
- Knowledge: `const char *yaml_lookup_scalar(ctx, path, &len)`, `yaml_merge_events(dst, src)`, `yaml_serialize_kv(ctx, key, value, len)`

## How the State Machine Works

The parser maintains an explicit state (IDLE, STREAM, DOCUMENT, MAPPING_KEY, MAPPING_VALUE, SEQUENCE, DONE, ERROR). Input bytes are tokenized into YAML tokens (scalars, keys, markers, comments), events are enqueued, and the knowledge tree is populated. The serializer translates events into YAML text in the output buffer. Caller drives: feed → next_event loop until no more events, drain output, repeat.

## Implemented Features (as of 2026-06-30)

The following features have been implemented beyond the initial skeleton:

- **Block scalar support**: Literal (`|`) and folded (`>`) block scalar parsing with chomp indicators.
- **Flow collection support**: Flow sequences (`[a, b]`) and flow mappings (`{k: v}`) including nested flow.
- **Anchor/alias parsing**: `&anchor` definitions and `*alias` references emitted as YAML_EVENT_ALIAS events.
- **Tag parsing**: `!tag` and `!!type` prefixes parsed and stored in event structs. Strict mode rejects unknown tags.
- **Indentation-based nesting**: Proper indent stack with depth tracking for nested mappings and sequences.
- **Multiline scalar continuation**: Plain scalars spanning multiple lines at same/greater indent.
- **Serializer improvements**: Flow style output (configurable), tag output, anchor/alias emission, proper indentation tracking.
- **Error events**: YAML_EVENT_ERROR with embedded char array (copy-safe). Triggered by malformed input, UTF-8 violations, max_document_size exceeded.
- **Knowledge tree API**: `yaml_get_child_count()`, `yaml_knowledge_depth()` for tree exploration.
- **Status query API**: `yaml_parser_state()`, `yaml_depth()`, `yaml_has_pending_events()`, `yaml_event_count()`, `yaml_output_pending()`, `yaml_output_size()`.
- **Fuzz harnesses**: 3 targets (parser, serializer, knowledge tree).
- **Valgrind targets**: For all test binaries (smoke, dialectic, errors, knowledge, nesting, block_scalar).

## Remaining Growth Areas

- Event queue backpressure policy (currently silent drop on overflow).
- Deep nested key-path lookup with sequence intermediates in knowledge tree.
- Unicode escape sequences (\uXXXX, \UXXXXXXXX) in double-quoted scalars.
- Partial token buffering across incremental feed_input calls.

## Documentation and Manpages

See `docs/` and `man/man3/` (installed as section 3 manpages) for C API details, return codes, and calling conventions. All public functions document NULL checks, return values (0 success / -1 error / bytes / 1 event), and event semantics.

This architecture guarantees pure byte-buffer testability, maximum reusability as plumbing, and strict adherence to no-syscall / no-callback rules.
