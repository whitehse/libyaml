# TODO — libyaml

Organized by category. Each item includes a rationale and references where applicable.
Items are roughly ordered by priority within each category.

---

## 1. Parser: Missing YAML Construct Support

### 1.1 Literal Block Scalars (|)
Status: Not implemented
Files: src/yaml.c (parse_input), include/yaml.h (YAML_SCALAR_LITERAL defined but unused)
The parser does not handle literal block scalars where `|` preserves newlines exactly.
Need to: detect `|` indicator after key or in sequence context, parse subsequent
indented lines as a single scalar with embedded newlines, emit with style=YAML_SCALAR_LITERAL.
AGENTS.md line 50, ARCHITECTURE.md line 68 both list this as missing.

### 1.2 Folded Block Scalars (>)
Status: Not implemented
Files: src/yaml.c (parse_input), include/yaml.h (YAML_SCALAR_FOLDED defined but unused)
The parser does not handle folded block scalars where `>` folds newlines into spaces.
Same implementation approach as 1.1 but with newline→space transformation logic.
AGENTS.md line 50, ARCHITECTURE.md line 68 both list this as missing.

### 1.3 Flow Sequence Parsing ([item1, item2])
Status: Not implemented
The parser does not recognize `[...]` flow sequence notation. Currently, flow indicators
like `[`, `]`, `{`, `}` are only checked in plain scalar termination. Need a flow context
stack that switches parsing mode when `[` or `{` is encountered.
Files: src/yaml.c (parse_input, parse_plain_scalar)

### 1.4 Flow Mapping Parsing ({key: value})
Status: Not implemented
Same as 1.3 but for `{...}` flow mapping notation.

### 1.5 Anchor Parsing (&anchor) on Scalars and Collections
Status: Partially stubbed
The `yaml_collection_start_t` has `anchor` and `anchor_len` fields. The parser never
populates them. Need to detect `&name` before a scalar or collection start and store
the anchor name in the event.
Files: src/yaml.c (parse_input), include/yaml.h (fields exist in structs)

### 1.6 Alias Resolution (*alias → value)
Status: Not implemented (events emitted but not resolved)
AGENTS.md line 52: "No anchor/alias resolution (aliases emitted as events but not resolved)."
The parser does not even emit YAML_EVENT_ALIAS events currently. Need to:
- Detect `*name` tokens and emit YAML_EVENT_ALIAS events
- Optionally: build an anchor table during parsing for caller-side resolution
- The library should at minimum emit the alias; resolution can be caller's responsibility
  (per "plumbing" philosophy), but a helper function would be useful
Files: src/yaml.c, include/yaml.h

### 1.7 Tag Parsing (!tag, !!tag)
Status: Not implemented
The `yaml_scalar_t` and `yaml_collection_start_t` have `tag`/`tag_len` fields but they
are never populated by the parser. Need to detect `!tag` and `!!tag` prefixes and fill
the tag field. `config.strict_mode` should reject unknown tags (or emit error events).
AGENTS.md line 51: "No YAML tag resolution beyond basic handling."
Files: src/yaml.c, include/yaml.h

### 1.8 Proper Indentation-Based Nesting
Status: Simplified heuristics, broken for nested structures
AGENTS.md line 53: "Indentation-based nesting uses simplified heuristics; edge cases
with mixed styles may not parse correctly."
The current parser uses `depth` as a flat counter but doesn't track actual indentation
levels. This breaks for:
  - Nested mappings (server: \n  host: x \n  port: y)
  - Mixed sequences of mappings
  - Mapping values that are collections themselves
Need: an indentation stack that records the column of each nesting level.
When a line's indent is less than the current level, pop levels and emit end events.
Files: src/yaml.c (parse_input), struct yaml_ctx (replace depth with indent_stack)

### 1.9 Proper State Machine for Mapping Key/Value Alternation
Status: Partially implemented
The parser tracks YAML_STATE_MAPPING_KEY and YAML_STATE_MAPPING_VALUE but doesn't
properly alternate between them. After a key: value pair, the state should return to
MAPPING_KEY for the next key. Current code stays in MAPPING_VALUE.
Files: src/yaml.c (parse_input, the key: value block at ~line 464)

