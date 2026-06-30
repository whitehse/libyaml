#ifndef YAML_H
#define YAML_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Role: parser or serializer --- */
typedef enum {
    YAML_ROLE_PARSER = 0,
    YAML_ROLE_SERIALIZER
} yaml_role_t;

/* --- Config struct (ADR 009 consistent interface) --- */
typedef struct {
    size_t event_queue_size;    /* 0 = use default (8) */
    size_t max_document_size;   /* 0 = use default (1 MiB) */
    int    strict_mode;         /* 1 = reject unknown YAML tags, 0 = lenient */
    int    flow_output;         /* 1 = flow style output ({}, []), 0 = block style */
} yaml_config_t;

/* --- Opaque context --- */
typedef struct yaml_ctx yaml_ctx_t;

/* --- Event types --- */
typedef enum {
    YAML_EVENT_NONE = 0,

    /* Stream events */
    YAML_EVENT_STREAM_START,
    YAML_EVENT_STREAM_END,

    /* Document events */
    YAML_EVENT_DOCUMENT_START,
    YAML_EVENT_DOCUMENT_END,

    /* Mapping events */
    YAML_EVENT_MAPPING_START,
    YAML_EVENT_MAPPING_END,

    /* Sequence events */
    YAML_EVENT_SEQUENCE_START,
    YAML_EVENT_SEQUENCE_END,

    /* Scalar event */
    YAML_EVENT_SCALAR,

    /* Alias event */
    YAML_EVENT_ALIAS,

    /* Error */
    YAML_EVENT_ERROR,

    YAML_EVENT_MAX
} yaml_event_type_t;

/* --- Scalar style --- */
typedef enum {
    YAML_SCALAR_PLAIN = 0,
    YAML_SCALAR_SINGLE_QUOTED,
    YAML_SCALAR_DOUBLE_QUOTED,
    YAML_SCALAR_LITERAL,        /* | */
    YAML_SCALAR_FOLDED          /* > */
} yaml_scalar_style_t;

/* --- Scalar data (embedded arrays, copy-safe per pure-state-machine patterns) --- */
#define YAML_MAX_SCALAR_LEN  4096
#define YAML_MAX_TAG_LEN     256
#define YAML_MAX_ALIAS_LEN   256
#define YAML_MAX_ERROR_LEN   512

typedef struct {
    char    value[YAML_MAX_SCALAR_LEN];
    size_t  value_len;
    char    tag[YAML_MAX_TAG_LEN];
    size_t  tag_len;
    yaml_scalar_style_t style;
} yaml_scalar_t;

/* --- Alias data --- */
typedef struct {
    char    anchor[YAML_MAX_ALIAS_LEN];
    size_t  anchor_len;
} yaml_alias_t;

/* --- Document boundary --- */
typedef struct {
    int implicit;   /* 1 if the --- was omitted */
} yaml_document_start_t;

/* --- Mapping/sequence start with optional tag and anchor --- */
typedef struct {
    char    tag[YAML_MAX_TAG_LEN];
    size_t  tag_len;
    char    anchor[YAML_MAX_ALIAS_LEN];
    size_t  anchor_len;
} yaml_collection_start_t;

/* --- Main event struct --- */
typedef struct {
    yaml_event_type_t type;
    union {
        yaml_scalar_t           scalar;
        yaml_alias_t            alias;
        yaml_document_start_t   document_start;
        yaml_collection_start_t collection_start;
        struct {
            char    message[YAML_MAX_ERROR_LEN];
            size_t  message_len;
        } error;
    } data;
} yaml_event_t;

/* --- Core API (consistent with ADR 009) --- */
yaml_ctx_t *yaml_create(yaml_role_t role);
yaml_ctx_t *yaml_create_with_config(yaml_role_t role, const yaml_config_t *config);
void        yaml_destroy(yaml_ctx_t *ctx);
void        yaml_reset(yaml_ctx_t *ctx);

/* Feed raw YAML text (parser) or event data (serializer) into the context.
 * Returns number of bytes consumed. */
size_t yaml_feed_input(yaml_ctx_t *ctx, const uint8_t *data, size_t len);

/* Pull the next event. Returns 1 if an event was dequeued, 0 otherwise.
 * Primary output mechanism for both parser and serializer roles. */
int yaml_next_event(yaml_ctx_t *ctx, yaml_event_t *event);

/* Drain pending output bytes. Returns bytes copied (0 if none).
 * For parser: no output (returns 0).
 * For serializer: YAML text bytes. */
size_t yaml_get_output(yaml_ctx_t *ctx, uint8_t *buf, size_t max_len);

/* --- Serializer helpers --- */

