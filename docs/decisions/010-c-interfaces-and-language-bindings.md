# ADR 010: C Interfaces and Implementations + Language Binding Friendly Design

**Date**: 2026-06-27  
**Status**: Accepted  
**Deciders**: Project maintainers

## Context
The library is a pure C state machine for YAML parsing. As the interface matures, it is important to establish long-term design principles for the public API.

Two influences are particularly relevant:
1. **C Interfaces and Implementations** (David R. Hanson, 1996) — opaque types, consistent naming, minimal interfaces, clear ownership.
2. **Language Binding Friendliness** — protocol libraries are frequently consumed via FFI from Rust, Go, Python, Zig, etc.

## Decision
The library shall adopt the following principles:

### From *C Interfaces and Implementations*
- **Opaque types**: `yaml_ctx_t` remains completely opaque.
- **Consistent naming**: All public symbols use the `yaml_` prefix.
- **Minimal interfaces**: Only what is necessary is exposed.
- **Clear ownership**: Functions that return newly allocated objects document the corresponding `*_destroy`.
- **Error handling**: Prefer explicit return values or out-parameters.

### Language Binding Support
- Public headers shall avoid:
  - Complex macros that expand to code
  - Bitfields in public structures
  - Inline functions that expose implementation details
  - C++ reserved words or GNU extensions in public APIs
- All handles are passed as pointers (`T *`).
- Function signatures should be straightforward for `bindgen`, `cffi`, `ctypes`, etc.
- Event structures (`yaml_event_t`) use simple unions and fixed-size arrays where possible.

## Rationale
- Hanson's book provides battle-tested patterns.
- Designing for FFI from the start reduces long-term maintenance.
- These principles reinforce the existing "plumbing" philosophy: thin, predictable, buffer-in/buffer-out interfaces.

## Consequences
- Future changes to `include/yaml.h` and `include/yaml.h` must be reviewed against these constraints.
- The `yaml_event_t` union and config structs are intentionally simple.
- Documentation must explicitly state ownership and error semantics.
- Preference will be given to designs that remain easy to bind.

## Verification
- Public headers continue to use opaque types and consistent prefixes.
- New public functions follow the established naming and ownership patterns.
- The library remains easy to consume from non-C languages (future target).

This decision strengthens both the internal quality and the external usability of the library.