### 1.10 Document End without Explicit Markers
Status: Not implemented
YAML allows an implicit document end when `---` starts a new document. Current code
handles this. But documents that simply end (EOF without `...`) need to emit proper
DOCUMENT_END, STREAM_END events.
Files: src/yaml.c (parse_input)

### 1.11 Multiline Scalar Continuation
Status: Not implemented
Plain scalars that span multiple lines (continuation lines at same or greater indent)
are not handled. Each line is treated as a separate scalar.
Files: src/yaml.c (parse_plain_scalar)

---

## 2. Serializer: Missing Features

### 2.1 Literal and Folded Scalar Style Output
Status: Not implemented
The serializer always outputs plain or double-quoted scalars. Even when
style=YAML_SCALAR_LITERAL or YAML_SCALAR_FOLDED is requested, the serializer
doesn't generate `|` or `>` block scalar output.
Need: detect the requested style in yaml_emit_scalar and produce proper block scalar
format with indentation indicators.
Files: src/yaml.c (yaml_emit_scalar, ~line 804)

### 2.2 Tag Output (!tag, !!tag)
Status: Not implemented
When a scalar or collection has a tag, the serializer should prepend `!tag ` or `!!type`
before the value. Currently tags are stored in events but never written to output.
Files: src/yaml.c (yaml_emit_scalar, yaml_emit_mapping_start, yaml_emit_sequence_start)

### 2.3 Anchor/Anchor+Alias Output
Status: Not implemented
yaml_emit_mapping_start/sequence_start accept anchor strings but the serializer never
writes `&anchor` in the output. Similarly, no way to emit `*alias` references.
Need: write `&name` after collection start indicators, add yaml_emit_alias() helper.
Files: src/yaml.c, include/yaml.h

### 2.4 Flow Style Output
Status: Not implemented
The serializer only produces block style. Need option for flow style:
`{key: value, key2: value2}` and `[item1, item2]`.
Could be controlled via a config option or per-event style hint.
Files: src/yaml.c

### 2.5 Proper Indentation in Serializer Output
Status: Not implemented
The serializer doesn't track or emit indentation for nested collections. All output
is flat. Need: maintain indent level, prepend spaces for nested mappings/sequences.
This is critical for producing valid YAML from the serializer.
Files: src/yaml.c (all emit_* functions)

### 2.6 Inline Key: Value in Serializer
Status: yaml_serialize_kv exists but bypasses event model
yaml_serialize_kv appends directly to the output buffer AND also enqueues events,
creating a double-write. The key is emitted via yaml_emit_scalar (which writes to
output) and then ": " is appended. Need to choose: either events-only (caller drains
output) or direct-write-only (no event queueing).
Files: src/yaml.c (yaml_serialize_kv, ~line 967)

---

## 3. Knowledge Tree

### 3.1 Deep Nested Key-Path Lookup with Sequence Intermediates
Status: Not implemented
AGENTS.md line 54: "Knowledge tree supports flat and simple nested key paths; deep
nesting with sequences as intermediate nodes needs work."
Currently, the knowledge tree only tracks scalar children of the current mapping.
Sequence items and nested mapping keys at depth>1 are not indexed.
Need: track nesting context during parsing and insert into the knowledge tree at
the correct depth, including sequence index paths (e.g., "items.0.name").
Files: src/yaml.c (parse_input knowledge update block, ~line 544)

### 3.2 Knowledge Tree Enumeration/Iteration
Status: Not implemented
No API to list all keys at a path or iterate children. Useful for discovering
structure without knowing key names in advance.
Suggested: yaml_enumerate_keys(ctx, path, callback) or yaml_get_child_count(ctx, path)
Files: include/yaml.h, src/yaml.c

### 3.3 Knowledge Tree Statistics
Status: Not implemented
No API to get tree size, depth, or node count. Useful for diagnostics.
Files: include/yaml.h, src/yaml.c

---

## 4. Error Handling

### 4.1 Proper YAML_EVENT_ERROR with Descriptive Messages
Status: Partially defined
The YAML_EVENT_ERROR type exists in the enum and the event struct has
error.message/message_len fields, but the parser never emits error events.
Need: detect malformed YAML and emit YAML_EVENT_ERROR with a descriptive message.
The error.message field is a `const char *` pointer which is NOT copy-safe in the
ring buffer (it will become dangling). Need to change to an embedded char array
or string table approach.
Files: include/yaml.h (error struct), src/yaml.c

