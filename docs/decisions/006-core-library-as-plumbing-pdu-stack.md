# ADR 006: Core Library as Plumbing — PDU Parsing with Unencapsulated Data Exposure

**Date**: 2026-06-27  
**Status**: Accepted  
**Deciders**: Project maintainers

## Context
The project follows the "plumbing" philosophy from the start (see ARCHITECTURE.md and user memory). However, it is important to explicitly record the decision so that future extensions (block scalar parsing, YAML tags, anchors and aliases, etc.) do not accidentally introduce active behavior.

## Decision
The core library (`libyaml`) shall act strictly as **plumbing**.

### Key Principles

1. **Minimal Active Code**
   - The library should contain as little "active" logic as possible.
   - It is an expert at **parsing and serializing YAML** — stream start/end, document start/end, mapping start/end, sequence start/end, scalar, alias, etc.
   - It does **not** decide what to do with the data (no auto-document handling, no auto-merge resolution, no schema validation).

2. **Unencapsulated Data Exposure**
   - Parsed data is returned to the caller in its **unencapsulated form** (complete events, scalar content, tag information, anchor/alias references).
   - The library exposes higher-level transmitted content without requiring the caller to understand every framing detail.

3. **Event-Driven Return of Data**
   - Instead of the library performing actions, it **emits** structured data to the caller via `yaml_next_event` + `yaml_event_t`.
   - The calling application is responsible for acting on this data (e.g., resolving tags, building data structures, handling anchors and aliases).

4. **Networking Stack Model**
   - The library behaves like a protocol layer in a stack.
   - YAML input is reduced to structured events + metadata.
   - A higher-level library or the application itself consumes these events without deep knowledge of the YAML specification.

5. **Caller Owns All Policy and Side Effects**
   - All networking I/O, schema resolution, tag handling, application logic, file operations, and decision-making live exclusively in the calling application.
   - The library only transforms bytes ↔ structured protocol data.

## Rationale
- Maximizes reusability and testability (dialectic tests become trivial buffer shuttles).
- Allows the same core PDU parser to be used by many different applications (raw clients, ORMs, proxies, fuzzers).
- Prevents the core from accumulating domain-specific behavior.
- Aligns with the explicit `yaml_event_t` preference and the "core as plumbing" mandate already recorded in project memory.

## Consequences
- New APIs must favor returning parsed data structures or events rather than internally generating responses.
- Block scalar parsing must emit raw content; actual application of tags stays outside the core.
- Documentation and examples must clearly show the separation between the protocol library and the application layer.

## Verification
- Code reviews will check that new functionality stays within PDU parsing/serialization boundaries.
- Dialectic tests continue to demonstrate that two protocol contexts can exchange data purely through buffer handoff.

This decision elevates the "pure state machine" principle into a full **streaming parser philosophy** and is considered foundational for libyaml.