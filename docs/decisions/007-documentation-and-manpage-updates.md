# ADR 007: Documentation and Manpage Updates Required with Functional or API Changes

**Date**: 2026-06-27  
**Status**: Accepted  
**Deciders**: Project maintainers

## Context
The project has a strong emphasis on the plumbing model, explicit `yaml_event_t` events, configurable initialization via `*_config_t` structs, and the agent-ready scaffold. There must be an explicit rule tying documentation updates to code changes to prevent drift.

## Decision
**Any functional change or public API modification must be accompanied by corresponding documentation updates in the same patch.**

This includes, but is not limited to:
- New or changed functions, parameters, or return values (including error cases and NULL handling)
- New or modified events in `include/yaml.h`
- Changes to config structs (`yaml_config_t`) or creation semantics
- Behavioral changes (state transitions, block scalar vs flow scalar handling, queue semantics)
- New features (YAML tags, anchors and aliases, multi-document streams, block scalars, etc.)

**Required updates in the same change**:
1. Update or add entries in `docs/decisions/` when the architectural assumption changes.
2. Revise `ARCHITECTURE.md` (module boundaries, entry points, invariants, plumbing model).
3. Revise `DOMAIN.md` (new concepts, event descriptions, workflows, tag handling).
4. Update `docs/README.md` index if new manpages or sections are added.
5. **Update the relevant manpages in `man/man3/`** (when they exist):
   - Add or revise sections for affected functions.
   - Ensure return codes, const-data lifetimes, config options, and caller responsibilities are accurately documented.
6. Optionally update root `README.md`, `AGENTS.md`, or examples.

The change is not considered complete until the documentation has been updated, reviewed, and the tests still pass (`ctest`).

## Rationale
- A library's primary deliverable to users is its **documented API**.
- Enforces the "agent-ready" and reviewable-patch philosophy.
- Prevents the accumulation of technical debt in the documentation layer.
- Supports downstream users, IDEs, and tooling.

## Consequences
- **Definition of done** (AGENTS.md) is extended: "Documentation updated for any API or behavioral change."
- Code review checklist now explicitly includes doc diffs.
- Future functional work will always produce a documentation delta.

## Verification
- Every PR that touches `src/*.c`, `include/*.h`, or adds new public symbols must also modify at least one `.md` file.
- `ctest` must pass.

This decision elevates documentation to the same engineering rigor as the protocol state machine.