### 4.2 Error Event Struct Fix (Pointer vs Embedded Array)
Status: Design issue
The error event uses `const char *message` which is not safe in the ring buffer
(same issue noted in ARCHITECTURE.md: "All event payload structs use embedded arrays
(not pointers) for copy-safety in ring buffers"). Need embedded char array for
error messages, like YAML_MAX_SCALAR_LEN for scalars.
Files: include/yaml.h (line 108-111)

### 4.3 Strict Mode Tag Validation
Status: Config field exists but unused
config.strict_mode is declared but never checked. When strict_mode=1 and an unknown
tag is encountered, the parser should emit YAML_EVENT_ERROR.
Files: src/yaml.c (parse_input)

### 4.4 max_document_size Enforcement
Status: Config field exists but unused
config.max_document_size is declared and stored but never checked during parsing.
If accumulated input exceeds this limit, the parser should emit an error event.
Files: src/yaml.c (yaml_feed_input, parse_input)

---

## 5. Event Queue and Backpressure

### 5.1 Event Queue Backpressure / Notification
Status: Not implemented
When the queue is full, queue_push returns -1 silently. The parser continues but
events are lost. Need one of:
  a) Caller-configurable policy: block (not compatible with no-blocking rule), drop
     oldest, drop newest, or error
  b) yaml_feed_input should return fewer bytes consumed so caller can retry
  c) Emit an event indicating loss/overflow
  d) Auto-grow queue (configurable)
ARCHITECTURE.md line 72 lists this as a future item.
Files: src/yaml.c (queue_push, yaml_feed_input)

### 5.2 Queue Overflow Notification to Caller
Status: Not implemented
No API to query queue status (full, empty, available slots). Callers cannot make
informed decisions about backpressure.
Suggested: yaml_queue_status(ctx) → {available, capacity, overflowed}
Files: include/yaml.h, src/yaml.c

---

## 6. Incremental / Streaming Parsing

### 6.1 Line-Boundary Awareness in Incremental Feed
Status: Partially working but fragile
The parser processes all accumulated input on each feed_input call. If input ends
mid-line, the parser may produce partial events or miss tokens. Need to track
"complete line" boundaries and only parse complete lines, buffering partial lines
across feed_input calls.
Files: src/yaml.c (parse_input, ~line 331)

### 6.2 Partial Token Buffering Across Feed Calls
Status: Not implemented
Similar to 6.1: quoted scalars that span multiple feed_input calls are not handled.
The opening quote is in one call, content in the next. Need to track "in-quote" state
across calls.
Files: src/yaml.c

---

## 7. UTF-8 and Unicode

