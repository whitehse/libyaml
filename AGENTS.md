# AGENTS.md — libyaml

**Project identity**: Pure C state-machine YAML parser and serializer library. System-call free, callback free. All I/O, networking, and event handling lives exclusively in the calling application. The library only consumes/produces byte buffers and explicit state transitions. Provides YAML 1.2 parsing from memory buffers into structured events, serialization of events back to YAML text in memory buffers, and a knowledge tree for key-path scalar lookup.

**Key commands** (run from repo root):
- `cmake -B build -S . && cmake --build build` — configure and build the static library + tests
- `ctest --test-dir build` — run verification tests
- `cmake --build build --target install` — install (optional)

**Documentation map** (progressive disclosure):
- AGENTS.md (this file) — start here for every task
- ARCHITECTURE.md — module boundaries, invariants, deliberate absences
- docs/README.md — full documentation index
- docs/DOMAIN.md — YAML domain glossary and workflows
- docs/decisions/ — Architecture Decision Records (ADRs)

**Operating rules**:
- Never introduce system calls, callbacks, or hidden I/O inside the library.
- State machine design follows libassh patterns: explicit states, deterministic transitions driven only by input buffers and caller-supplied context.
- Every change must keep the library buildable with `-Wall -Wextra -Wpedantic -Werror` (or MSVC equivalent) and pass existing tests.
- Prefer small, reviewable patches. Update relevant docs/ADRs when architecture or domain assumptions change.
- Hermes agent (or any coding agent) must consult AGENTS.md before editing code or docs.

**Definition of done** (for any ticket):
- Code compiles cleanly under strict warnings.
- Tests pass (`ctest`).
- AGENTS.md, ARCHITECTURE.md, and relevant docs remain accurate.
- No new syscalls or callbacks introduced.
- State machine remains pure (inputs → state/output only).

**Current status**: Functional YAML parser/serializer. Supports plain/quoted scalars, mappings, sequences, multi-document streams, knowledge-tree scalar lookup, merge helpers, and KV serialization. Event-driven architecture with configurable queue size.

**Testing, Fuzzing & Valgrind Policy** (see ADR 003):
- Every change to core protocol files must add or update tests in `tests/`.
- Run `ctest` before considering any change complete.
- Parser changes require a libFuzzer run of several million iterations with no crashes.
- All tests must pass under Valgrind with no leaks or memory errors.

**Current Interface Direction**:
- All protocol modules should expose a consistent shape:
  - `yaml_config_t` (with `event_queue_size`, `max_document_size`, `strict_mode`)
  - `yaml_create(role)` and `yaml_create_with_config(role, config)`
  - `yaml_feed_input(ctx, data, len)`
  - `yaml_next_event(ctx, &event)` returning `yaml_event_t`
  - `yaml_get_output(ctx, buf, max)`
- Event-driven path preferred over legacy getters.
- Roles: PARSER (YAML text → events + knowledge tree) and SERIALIZER (events → YAML text output).

**Known Limitations / Areas for Improvement**:
- Parser handles plain and single/double-quoted scalars; literal (|) and folded (>) block scalars not yet implemented.
- No YAML tag resolution beyond basic handling.
- No anchor/alias resolution (aliases emitted as events but not resolved).
- Indentation-based nesting uses simplified heuristics; edge cases with mixed styles may not parse correctly.
- Knowledge tree supports flat and simple nested key paths; deep nesting with sequences as intermediate nodes needs work.

When making changes, prefer extending the event-driven path.

**ADR 010 Alignment (C Interfaces and Implementations + Language Bindings)**:
- All public interfaces follow opaque type principles from Hanson's *C Interfaces and Implementations*.
- Public headers are designed to be FFI-friendly (simple types, no complex macros or bitfields).
- Consistent naming and clear ownership semantics.
- When adding or modifying public functions, prefer designs that are easy to consume from other languages.
