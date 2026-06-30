# ADR 011: Nefarious Man-in-the-Middle Adversarial Testing Harness

**Date**: 2026-06-27  
**Status**: Accepted  
**Deciders**: Project maintainers

## Context
As the library matures and gains more protocol coverage (including extended query protocol, COPY, authentication, and binary data handling), it becomes increasingly important to test its robustness against malicious or malformed input.

Traditional unit and dialectic tests are valuable but tend to be cooperative. To properly stress the protocol state machine, parser, and authentication logic, we need an **active adversary** that can sit between a legitimate client and server and deliberately attempt to break or compromise the session.

## Decision
We will implement a **nefarious Man-in-the-Middle (MITM)** testing harness as part of the project. This component will:

- Sit between two `yaml_ctx_t` instances (parser and serializer).
- Intercept, inspect, mutate, drop, reorder, duplicate, or inject YAML events.
- Provide configurable attack strategies (e.g., length corruption, bit flips, state machine confusion, tag injection attacks).
- Be usable both in unit tests and as a foundation for fuzzing.

The MITM will be implemented in a way that keeps the core library clean while providing powerful adversarial testing capabilities.

## Rationale
- Protocol implementations are security-critical.
- Cooperative tests often miss edge cases that real attackers would exploit.
- A dedicated adversarial harness aligns with the project's strong emphasis on robustness and input validation.
- It provides a natural path toward structured fuzzing (`fuzz_yaml`).

## Consequences
- New files will be added under `tests/mitm/` or `src/mitm/`.
- New event types or hooks may be added to allow the MITM to observe and modify traffic.
- Fuzzing campaigns will be able to use the MITM as a mutation engine.
- Documentation and ADRs will reference this adversarial testing approach.

## Verification
- The MITM will be exercised in dedicated test cases.
- Fuzzing runs using the MITM should demonstrate the discovery of previously untested edge cases.
- All existing tests must continue to pass.

This decision formalizes the commitment to proactive, adversarial testing of the YAML parsing implementation.