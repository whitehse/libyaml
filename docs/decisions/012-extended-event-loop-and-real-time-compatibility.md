# ADR 012: Extended Event-Loop and Real-Time Compatibility Requirements

**Date**: 2026-06-28  
**Status**: Accepted  
**Deciders**: Project maintainers (Hermes agent + human review)

## Context

This ADR extends ADR 002 (Event-Loop Compatibility Requirement). The original four mechanisms (io_uring/liburing, libev, libuv, epoll) remain mandatory compatibility targets.

In addition, the library must now support integration with:

- ESP-IDF Event Loop Library (for ESP32 and other Espressif embedded targets; uses a callback-driven but non-blocking event loop with esp_event_post and handler registration)
- Coroutine-based execution models (e.g., cooperative multitasking via libraries such as libcoro, or language extensions / manual stack switching in pure C; requires the state machine to be re-entrant and yield-friendly with bounded stack usage)
- Real-time programming environments and operating systems that support real-time programming (RTOS such as FreeRTOS, Zephyr, QNX, VxWorks, or Linux with PREEMPT_RT; emphasis on deterministic latency, bounded worst-case execution time (WCET), priority-aware scheduling, avoidance of priority inversion, and no unbounded operations)

Any future change to the core state machine or sibling protocol modules must remain compatible with how these additional environments consume and drive the API.

## Decision

All changes to the core library **must** be compatible with the usage patterns demonstrated by the original four reference examples **plus** the extended set:

- ESP-IDF Event Loop (non-blocking post/handler model)
- Coroutine drivers (yield points at `feed_input` / `next_event` / `get_output` boundaries; no long-running operations inside the library)
- Real-time schedulers (bounded execution time per call, no malloc in hot paths where possible, support for priority inheritance / ceiling protocols at the application level, avoidance of priority inversion through the library, support for both preemptive and cooperative RT contexts)

The library shall remain:

- System-call free
- Callback free (even when the calling environment uses callbacks, the library itself exposes only pull/push buffer APIs)
- Driven exclusively by `yaml_feed_input(ctx, buf, len)`, `yaml_next_event(ctx, &event)`, and `yaml_get_output(ctx, buf, max)`
- Able to work with edge-triggered, level-triggered, completion-based, event-post, and coroutine-yield models
- Free of assumptions about blocking behavior, threading model, buffer ownership, or scheduling policy
- Suitable for real-time use: all public entry points must have bounded, deterministic execution time; no operations whose duration depends on input size or internal state growth without explicit caller-configurable limits

## Rationale

The expanded set of environments represents critical use cases for embedded, IoT, high-reliability, and latency-sensitive deployments:

- ESP-IDF: Dominant framework for ESP32 Wi-Fi/BT SoCs; many industrial and consumer IoT devices
- Coroutines: Enable lightweight, stack-efficient cooperative multitasking without OS threads; common in embedded and high-concurrency C codebases
- Real-time OSes and RT programming: Required for safety-critical, automotive, avionics, robotics, and industrial control systems where missed deadlines are unacceptable

By explicitly requiring compatibility, we prevent introduction of:

- Unbounded loops or allocations
- Hidden state that breaks re-entrancy or coroutine yields
- Timing assumptions incompatible with RT schedulers
- Callback or thread-local storage usage inside the library

## Consequences

- Any proposed change to the public API or internal state machine must be reviewed against both the original four examples and the extended targets (ESP-IDF, coroutine harnesses, RTOS integration examples).
- New features must be exercisable from all supported environments.
- The `examples/` directory should eventually include reference integrations for the new targets (or at minimum, the core must not preclude them).
- `AGENTS.md`, `ARCHITECTURE.md`, and `docs/README.md` must list this extended constraint as a first-class invariant.
- Real-time verification (e.g., static analysis for WCET, priority inversion checks) may become part of the CI policy for core changes.

## Verification

- All original four examples continue to build and the new compatibility targets must not introduce breakage.
- The core test and dialectic tests continue to pass.
- Future ADRs or PRs that touch the state machine must reference both ADR 002 and ADR 012.
- Documentation must explicitly call out real-time constraints (bounded time, re-entrancy, no syscalls).

This decision ensures the library remains a true reusable state machine suitable for the broadest possible range of high-performance, embedded, and real-time environments.