### 7.1 Unicode Escape Sequences in Double-Quoted Scalars
Status: Not implemented
The parser handles basic escapes (\n, \t, \\, \", \') but not:
  - \uXXXX (4-digit Unicode)
  - \UXXXXXXXX (8-digit Unicode)
  - \xXX (2-digit hex)
  - Other YAML escape sequences per spec
Files: src/yaml.c (parse_quoted_scalar, ~line 311)

### 7.2 UTF-8 Validation
Status: Not implemented
No validation that input is valid UTF-8. The YAML 1.2 spec requires valid UTF-8
(or UTF-16/32 with BOM). Need at minimum a UTF-8 validity check on input bytes.
Files: src/yaml.c

---

## 8. Testing

### 8.1 Valgrind Test Targets for All Test Binaries
Status: Partially implemented
CMakeLists.txt has Valgrind targets for smoke and dialectic tests but NOT for
errors and knowledge tests. Need to add:
  - yaml_errors_valgrind
  - yaml_knowledge_valgrind
Files: CMakeLists.txt (~line 62)

### 8.2 Serializer Fuzz Harness
Status: Not implemented
Only a parser fuzz harness exists (fuzz/fuzz_yaml.c). Need a serializer fuzz harness
that feeds random event sequences and checks for crashes, memory errors, and
valid YAML output.
Files: fuzz/ (new file), CMakeLists.txt

### 8.3 Fuzz Harness for Knowledge Tree
Status: Not implemented
The existing fuzz harness only does a single lookup_scalar("test"). Need more
thorough fuzzing of the knowledge tree: random key paths, deep nesting, resets.
Files: fuzz/fuzz_yaml.c

### 8.4 Tests for Block Scalars (once implemented)
Status: Needed with 1.1/1.2
Test literal (|) and folded (>) block scalar parsing, including multiline content,
chomp indicators (-/+, +/-), and indentation indicators.
Files: tests/ (new test file)

### 8.5 Tests for Flow Collections (once implemented)
Status: Needed with 1.3/1.4
Test [a, b, c] and {k: v} flow collection parsing.
Files: tests/ (new test file)

### 8.6 Tests for Anchor/Alias (once implemented)
Status: Needed with 1.5/1.6
Test anchor definition, alias emission, and round-trip with anchored values.
Files: tests/ (new test file)

### 8.7 Tests for Nested Mappings
Status: Partially covered
The deeply nested test in test_yaml_errors.c tests serializer nesting but there's
no test for PARSING nested mappings like:
```yaml
server:
  host: localhost
  port: 8080
```
Need parser tests for multi-level indentation.
Files: tests/

### 8.8 Tests for Mixed Sequences and Mappings
Status: Not implemented
No tests for sequences containing mappings:
```yaml
- name: first
  value: 1
- name: second
  value: 2
```
Files: tests/

### 8.9 Tests for Edge Cases in Quoted Scalars
Status: Minimal
Need tests for: empty quoted strings, escaped quotes within quotes, multi-character
escape sequences, quotes at end of input, unclosed quotes.
Files: tests/

### 8.10 Round-Trip Fidelity Test
Status: Partial
The dialectic round-trip tests check for substring presence but not exact byte-for-byte
equality. Need a true round-trip test: parse → serialize → parse again → compare events.
Files: tests/test_yaml_dialectic.c

---

## 9. API Surface Improvements

### 9.1 yaml_parser_state() Getter
Status: Not implemented
No way for caller to query parser state (IDLE, STREAM, DOCUMENT, etc.).
Useful for detecting incomplete parsing (e.g., no STREAM_END received).
Files: include/yaml.h, src/yaml.c

### 9.2 yaml_depth() Getter
Status: Not implemented
No way to query current nesting depth. Useful for progress tracking.
Files: include/yaml.h, src/yaml.c

### 9.3 yaml_emit_alias() Helper
Status: Not implemented
No serializer helper to emit an alias event (*name). The event type exists
(YAML_EVENT_ALIAS) but no corresponding emit function.
Files: include/yaml.h, src/yaml.c

### 9.4 yaml_has_pending_events() / yaml_event_count()
Status: Not implemented
No convenience function to check if events are available without dequeuing.
Suggested: int yaml_has_pending(ctx) or size_t yaml_event_count(ctx)
Files: include/yaml.h, src/yaml.c

### 9.5 yaml_output_pending() / yaml_output_size()
Status: Not implemented
No way to query output buffer size without draining. Useful for knowing
how large a buffer to allocate.
Files: include/yaml.h, src/yaml.c

---

## 10. Build and Infrastructure

### 10.1 Shared Library Build Option
Status: Not implemented
CMakeLists.txt only builds a static library. Need a BUILD_SHARED_LIBS option
for .so/.dylib/.dll generation.
Files: CMakeLists.txt

### 10.2 pkg-config File Generation
Status: Not implemented
No .pc file generated for easy integration with non-CMake build systems.
Files: CMakeLists.txt, yaml.pc.in (new)

### 10.3 CMake Version Header Generation
Status: Not implemented
No yaml_version.h generated with version macros. Callers cannot check library
version at compile time.
Files: CMakeLists.txt, include/yaml_version.h.in (new)

### 10.4 Sanitizer CI Targets
Status: Not implemented
No AddressSanitizer or UBSan targets beyond the fuzz build. Need CMake options
for -fsanitize=address and -fsanitize=undefined on test binaries.
Files: CMakeLists.txt

---

## 11. Documentation

### 11.1 Manpages for Public API
Status: Not implemented
ARCHITECTURE.md line 77 references man/man3/ manpages but none exist.
Need manpage files documenting each public function's parameters, return values,
error conditions, and thread safety guarantees.
Files: man/man3/ (new directory)

### 11.2 ADR Cleanup — pqwire References
Status: Stale references
Multiple ADRs (002, 003, 004, 006, 009, 010) still reference pqwire_* APIs and
PostgreSQL-specific functionality. These were inherited from a sibling project.
Should be updated to reference yaml_* APIs and YAML-specific context.
Files: docs/decisions/002-*.md through 010-*.md

### 11.3 ADR 005 — Missing
Status: Gap
ADR numbering goes from 004 to 006. ADR 005 is missing. Should either be created
for a relevant decision or the gap documented.
Files: docs/decisions/

### 11.4 Example: Serializer Round-Trip
Status: Not implemented
The existing example (yaml_parse_example.c) only shows parsing and separate
serialization. Need an example showing parse → events → serialize → verify round-trip.
Files: examples/ (new file)

### 11.5 Example: Knowledge Tree Deep Lookup
Status: Not implemented
The existing example notes that dot-path lookup for nested mappings needs the
"full indentation-aware parser." Once nesting works, update the example.
Files: examples/yaml_parse_example.c

### 11.6 Example: Event-Loop Integration Patterns
Status: Not implemented
ADR 002 and 012 require compatibility with io_uring, libev, libuv, epoll, ESP-IDF,
coroutines, and RT environments. No examples exist demonstrating these patterns.
Files: examples/ (new directory/files)

---

## 12. Code Quality / Internal Improvements

### 12.1 Input Buffer Growth Without max_document_size Check
Status: Bug
yaml_feed_input grows the input buffer without bound (line 679-686). The
max_document_size config is never checked. A long input stream will consume
unbounded memory.
Files: src/yaml.c (yaml_feed_input)

### 12.2 Ring Buffer Wrap-Around Bug in Output Append
Status: Potential bug
output_append writes byte-by-byte with modular wrap (line 147-150), but the
initial realloc growth check uses `ob->count + len > ob->capacity`. After
a realloc, the data may not be contiguous from read_pos to write_pos if the
ring was wrapped. Need to linearize the buffer before realloc.
Files: src/yaml.c (output_append, ~line 136)

### 12.3 Unused Variable in Serializer
Status: Code smell
In yaml_emit_scalar, `char esc[3]` is declared at line 846 but never used
(marked with `(void)esc`). Should be removed.
Files: src/yaml.c (line 846, 854)

### 12.4 parse_input Removes All Consumed Input
Status: Potential issue
After parsing, `pos` bytes are removed from the input buffer via memmove (line 586-589).
If the parser stops mid-line (e.g., incomplete token), valid bytes are lost. The parser
should only remove bytes up to the last complete token/line boundary.
Files: src/yaml.c (parse_input, ~line 586)

### 12.5 Serializer Depth Tracking Mismatch
Status: Potential bug
yaml_emit_mapping_start/sequence_start increment depth (lines 776, 800), and
yaml_emit_mapping_end/sequence_end decrement (lines 870, 880). But the parser's
depth tracking (lines 430, 488) is separate and may diverge from serializer depth
if events are merged. Need unified depth tracking or per-role depth.
Files: src/yaml.c

---

## Priority Summary

Highest priority (core functionality gaps):
  1.8  Proper indentation-based nesting (blocks many other features)
  1.1/1.2  Block scalar support (| and >)
  4.2  Error event struct fix (pointer safety in ring buffer)
  12.1 max_document_size enforcement
  12.2 Ring buffer wrap-around bug

High priority (completeness):
  1.5/1.6  Anchor/alias support
  1.7  Tag parsing
  2.5  Proper serializer indentation
  8.7/8.8  Parser tests for nesting

Medium priority (feature richness):
  1.3/1.4  Flow collections
  2.1/2.2  Block scalar and tag output in serializer
  5.1    Backpressure handling
  6.1    Line-boundary awareness
  10.*   Build infrastructure

Lower priority (polish and docs):
  7.*    Unicode support
  9.*    API surface helpers
  11.*   Documentation and examples
