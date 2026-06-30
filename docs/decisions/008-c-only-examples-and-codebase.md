# ADR 008: Strict C Language Requirement for Examples and Codebase

**Date**: 2026-06-27  
**Status**: Accepted  
**Deciders**: Project maintainers

## Context
The core library is implemented in pure C11 with no C++ features. The project follows the "pure C state-machine" identity and must maintain language purity for portability, embedded use, static analysis, and cross-compilation.

## Decision
**All code in this repository, including the example programs and tests, must be strictly valid C (C11). No C++ features are permitted.**

This applies to:
- Core library sources and headers
- All files under `examples/`
- Test files
- Any supporting scripts or build glue that is compiled as C

Allowed: C11 features (`_Static_assert`, designated initializers, `thread_local`, etc.)
Forbidden: C++ lambdas, `auto`, range-based for, `std::` anything, `new`/`delete`, templates, etc.

## Rationale
- Maintains consistency with the "pure C state-machine" identity.
- Simplifies cross-compilation, embedded use, and static analysis.
- Avoids hidden dependencies on a C++ runtime or ABI.
- Makes the examples easier for C programmers and agents to maintain and review.
- Prevents accidental introduction of C++-only constructs that would break the strict `-Werror -pedantic` build.

## Consequences
- Example code using event loops (epoll, liburing, libev, libuv) must use only C idioms.
- Any future feature (binary row decoding, PostGIS support, etc.) must also obey this rule.
- CI / build checks will continue to enforce `-Wall -Wextra -Wpedantic -Werror`.
- When reviewing or generating code, any use of C++ syntax must be rejected.

## Verification
- All examples and tests compile successfully under the existing CMake strict flags.
- New examples or significant changes must be reviewed for C purity.
- This ADR is referenced in `AGENTS.md` and future contribution guidelines.

This decision reinforces the project's commitment to being a thin, portable, pure-C protocol plumbing library.