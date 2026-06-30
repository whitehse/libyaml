# ADR 003: Mandatory Testing, Fuzzing, and Valgrind for Core Library Changes

**Date**: 2026-06-27  
**Status**: Accepted  
**Deciders**: Project maintainers

## Context
The library is a security-sensitive, pure state machine that will be driven by untrusted input (YAML parsing) from many event loops (liburing, libev, libuv, epoll). Changes to the core (`src/yaml.c`, `include/yaml.h`, YAML parsing, block scalar parsing, tag handling including YAML tags) carry high risk of memory safety bugs, parsing errors, and protocol violations.

## Decision
All changes to the core library **must** pass the following before being considered complete:

1. **Unit / Integration Tests**
   - All new functionality must have corresponding tests in `tests/`.
   - Existing tests must continue to pass (`ctest`).

2. **Fuzzing**
   - A libFuzzer (or AFL++) target (`fuzz_yaml`) should exist and be run on any non-trivial change to YAML parsing, block scalar parsing, or tag handling.
   - Fuzzing runs of at least several million iterations with no new crashes or sanitizer violations are required for parser changes.

3. **Valgrind**
   - All tests must pass under Valgrind (`valgrind --leak-check=full --error-exitcode=1`).
   - No new memory leaks, use-after-free, or invalid reads/writes introduced.

## Rationale
- The library is intended to be embedded in high-performance applications handling untrusted input.
- The event-loop harnesses demonstrate that the same core code will be exercised in very different ways.
- Requiring fuzzing + Valgrind raises the bar for memory safety and protocol correctness, especially around block scalars and YAML tags.

## Consequences
- CMake should expose fuzzing and valgrind targets when implemented.
- Any PR or agent-generated change touching the core parser or block scalar parsing must include evidence of fuzzing runs and a clean Valgrind report.
- Test coverage is now part of the Definition of Done in `AGENTS.md`.

## Verification
- `ctest`
- Future `./build/fuzz_yaml` (libFuzzer) or AFL++ campaign
- `ctest -D ExperimentalMemCheck` or direct `valgrind` invocation on the test binary

This policy is recorded so future agents and contributors understand the quality bar for the core library.