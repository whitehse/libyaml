# TODO — libyaml

Current state: 2,751 LOC source, 13 test files (13/13 pass), 3 fuzz
harnesses, 2 examples, 5 manpages. Builds clean under -Wall -Wextra
-Wpedantic -Werror. Valgrind targets for all test binaries.

---

## 1. Parser: Remaining Gaps

### 1.1 Proper State Machine for Mapping Key/Value Alternation
Status: Partially implemented
The parser tracks YAML_STATE_MAPPING_KEY and YAML_STATE_MAPPING_VALUE but
after a key: value pair, the state should return to MAPPING_KEY for the
next key. Current code may stay in MAPPING_VALUE in edge cases.
Files: src/yaml.c (parse_input, the key: value block)

### 1.2 Document End without Explicit Markers
Status: Not implemented
Documents that simply end (EOF without `...`) need to emit proper
DOCUMENT_END, STREAM_END events.
Files: src/yaml.c (parse_input)

### 1.3 Deep Nested Key-Path Lookup with Sequence Intermediates
Status: Not implemented
The knowledge tree supports flat and simple nested key paths; deep nesting
with sequences as intermediate nodes (e.g., "items.0.name") needs work.
Need: track nesting context during parsing and insert into the knowledge
tree at the correct depth including sequence index paths.
Files: src/yaml.c (parse_input knowledge update block)

---

## 2. Serializer: Remaining Gaps

### 2.1 Proper Indentation in Serializer Output
Status: Partially implemented
The serializer tracks ser_depth/ser_indent but nested collection
indentation may not produce correct YAML for all nesting combinations.
Verify and fix for deeply nested mappings, sequences of mappings, and
mixed nesting.
Files: src/yaml.c (all emit_* functions)

---

## 3. Event Queue and Backpressure

### 3.1 Event Queue Backpressure / Notification
Status: Not implemented
When the queue is full, queue_push returns -1 silently. The parser
continues but events are lost. Need one of:
  a) Caller-configurable policy: drop oldest, drop newest, or error
  b) yaml_feed_input should return fewer bytes consumed so caller can retry
  c) Auto-grow queue (configurable)
Files: src/yaml.c (queue_push, yaml_feed_input)

---

## 4. Incremental / Streaming Parsing

### 4.1 Partial Token Buffering Across Feed Calls
Status: Not implemented
Quoted scalars that span multiple feed_input calls are not handled. The
opening quote is in one call, content in the next. Need to track
"in-quote" state across calls.
Files: src/yaml.c

---

## 5. UTF-8 and Unicode

### 5.1 Unicode Escape Sequences in Double-Quoted Scalars
Status: Not implemented
The parser handles basic escapes (\n, \t, \\, \", \') but not:
  - \uXXXX (4-digit Unicode)
  - \UXXXXXXXX (8-digit Unicode)
  - \xXX (2-digit hex)
Files: src/yaml.c (parse_quoted_scalar)

---

## 6. Testing

### 6.1 Tests for Mixed Sequences and Mappings
Status: Not explicitly tested
No dedicated tests for sequences containing mappings:
```yaml
- name: first
  value: 1
- name: second
  value: 2
```
Files: tests/

### 6.2 True Round-Trip Fidelity Test
Status: Partial
The dialectic round-trip tests check for substring presence but not exact
byte-for-byte equality. Need a true round-trip test: parse → serialize →
parse again → compare events.
Files: tests/test_yaml_dialectic.c

---

## 7. Build and Infrastructure

### 7.1 Shared Library Build Option
Status: Not implemented
CMakeLists.txt only builds a static library. Need a BUILD_SHARED_LIBS
option for .so/.dylib/.dll generation.
Files: CMakeLists.txt

### 7.2 pkg-config File Generation
Status: Not implemented
No .pc file generated for easy integration with non-CMake build systems.
Files: CMakeLists.txt, yaml.pc.in (new)

### 7.3 CMake Version Header Generation
Status: Not implemented
No yaml_version.h generated with version macros. Callers cannot check
library version at compile time.
Files: CMakeLists.txt, include/yaml_version.h.in (new)

### 7.4 Sanitizer CI Targets
Status: Not implemented
No AddressSanitizer or UBSan targets beyond the fuzz build. Need CMake
options for -fsanitize=address and -fsanitize=undefined on test binaries.
Files: CMakeLists.txt

---

## 8. Documentation

### 8.1 Additional Manpages
Status: Partial (5 manpages exist)
Additional manpages needed for newer API functions:
  - yaml_emit_mapping_start(3), yaml_emit_sequence_start(3)
  - yaml_emit_alias(3)
  - yaml_depth(3), yaml_parser_state(3)
  - yaml_has_pending_events(3), yaml_event_count(3)
  - yaml_output_pending(3), yaml_output_size(3)
  - yaml_get_child_count(3), yaml_knowledge_depth(3)
Files: man/man3/ (existing directory)

### 8.2  Additional Manpages
Status: Partial (5 manpages exist, 8 more being created)
Additional manpages being created for newer API functions.

Files: man/man3/ (existing directory)

### 8.3  Example: Serializer Round-Trip
Status: Not implemented
Need an example showing parse → events → serialize → verify round-trip.
Files: examples/ (new file)

### 8.4 Example: Knowledge Tree Deep Lookup
Status: Not implemented
Once deep nesting (1.3) works, update the parse example to demonstrate
dot-path lookup for nested mappings.
Files: examples/yaml_parse_example.c

### 8.5 Example: Event-Loop Integration Patterns
Status: Not implemented
ADR 002 and 012 require compatibility with io_uring, libev, libuv, epoll,
ESP-IDF, coroutines, and RT environments. No examples exist demonstrating
these patterns.
Files: examples/ (new directory/files)

---

## 9. Code Quality / Internal Improvements

### 9.1 Ring Buffer Wrap-Around Bug in Output Append
Status: Potential bug
output_append writes byte-by-byte with modular wrap, but the initial
realloc growth check uses `ob->count + len > ob->capacity`. After a
realloc, the data may not be contiguous from read_pos to write_pos if
the ring was wrapped. Need to linearize the buffer before realloc.
Files: src/yaml.c (output_append)

### 9.2 Serializer Depth Tracking Mismatch
Status: Potential bug
yaml_emit_mapping_start/sequence_start increment ser_depth, and
yaml_emit_mapping_end/sequence_end decrement. But the parser's depth
tracking is separate and may diverge from serializer depth if events are
merged. Need unified depth tracking or per-role depth.
Files: src/yaml.c
