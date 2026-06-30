# DOMAIN.md — libyaml

## YAML Overview

YAML (YAML Ain't Markup Language) is a human-readable data serialization language. It is commonly used for configuration files, data exchange, and document storage. YAML 1.2 is the current specification.

## Key Concepts

- **Stream**: A sequence of documents. Starts with optional `---`, ends with optional `...`.
- **Document**: A single YAML value (scalar, mapping, or sequence). Separated by `---`.
- **Mapping**: An unordered set of key-value pairs. Keys are scalars, values can be any type.
- **Sequence**: An ordered list of values, each prefixed with `- `.
- **Scalar**: A single value (string, number, boolean, null). Can be plain, single-quoted, double-quoted, literal (`|`), or folded (`>`).
- **Tag**: An explicit type annotation (e.g., `!!str`, `!!int`). Optional in most cases.
- **Anchor/Alias**: Reusable values via `&anchor` and `*alias` references.
- **Comment**: Lines starting with `#` (not part of the data model).

## Event Model

The library emits events that correspond to YAML structural elements:
- STREAM_START / STREAM_END — document stream boundaries
- DOCUMENT_START / DOCUMENT_END — document boundaries
- MAPPING_START / MAPPING_END — mapping boundaries
- SEQUENCE_START / SEQUENCE_END — sequence boundaries
- SCALAR — a key, value, or standalone scalar
- ALIAS — a reference to an anchored value

## Workflows

### Parsing
```
yaml_feed_input(ctx, yaml_bytes, len) → tokenizer → events queued
yaml_next_event(ctx, &event) → caller processes events
yaml_lookup_scalar(ctx, "server.port", &len) → direct value access
```

### Serializing
```
yaml_emit_stream_start(ctx)
yaml_emit_document_start(ctx, implicit)
yaml_emit_mapping_start(ctx, tag, anchor)
yaml_emit_scalar(ctx, "key", 3, tag, style)
yaml_emit_scalar(ctx, "value", 5, tag, style)
yaml_emit_mapping_end(ctx)
yaml_emit_document_end(ctx, implicit)
yaml_emit_stream_end(ctx)
yaml_get_output(ctx, buf, max) → YAML text bytes
```

### Round-Trip (Parse → Serialize)
```
yaml_feed_input(parser, yaml_text, len)
yaml_merge_events(serializer, parser) → events forwarded
yaml_get_output(serializer, buf, max) → re-serialized YAML
```

## Scalar Styles

| Style | Marker | Description |
|-------|--------|-------------|
| Plain | (none) | Unquoted, auto-detected type |
| Single-quoted | `'...'` | Literal, no escape sequences |
| Double-quoted | `"..."` | Supports escape sequences (\n, \t, \\, \") |
| Literal | `\|` | Preserves newlines exactly |
| Folded | `>` | Folds newlines into spaces |

## Knowledge Tree

The knowledge tree is a hierarchical key-value index built during parsing. It enables O(1) lookup of scalar values by dot-separated key path. This is a convenience feature — the caller can always reconstruct their own data model from the event stream.

## Deliberate Absences

- No schema validation (caller's responsibility).
- No type coercion (all values are strings in the event model).
- No file I/O (buffer-in, buffer-out only).
- No anchor/alias resolution (events emitted as-is; caller resolves).

This file is seeded and should be expanded with human review of real workflows and edge cases.
