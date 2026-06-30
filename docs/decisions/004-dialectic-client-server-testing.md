# ADR 004: Dialectic Client+Server Implementation for Protocol Code

**Date**: 2026-06-27  
**Status**: Accepted  
**Deciders**: Project maintainers (via agent directive)

## Context
The YAML format is inherently two-sided (parser/reader and serializer/writer). Implementing only one side risks incomplete coverage, asymmetric bugs, and tests that cannot exercise full parse/emit cycles without external dependencies (real sockets, event loops, or third-party tools).

The existing scaffold uses a role parameter (`YAML_ROLE_PARSER` / `YAML_ROLE_SERIALIZER`). Changes to the core state machine must remain pure (buffer-driven, no syscalls).

## Decision
Any implementation or extension that touches network protocol logic **must** implement symmetric support for both sides of the connection (client and server roles) inside the same state machine / library, at minimum to enable dialectic (paired) testing.

- The library exposes a single `yaml_ctx_t` that can be instantiated in either `YAML_ROLE_PARSER` or `YAML_ROLE_SERIALIZER` mode.
- Parser mode: consumes YAML tokens and emits structured events (stream start/end, document start/end, mapping, sequence, scalar, alias).
- Serializer mode: emits YAML output from structured events (including block scalars and YAML tags).
- All testing of new YAML parsing, block scalar handling, or state logic uses paired in-memory parser_ctx ↔ serializer_ctx exchanges (feed one's output buffer directly into the other's `yaml_feed_input`). No sockets, no blocking, no external processes.
- This principle is now a first-class architectural requirement alongside "no syscalls" and "pure state machine."

## Rationale
- Forces complete, symmetric protocol coverage.
- Enables hermetic, reproducible tests that exercise both code paths without network stack.
- Catches client/server asymmetry bugs early (e.g., startup parameter handling, binary vs text format negotiation, OID handling).
- Aligns with the "pure buffer" design: the test harness is just a simple loop shuttling byte arrays.
- Prevents future one-sided features that would be hard to test or verify.

## Consequences
- `yaml_create` (or `yaml_create_with_config`) accepts a role parameter; internal state machine branches on role.
- New tests in `tests/` must demonstrate client↔server roundtrips.
- Documentation (DOMAIN.md, ARCHITECTURE.md, AGENTS.md) updated to reference the dialectic requirement.
- Future extensions (block scalar parsing, YAML tags, anchors and aliases, multi-document streams) must be exercised from both roles in tests.
- No change to the "all I/O lives in caller" rule — role only affects internal generation and parsing of messages.

## Verification
- `ctest` includes dialectic tests that pass with two contexts exchanging buffers.
- Build remains clean under strict warnings.
- No new syscalls or callbacks introduced.
- ADR referenced in future changes touching parser/serializer paths.

This decision codifies the "dialectic development" mandate so that parser and serializer code evolve together and are validated against each other.