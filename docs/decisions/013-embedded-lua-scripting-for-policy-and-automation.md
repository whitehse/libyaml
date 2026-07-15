# ADR 013: Embedded Lua Scripting for Policy and Automation

**Date**: 2026-06-30
**Status**: Accepted
**Deciders**: Project maintainers (Hermes agent + human review)

## Context

The library is a pure C, system-call-free YAML parser and serializer state machine. Per ADR 006 (Core Library as Plumbing), the library exposes structured events and data but contains no policy, no I/O, and no application logic. Per ADR 002 (Event-Loop Compatibility) and ADR 012 (Extended Event-Loop and Real-Time Compatibility), the library must drive cleanly from any event loop or coroutine scheduler without assumptions about threading, blocking, or scheduling.

The calling application needs a programmable layer to define operational policy — validation rules, transformation logic, routing decisions, safety constraints — without embedding that logic in C. This layer must be deterministic, inspectable, and safe. The long-term vision is an AI-driven automation harness where multiple sibling libraries (libhttp2, libnetconf, librest, libpqwire, libdiscord, libslack) form a programmable substrate. A control-plane language is needed that bridges non-deterministic AI reasoning with deterministic protocol operation.

## Decision

The library shall support integration with an **embedded Lua scripting layer** owned and managed entirely by the calling application. The following principles govern this integration:

### Core as Plumbing Remains

- The library itself shall **not** embed a Lua VM. It remains a pure C state machine per ADR 006.
- The library shall expose its internal objects, state, and events through a well-defined **introspection and mutation API** — C functions that return structured data to any consumer.
- The calling application bridges this API to a Lua VM. The library has no knowledge of Lua.
- Lua shall never make system calls or I/O calls through the library. All I/O boundaries remain at the application level.

### Objects and Data Exposed to Scripting

The library shall expose the following through its C API so that an application-level Lua bridge can inspect and manipulate them:

- **Parse events**: type, start/end marks, and associated metadata from `yaml_next_event`
- **Document trees**: root nodes, document boundaries, and multi-document streams
- **Mapping and sequence nodes**: structure, ordering, nested containment
- **Scalar values**: content, style (plain, quoted, literal, folded), and resolved type hints
- **Anchors and aliases**: anchor names, alias references, and resolution state
- **Tags**: tag handles, suffixes, and application-provided resolution results
- **Serialization state**: emitter queue, output buffer status, current nesting depth
- **Configuration and context**: `yaml_config_t` fields, error state, parser/serializer role

The library provides the data; Lua provides the policy.

### Coroutine-Based Non-Blocking Execution

- Lua coroutines shall map naturally to the library's event-driven model.
- A Lua script shall be able to yield at event boundaries — after `yaml_next_event`, after `yaml_feed_input` — and resume when new data arrives from the event loop.
- No blocking. No OS threads. Cooperative scheduling only.
- This aligns with ADR 012's requirement for coroutine-yield compatibility. The library's bounded, re-entrant call semantics already support yield-friendly consumption.

### Event-Loop and io_uring Compatibility

- The Lua integration layer (in the application, not in the library) shall work within any unspecified event loop per ADR 002 or with io_uring.
- Lua coroutine scheduling shall be driven by the same event loop that drives the protocol library.
- When the event loop signals readiness, the application feeds data to the library, retrieves events, and resumes the corresponding Lua coroutine with the event data. The library remains unaware of this orchestration.

### Minimal System Calls

- Both the library and the Lua integration layer shall avoid system calls.
- Pure computation, buffer manipulation, and state transitions only.
- All I/O boundaries remain at the application level, outside both the library and the Lua script execution context.
- Lua scripts shall not contain `os.execute`, `io.*`, or `socket.*` calls. The application shall provide a restricted Lua environment with only safe libraries loaded.

### AI Harness Integration

- This is the long-term architectural vision. A future AI harness shall use the sibling protocol libraries as building blocks for automated reasoning and operations.
- Lua serves as the **deterministic control plane**: operators program business logic, safety constraints, and operational rules in Lua scripts.
- The AI agent can read and modify Lua state (tables, variables, registered callbacks) to influence system behavior. Lua is the bridge between non-deterministic AI reasoning and deterministic protocol operation.
- The introspection API makes this possible: the AI agent queries library state through Lua, and the Lua scripts enforce deterministic policy on every event.

### Design for Sibling Libraries

- This pattern shall apply uniformly across all sibling libraries: libhttp2, libnetconf, librest, libpqwire, libdiscord, libslack.
- Each library exposes its domain objects (frames, messages, queries, payloads) through a consistent introspection/mutation C API.
- Each library's C API is bridged to Lua by the calling application using the same pattern.
- Together, the libraries form a **programmable substrate** for an AI-driven automation harness. Lua is the common scripting layer; each library is a pure plumbing component.

## Rationale

- **Lua is the natural fit**: lightweight (under 300 KB), coroutine-native, embeddable, no OS dependencies, deterministic execution, widely used as an embedded scripting language in C projects (Redis, Nginx, Wireshark, game engines).
- **Decoupling policy from plumbing**: the library remains pure per ADR 006. Policy lives in Lua. This prevents the C core from accumulating domain-specific behavior.
- **Coroutine alignment**: Lua's native coroutine support (`coroutine.yield`, `coroutine.resume`) maps 1:1 to the library's event-driven pull model. A script yields after each event; the event loop resumes it when more data arrives.
- **Safety**: the sandboxed Lua environment prevents system calls. The library provides data; Lua transforms it. Neither side can perform I/O.
- **Extensibility**: the same introspection API that serves Lua today can serve other scripting engines, FFI bindings (per ADR 010), or direct AI agent integration tomorrow.
- **Sibling library consistency**: a uniform scripting pattern across all protocol libraries reduces the learning curve and enables cross-library automation scripts.

## Consequences

- The library's C API shall be reviewed to ensure all internal state is reachable through introspection functions. No opaque state that a scripting layer cannot inspect.
- New `yaml_introspect_*` or `yaml_query_*` functions may be added to expose document trees, node attributes, and serialization state. These functions shall follow ADR 010 naming and ownership conventions.
- The `examples/` directory shall eventually include a reference Lua integration showing coroutine-based event consumption.
- `AGENTS.md`, `ARCHITECTURE.md`, and `docs/README.md` shall document this scripting integration pattern as a first-class architectural concern.
- Lua integration code shall live in the application layer (or a separate `libyaml-lua` bridge module), never in the core library.
- The restricted Lua environment (no `os`, no `io`, no `debug` library, no `loadfile`/`dofile`) shall be documented as a security requirement.
- Sibling libraries shall adopt the same introspection API shape for consistency.

## Verification

- The core library shall continue to build and pass all tests with zero Lua dependencies.
- A reference Lua integration example shall demonstrate coroutine-based event consumption with no blocking and no system calls from the script.
- Valgrind shall confirm no memory leaks when Lua scripts process events through the introspection API.
- The introspection API shall be exercised from C tests (no Lua) to verify it exposes complete and correct state.
- Future ADRs that touch the public API or state machine shall reference ADR 013 alongside ADR 002, ADR 006, ADR 010, and ADR 012.

This decision establishes the architectural foundation for a programmable, AI-ready automation substrate built on pure C protocol libraries and deterministic Lua scripting.