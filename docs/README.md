# Documentation Index — libyaml

## Entry Points

- [AGENTS.md](../AGENTS.md) — Start here for every task. Build commands, operating rules, definition of done.
- [ARCHITECTURE.md](../ARCHITECTURE.md) — Module boundaries, invariants, deliberate absences.

## Domain Knowledge

- [DOMAIN.md](DOMAIN.md) — YAML domain glossary, event model, workflows, scalar styles.

## Architecture Decision Records

| ADR | Title | Status |
|-----|-------|--------|
| 001 | Agent-Ready Documentation | Accepted |
| 002 | Event-Loop Compatibility | Accepted |
| 003 | Testing, Fuzzing & Valgrind | Accepted |
| 004 | Dialectic Client-Server Testing | Accepted |
| 005 | Integrated Knowledge Tree for Key-Path Scalar Lookup | Accepted |
| 006 | Core Library as Plumbing (PDU Stack) | Accepted |
| 007 | Documentation and Manpage Updates | Accepted |
| 008 | C-Only Examples and Codebase | Accepted |
| 009 | Consistent Protocol Interfaces | Accepted |
| 010 | C Interfaces and Language Bindings | Accepted |
| 011 | Nefarious MITM Adversarial Testing | Accepted |
| 012 | Extended Event-Loop and Real-Time Compatibility | Accepted |

## Test Suites

- `tests/test_yaml_smoke.c` — API surface: create/destroy, NULL handling, config validation, parser/serializer basics, reset, event queue overflow.
- `tests/test_yaml_dialectic.c` — Paired parser↔serializer buffer exchange: round-trip mapping/sequence, multi-document, quoted scalars, KV serialization, incremental feed.
- `tests/test_yaml_errors.c` — Error paths: wrong role operations, NULL handling, edge-case inputs.
- `tests/test_yaml_knowledge.c` — Knowledge tree: key lookup, multiple keys, reset behavior, sequences.
- `tests/test_yaml_knowledge_deep.c` — Deep knowledge tree: nested key paths, sequence intermediates, tree exploration.
- `tests/test_yaml_nesting.c` — Indentation-based nesting: nested mappings, triple nesting, mixed sequence-of-mappings, mapping-with-sequence-value, round-trip.
- `tests/test_yaml_block_scalars.c` — Literal (`|`) and folded (`>`) block scalars with chomp indicators.
- `tests/test_yaml_flow.c` — Flow sequences (`[a, b]`) and flow mappings (`{k: v}`) including nested flow.
- `tests/test_yaml_anchor_alias.c` — Anchor definitions (`&name`) and alias references (`*name`).
- `tests/test_yaml_quoted_edge.c` — Edge cases in single-quoted and double-quoted scalars.
- `tests/test_yaml_multiline.c` — Multiline scalar continuation.
- `tests/test_yaml_api.c` — API surface: depth, parser_state, has_pending_events, event_count, output_pending, output_size.
- `tests/test_yaml_roundtrip.c` — Parse → serialize → parse round-trip fidelity.

## Fuzzing

- `fuzz/fuzz_yaml.c` — libFuzzer harness targeting the parser surface.
- `fuzz/fuzz_yaml_serializer.c` — Serializer fuzz harness: random event sequences.
- `fuzz/fuzz_yaml_knowledge.c` — Knowledge tree fuzz harness: random key paths and deep nesting.

## Examples

- `examples/yaml_parse_example.c` — Demonstrates parsing YAML text into events and querying the knowledge tree.
- `examples/yaml_roundtrip_example.c` — Demonstrates parse → events → serialize round-trip.
