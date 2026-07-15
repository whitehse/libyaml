# ADR 005: Integrated Knowledge Tree for Key-Path Scalar Lookup

**Date**: 2026-06-30  
**Status**: Accepted  
**Deciders**: Project maintainers

## Context

The sibling protocol libraries (libhttp2, libnetconf, librest, libdiscord, libslack) are pure PDU parsers that emit structured events and leave all interpretation to the caller. libyaml could follow the same pattern strictly — parse YAML into events and let the caller build whatever representation they need.

However, YAML is fundamentally a configuration and data serialization format. The most common use case is: parse a YAML document and look up values by key path (e.g., `server.port`, `database.host`). Requiring every caller to build their own tree from the event stream would be significant duplicated effort.

## Decision

We will include an integrated knowledge tree in the parser that:

1. **Builds automatically during parsing** — no separate API call needed. As YAML scalars are encountered, they are indexed by their key path.
2. **Supports dot-separated key paths** — `yaml_lookup_scalar(ctx, "server.port", &len)` returns the value directly.
3. **Tracks nesting depth** — `yaml_get_child_count()` and `yaml_knowledge_depth()` allow tree exploration.
4. **Is a pure convenience** — the caller can ignore it entirely and use the event stream directly. The knowledge tree does not replace or modify events.
5. **Is parser-only** — the serializer does not maintain a knowledge tree.

## Rationale

- YAML configuration lookup is the dominant use case. Making it zero-effort for callers reduces boilerplate across all consuming applications.
- The knowledge tree is built during the same parse pass as event emission — no additional overhead.
- Keeping it as an optional convenience (not the primary API) preserves the plumbing philosophy: events are the primary output, the tree is a derived index.
- Other format-specific conveniences (e.g., JSON path lookup) follow the same pattern in their respective libraries.

## Consequences

- The parser context carries a knowledge tree alongside the event queue.
- `yaml_destroy()` frees both the event queue and the knowledge tree.
- `yaml_reset()` clears both.
- Deep nesting with sequence intermediates (e.g., `items.0.name`) requires additional work to fully index.
- The knowledge tree adds memory overhead proportional to the number of scalar key-value pairs in the document.

## Verification

- Knowledge tree tests verify flat lookup, nested lookup, and tree exploration APIs.
- Fuzz harness (`fuzz_yaml_knowledge.c`) exercises random key paths and deep nesting.
- Valgrind tests confirm no memory leaks in tree construction and destruction.