/* Emit a stream start event. Returns 0 on success, -1 on error. */
int yaml_emit_stream_start(yaml_ctx_t *ctx);

/* Emit a document start event. implicit=1 omits the '---' marker. */
int yaml_emit_document_start(yaml_ctx_t *ctx, int implicit);

/* Emit a mapping start event with optional tag/anchor (NULLs for none). */
int yaml_emit_mapping_start(yaml_ctx_t *ctx, const char *tag, const char *anchor);

/* Emit a sequence start event with optional tag/anchor (NULLs for none). */
int yaml_emit_sequence_start(yaml_ctx_t *ctx, const char *tag, const char *anchor);

/* Emit a scalar event. style=YAML_SCALAR_PLAIN for auto-selection. */
int yaml_emit_scalar(yaml_ctx_t *ctx, const char *value, size_t value_len,
                     const char *tag, yaml_scalar_style_t style);

/* Emit end events for collections, documents, and streams. */
int yaml_emit_mapping_end(yaml_ctx_t *ctx);
int yaml_emit_sequence_end(yaml_ctx_t *ctx);
int yaml_emit_document_end(yaml_ctx_t *ctx, int implicit);
int yaml_emit_stream_end(yaml_ctx_t *ctx);

/* Emit an alias event (*name). Returns 0 on success, -1 on error. */
int yaml_emit_alias(yaml_ctx_t *ctx, const char *name);

/* --- Knowledge / Merge helpers --- */

/* Merge two YAML event streams. Feeds src events into dst serializer context.
 * Caller provides both contexts. Returns number of events merged, or -1 on error.
 * This is pure plumbing: no I/O, just event forwarding. */
int yaml_merge_events(yaml_ctx_t *dst, yaml_ctx_t *src);

/* Lookup a scalar value by key path in a parsed YAML event stream.
 * path is a dot-separated key string (e.g. "server.port").
 * Returns pointer to internal scalar value (valid until next feed_input),
 * or NULL if not found. Caller must NOT free. */
const char *yaml_lookup_scalar(yaml_ctx_t *ctx, const char *path,
                               size_t *out_len);

/* Serialize a scalar value to a YAML key-value pair into the output buffer.
 * Returns 0 on success, -1 on error. */
int yaml_serialize_kv(yaml_ctx_t *ctx, const char *key, const char *value,
                      size_t value_len);

/* --- Status / Query API --- */

/* Returns the current parser state as a string (e.g. "IDLE", "STREAM", "DOCUMENT", "DONE").
 * Returns NULL if ctx is NULL or role is not PARSER. */
const char *yaml_parser_state(yaml_ctx_t *ctx);

/* Returns current nesting depth (0 = top level). For parser: indent_depth.
 * For serializer: ser_depth. Returns -1 if ctx is NULL. */
int yaml_depth(yaml_ctx_t *ctx);

/* Returns 1 if there are events in the queue waiting to be dequeued, 0 otherwise. */
int yaml_has_pending_events(yaml_ctx_t *ctx);

/* Returns the number of events currently in the queue. */
size_t yaml_event_count(yaml_ctx_t *ctx);

/* Returns the number of bytes currently in the output buffer (serializer only).
 * Returns 0 for parser role or if ctx is NULL. */
size_t yaml_output_pending(yaml_ctx_t *ctx);

/* Returns the number of bytes currently in the output buffer (alias).
 * Returns 0 if ctx is NULL. */
size_t yaml_output_size(yaml_ctx_t *ctx);

/* Query queue status. Returns available slots, or -1 if ctx is NULL. */
int yaml_queue_available(yaml_ctx_t *ctx);

/* Returns queue capacity, or 0 if ctx is NULL. */
size_t yaml_queue_capacity(yaml_ctx_t *ctx);

/* --- Knowledge tree enumeration --- */

/* Returns the number of direct children at the given path, or 0 if not found.
 * Pass empty string "" for root children. */
size_t yaml_get_child_count(yaml_ctx_t *ctx, const char *path);

/* Gets the key name of the Nth child at the given path.
 * Returns 0 on success, -1 if not found or out of range.
 * out_key is set to point to internal storage (valid until next feed_input/reset). */
int yaml_get_child_key(yaml_ctx_t *ctx, const char *path, size_t index,
                       const char **out_key, size_t *out_key_len);

/* --- Knowledge tree statistics --- */

/* Returns the total number of key-value pairs stored in the knowledge tree. */
size_t yaml_knowledge_size(yaml_ctx_t *ctx);

/* Returns the maximum depth of the knowledge tree. */
size_t yaml_knowledge_depth(yaml_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* YAML_H */
