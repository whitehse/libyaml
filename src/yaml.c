/* yaml.c — Pure C YAML parser/serializer state machine
 *
 * Pure plumbing: no syscalls, no callbacks, no hidden I/O.
 * All state transitions driven by input buffers and caller context.
 * Follows ADR 006, ADR 009, ADR 010 patterns from sibling libraries.
 */

#include "yaml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define DEFAULT_EVENT_QUEUE_SIZE 8
#define DEFAULT_MAX_DOCUMENT_SIZE (1024 * 1024)

/* --- Ring buffer for events --- */
typedef struct {
    yaml_event_t *events;
    size_t        capacity;
    size_t        head;
    size_t        tail;
    size_t        count;
} event_queue_t;

/* --- Output buffer for serializer --- */
typedef struct {
    uint8_t *data;
    size_t   capacity;
    size_t   write_pos;
    size_t   read_pos;
    size_t   count;
} output_buffer_t;

/* --- Internal parser state --- */
typedef enum {
    YAML_STATE_IDLE = 0,
    YAML_STATE_STREAM,
    YAML_STATE_DOCUMENT,
    YAML_STATE_MAPPING_KEY,
    YAML_STATE_MAPPING_VALUE,
    YAML_STATE_SEQUENCE,
    YAML_STATE_DONE,
    YAML_STATE_ERROR
} yaml_state_t;

/* --- Knowledge node for scalar lookup --- */
typedef struct knowledge_node_s {
    char   *key;
    char   *value;
    size_t  value_len;
    struct knowledge_node_s *children;
    size_t  child_count;
    size_t  child_capacity;
} knowledge_node_t;

/* --- Indent tracking for nested structures --- */
#define YAML_MAX_INDENT_LEVELS 64

typedef enum {
    INDENT_NONE = 0,
    INDENT_MAPPING,
    INDENT_SEQUENCE
} indent_type_t;

/* --- Main context --- */
struct yaml_ctx {
    yaml_role_t       role;
    yaml_config_t     config;
    yaml_state_t      state;
    event_queue_t     queue;
    output_buffer_t   output;

    /* Parser: accumulated input buffer */
    uint8_t          *input_buf;
    size_t            input_len;
    size_t            input_capacity;

    /* Knowledge tree root for lookup */
    knowledge_node_t  knowledge_root;

    /* Indentation-based nesting: stack of indent column positions */
    int               indent_stack[YAML_MAX_INDENT_LEVELS];
    indent_type_t     indent_types[YAML_MAX_INDENT_LEVELS];
    int               indent_depth;

    /* Serializer depth tracking (separate from parser indent stack) */
    int               ser_depth;

    /* Serializer indentation tracking */
    int               ser_indent;       /* current indent level (spaces = ser_indent * 2) */
    int               ser_at_line_start; /* 1 if next content should be preceded by indent */
    int               ser_expect_value; /* 1 if next scalar is a value (preceded by ": ") */
    indent_type_t     ser_indent_types[YAML_MAX_INDENT_LEVELS]; /* mapping/sequence at each level */

    /* Pending anchor for next collection start event */
    char              pending_anchor[YAML_MAX_ALIAS_LEN];
    size_t            pending_anchor_len;

    /* Deep path tracking for knowledge tree */
    char              indent_keys[YAML_MAX_INDENT_LEVELS][256];
    int               indent_key_lens[YAML_MAX_INDENT_LEVELS];
    size_t            indent_seq_idx[YAML_MAX_INDENT_LEVELS];

    /* Incremental parsing: track partial lines across feed calls */
    int               in_quoted_scalar;       /* 1 if inside a quoted scalar spanning feeds */
    size_t            last_complete_pos;      /* last complete line boundary position */
};

/* --- Event queue operations --- */

static int queue_init(event_queue_t *q, size_t capacity)
{
    q->events = (yaml_event_t *)calloc(capacity, sizeof(yaml_event_t));
    if (!q->events) return -1;
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return 0;
}

static void queue_cleanup(event_queue_t *q)
{
    free(q->events);
    q->events = NULL;
    q->capacity = 0;
    q->head = q->tail = q->count = 0;
}

static int queue_push(event_queue_t *q, const yaml_event_t *ev)
{
    if (q->count >= q->capacity) return -1;
    q->events[q->tail] = *ev;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    return 0;
}

static int queue_pop(event_queue_t *q, yaml_event_t *ev)
{
    if (q->count == 0) return 0;
    *ev = q->events[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return 1;
}

/* --- Output buffer operations --- */

static int output_init(output_buffer_t *ob, size_t capacity)
{
    ob->data = (uint8_t *)malloc(capacity);
    if (!ob->data) return -1;
    ob->capacity = capacity;
    ob->write_pos = 0;
    ob->read_pos = 0;
    ob->count = 0;
    return 0;
}

static void output_cleanup(output_buffer_t *ob)
{
    free(ob->data);
    ob->data = NULL;
    ob->capacity = 0;
    ob->write_pos = ob->read_pos = ob->count = 0;
}

static int output_append(output_buffer_t *ob, const uint8_t *data, size_t len)
{
    if (ob->count + len > ob->capacity) {
        /* Linearize if wrapped before realloc */
        if (ob->count > 0 && ob->read_pos > ob->write_pos) {
            size_t first_chunk = ob->capacity - ob->read_pos;
            uint8_t *tmp = (uint8_t *)malloc(ob->count);
            if (!tmp) return -1;
            memcpy(tmp, ob->data + ob->read_pos, first_chunk);
            memcpy(tmp + first_chunk, ob->data, ob->write_pos);
            memcpy(ob->data, tmp, ob->count);
            free(tmp);
            ob->read_pos = 0;
            ob->write_pos = ob->count;
        }
        /* Grow buffer */
        size_t new_cap = ob->capacity * 2;
        while (new_cap < ob->count + len) new_cap *= 2;
        uint8_t *new_data = (uint8_t *)realloc(ob->data, new_cap);
        if (!new_data) return -1;
        ob->data = new_data;
        ob->capacity = new_cap;
    }
    for (size_t i = 0; i < len; i++) {
        ob->data[ob->write_pos] = data[i];
        ob->write_pos = (ob->write_pos + 1) % ob->capacity;
    }
    ob->count += len;
    return 0;
}

static size_t output_drain(output_buffer_t *ob, uint8_t *buf, size_t max_len)
{
    size_t to_copy = ob->count < max_len ? ob->count : max_len;
    for (size_t i = 0; i < to_copy; i++) {
        buf[i] = ob->data[ob->read_pos];
        ob->read_pos = (ob->read_pos + 1) % ob->capacity;
    }
    ob->count -= to_copy;
    return to_copy;
}

/* --- Knowledge tree helpers --- */

static void knowledge_node_cleanup(knowledge_node_t *node)
{
    if (!node) return;
    free(node->key);
    free(node->value);
    for (size_t i = 0; i < node->child_count; i++) {
        knowledge_node_cleanup(&node->children[i]);
    }
    free(node->children);
    node->key = NULL;
    node->value = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
}

static knowledge_node_t *knowledge_find_or_create(knowledge_node_t *parent,
                                                  const char *key, size_t key_len)
{
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i].key &&
            strlen(parent->children[i].key) == key_len &&
            memcmp(parent->children[i].key, key, key_len) == 0) {
            return &parent->children[i];
        }
    }
    /* Create new child */
    if (parent->child_count >= parent->child_capacity) {
        size_t new_cap = parent->child_capacity == 0 ? 8 : parent->child_capacity * 2;
        knowledge_node_t *new_children = (knowledge_node_t *)realloc(
            parent->children, new_cap * sizeof(knowledge_node_t));
        if (!new_children) return NULL;
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }
    knowledge_node_t *child = &parent->children[parent->child_count];
    memset(child, 0, sizeof(*child));
    child->key = (char *)malloc(key_len + 1);
    if (!child->key) return NULL;
    memcpy(child->key, key, key_len);
    child->key[key_len] = '\0';
    parent->child_count++;
    return child;
}

static const knowledge_node_t *knowledge_lookup(const knowledge_node_t *root,
                                                 const char *path, size_t *out_len)
{
    if (!root || !path) return NULL;

    const char *seg_start = path;
    const knowledge_node_t *current = root;

    while (*seg_start) {
        const char *seg_end = seg_start;
        while (*seg_end && *seg_end != '.') seg_end++;
        size_t seg_len = (size_t)(seg_end - seg_start);

        int found = 0;
        for (size_t i = 0; i < current->child_count; i++) {
            if (current->children[i].key &&
                strlen(current->children[i].key) == seg_len &&
                memcmp(current->children[i].key, seg_start, seg_len) == 0) {
                current = &current->children[i];
                found = 1;
                break;
            }
        }
        if (!found) return NULL;

        seg_start = seg_end;
        if (*seg_start == '.') seg_start++;
    }

    if (out_len) *out_len = current->value_len;
    return current;
}

/* Build full dot-separated path from current indent state.
 * Walks indent levels 0..indent_depth-1, using keys for mappings
 * and numeric indices for sequences. */
static int build_knowledge_path(const yaml_ctx_t *ctx, char *out, size_t max_out)
{
    size_t pos = 0;
    for (int i = 0; i < ctx->indent_depth; i++) {
        if (i > 0 && pos < max_out - 1) {
            out[pos++] = '.';
        }
        if (ctx->indent_types[i] == INDENT_SEQUENCE) {
            char idx_buf[32];
            int n = snprintf(idx_buf, sizeof(idx_buf), "%zu", ctx->indent_seq_idx[i]);
            if (n > 0) {
                size_t written = (size_t)n;
                if (pos + written < max_out) {
                    memcpy(out + pos, idx_buf, written);
                    pos += written;
                }
            }
        } else {
            size_t len = (size_t)ctx->indent_key_lens[i];
            if (len > 0 && pos + len < max_out) {
                memcpy(out + pos, ctx->indent_keys[i], len);
                pos += len;
            }
        }
    }
    if (pos < max_out) out[pos] = '\0';
    else if (max_out > 0) out[max_out - 1] = '\0';
    return (int)pos;
}

/* Insert a value at a dot-separated path in the knowledge tree.
 * Creates intermediate nodes as needed. */
static knowledge_node_t *knowledge_insert_path(knowledge_node_t *root,
                                                const char *path,
                                                const char *value, size_t value_len)
{
    knowledge_node_t *current = root;
    const char *seg = path;

    while (*seg) {
        const char *dot = strchr(seg, '.');
        size_t seg_len = dot ? (size_t)(dot - seg) : strlen(seg);

        current = knowledge_find_or_create(current, seg, seg_len);
        if (!current) return NULL;

        seg += seg_len;
        if (*seg == '.') seg++;
    }

    /* Set value on the final node */
    free(current->value);
    current->value = (char *)malloc(value_len + 1);
    if (current->value) {
        memcpy(current->value, value, value_len);
        current->value[value_len] = '\0';
        current->value_len = value_len;
    }
    return current;
}

/* Recursively count nodes with values */
static void knowledge_count_values(const knowledge_node_t *node, size_t *count)
{
    if (!node) return;
    if (node->value) (*count)++;
    for (size_t i = 0; i < node->child_count; i++) {
        knowledge_count_values(&node->children[i], count);
    }
}

/* Recursively find maximum depth */
static void knowledge_max_depth(const knowledge_node_t *node, size_t depth, size_t *max_d)
{
    if (!node) return;
    if (depth > *max_d) *max_d = depth;
    for (size_t i = 0; i < node->child_count; i++) {
        knowledge_max_depth(&node->children[i], depth + 1, max_d);
    }
}

/* --- YAML character classification helpers --- */

static int yaml_is_whitespace(uint8_t c)
{
    return c == ' ' || c == '\t';
}

static int yaml_is_newline(uint8_t c)
{
    return c == '\n' || c == '\r';
}

static int yaml_is_flow_indicator(uint8_t c)
{
    return c == ',' || c == '[' || c == ']' || c == '{' || c == '}';
}

/* Skip whitespace (not newlines) at position, return new position */
static size_t skip_spaces(const uint8_t *data, size_t len, size_t pos)
{
    while (pos < len && yaml_is_whitespace(data[pos])) pos++;
    return pos;
}

/* Skip to end of line, return position after newline */
static size_t skip_to_eol(const uint8_t *data, size_t len, size_t pos)
{
    while (pos < len && !yaml_is_newline(data[pos])) pos++;
    if (pos < len) {
        if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') pos++;
        pos++;
    }
    return pos;
}

/* --- Error emission helper --- */

static void emit_error(yaml_ctx_t *ctx, const char *msg)
{
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_ERROR;
    size_t msg_len = strlen(msg);
    if (msg_len >= YAML_MAX_ERROR_LEN) msg_len = YAML_MAX_ERROR_LEN - 1;
    memcpy(ev.data.error.message, msg, msg_len);
    ev.data.error.message[msg_len] = '\0';
    ev.data.error.message_len = msg_len;
    queue_push(&ctx->queue, &ev);
}

/* --- Strict mode tag validation --- */

/* Returns 1 if tag is a standard YAML tag (!, !!, !<...>), 0 otherwise.
 * Standard tags: !, !!, !<uri>, !letter..., !!letter... */
static int is_standard_tag(const char *tag, size_t tag_len)
{
    if (tag_len == 0) return 0;
    if (tag[0] != '!') return 0;

    /* Bare "!" is valid */
    if (tag_len == 1) return 1;

    /* "!!" is valid */
    if (tag_len == 2 && tag[1] == '!') return 1;

    /* "!<...>" is valid (verbatim tag) */
    if (tag[1] == '<') return 1;

    /* "!!" prefix followed by alphanumeric */
    if (tag_len > 2 && tag[1] == '!') {
        /* !!something */
        return 1;
    }

    /* "!letter..." is valid */
    if (tag_len > 1 && ((tag[1] >= 'a' && tag[1] <= 'z') ||
                         (tag[1] >= 'A' && tag[1] <= 'Z'))) {
        return 1;
    }

    return 0;
}

/* --- Parser: extract a plain scalar until newline or flow indicator --- */
static size_t parse_plain_scalar(const uint8_t *data, size_t len, size_t pos,
                                  char *out, size_t max_out, size_t *out_len)
{
    size_t wpos = 0;
    while (pos < len && !yaml_is_newline(data[pos]) &&
           !yaml_is_flow_indicator(data[pos]) && data[pos] != ':' &&
           data[pos] != '#' && data[pos] != '&' && data[pos] != '*' &&
           data[pos] != '!') {
        if (wpos < max_out - 1) {
            out[wpos++] = (char)data[pos];
        }
        pos++;
    }
    /* Trim trailing whitespace */
    while (wpos > 0 && yaml_is_whitespace((uint8_t)out[wpos - 1])) wpos--;
    out[wpos] = '\0';
    *out_len = wpos;
    return pos;
}

/* --- Parser: extract an anchor name after '&' --- */
static size_t parse_anchor(const uint8_t *data, size_t len, size_t pos,
                            char *out, size_t max_out, size_t *out_len)
{
    size_t wpos = 0;
    pos++; /* skip '&' */
    while (pos < len && !yaml_is_whitespace(data[pos]) &&
           !yaml_is_newline(data[pos]) && data[pos] != ':' &&
           data[pos] != ',' && data[pos] != ']' && data[pos] != '}' &&
           data[pos] != '#') {
        if (wpos < max_out - 1) {
            out[wpos++] = (char)data[pos];
        }
        pos++;
    }
    out[wpos] = '\0';
    *out_len = wpos;
    return pos;
}

/* --- Parser: extract an alias name after '*' --- */
static size_t parse_alias_name(const uint8_t *data, size_t len, size_t pos,
                                char *out, size_t max_out, size_t *out_len)
{
    size_t wpos = 0;
    pos++; /* skip '*' */
    while (pos < len && !yaml_is_whitespace(data[pos]) &&
           !yaml_is_newline(data[pos]) && data[pos] != ':' &&
           data[pos] != ',' && data[pos] != ']' && data[pos] != '}' &&
           data[pos] != '#') {
        if (wpos < max_out - 1) {
            out[wpos++] = (char)data[pos];
        }
        pos++;
    }
    out[wpos] = '\0';
    *out_len = wpos;
    return pos;
}

/* --- Parser: extract a tag name after '!' --- */
static size_t parse_tag(const uint8_t *data, size_t len, size_t pos,
                         char *out, size_t max_out, size_t *out_len)
{
    size_t wpos = 0;
    /* pos points to '!' */
    while (pos < len && !yaml_is_whitespace(data[pos]) &&
           !yaml_is_newline(data[pos]) && data[pos] != ':' &&
           data[pos] != ',' && data[pos] != ']' && data[pos] != '}') {
        if (wpos < max_out - 1) {
            out[wpos++] = (char)data[pos];
        }
        pos++;
    }
    out[wpos] = '\0';
    *out_len = wpos;
    return pos;
}

/* --- Helper: validate UTF-8 sequence --- */
static int yaml_validate_utf8(const uint8_t *data, size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (data[i] < 0x80) {
            i++;
        } else if ((data[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80) return 0;
            i += 2;
        } else if ((data[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80) return 0;
            i += 3;
        } else if ((data[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80) return 0;
            i += 4;
        } else {
            return 0; /* invalid leading byte */
        }
    }
    return 1;
}

/* --- Helper: write a Unicode code point as UTF-8 bytes --- */
static size_t write_utf8(uint32_t cp, char *out, size_t wpos, size_t max_out)
{
    if (cp < 0x80) {
        if (wpos < max_out - 1) out[wpos++] = (char)cp;
    } else if (cp < 0x800) {
        if (wpos + 1 < max_out - 1) {
            out[wpos++] = (char)(0xC0 | (cp >> 6));
            out[wpos++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (cp < 0x10000) {
        if (wpos + 2 < max_out - 1) {
            out[wpos++] = (char)(0xE0 | (cp >> 12));
            out[wpos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[wpos++] = (char)(0x80 | (cp & 0x3F));
        }
    } else {
        if (wpos + 3 < max_out - 1) {
            out[wpos++] = (char)(0xF0 | (cp >> 18));
            out[wpos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[wpos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[wpos++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    return wpos;
}

/* --- Helper: parse hex digit --- */
static int hex_digit(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* --- Parser: extract a quoted scalar --- */
static size_t parse_quoted_scalar(const uint8_t *data, size_t len, size_t pos,
                                   char quote_char, char *out, size_t max_out,
                                   size_t *out_len, int *found_close)
{
    size_t wpos = 0;
    pos++; /* skip opening quote */
    while (pos < len) {
        if (data[pos] == (uint8_t)quote_char) {
            /* Check for escaped quote: '' in single-quoted, \" in double-quoted */
            if (quote_char == '\'' && pos + 1 < len && data[pos + 1] == '\'') {
                /* In single-quoted strings, '' is escape for literal ' */
                if (wpos < max_out - 1) out[wpos++] = '\'';
                pos += 2;
                continue;
            }
            break; /* actual closing quote */
        }
        if (data[pos] == '\\' && quote_char != '\'' && pos + 1 < len) {
            pos++;
            switch (data[pos]) {
                case 'n': if (wpos < max_out - 1) out[wpos++] = '\n'; break;
                case 't': if (wpos < max_out - 1) out[wpos++] = '\t'; break;
                case '\\': if (wpos < max_out - 1) out[wpos++] = '\\'; break;
                case '"': if (wpos < max_out - 1) out[wpos++] = '"'; break;
                case '\'': if (wpos < max_out - 1) out[wpos++] = '\''; break;
                case '0': if (wpos < max_out - 1) out[wpos++] = '\0'; break;
                case 'a': if (wpos < max_out - 1) out[wpos++] = '\a'; break;
                case 'b': if (wpos < max_out - 1) out[wpos++] = '\b'; break;
                case 'f': if (wpos < max_out - 1) out[wpos++] = '\f'; break;
                case 'r': if (wpos < max_out - 1) out[wpos++] = '\r'; break;
                case 'v': if (wpos < max_out - 1) out[wpos++] = '\v'; break;
                case 'x': /* \xXX hex escape */
                    if (pos + 2 < len) {
                        int h1 = hex_digit(data[pos + 1]);
                        int h2 = hex_digit(data[pos + 2]);
                        if (h1 >= 0 && h2 >= 0) {
                            uint32_t cp = (uint32_t)((h1 << 4) | h2);
                            wpos = write_utf8(cp, out, wpos, max_out);
                            pos += 2;
                        }
                    }
                    break;
                case 'u': /* \uXXXX 4-digit Unicode */
                    if (pos + 4 < len) {
                        int h1 = hex_digit(data[pos+1]), h2 = hex_digit(data[pos+2]);
                        int h3 = hex_digit(data[pos+3]), h4 = hex_digit(data[pos+4]);
                        if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                            uint32_t cp = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
                            wpos = write_utf8(cp, out, wpos, max_out);
                            pos += 4;
                        }
                    }
                    break;
                case 'U': /* \UXXXXXXXX 8-digit Unicode */
                    if (pos + 8 < len) {
                        uint32_t cp = 0;
                        int valid = 1;
                        for (int k = 1; k <= 8; k++) {
                            int d = hex_digit(data[pos + k]);
                            if (d < 0) { valid = 0; break; }
                            cp = (cp << 4) | (uint32_t)d;
                        }
                        if (valid) {
                            wpos = write_utf8(cp, out, wpos, max_out);
                            pos += 8;
                        }
                    }
                    break;
                default: if (wpos < max_out - 1) out[wpos++] = (char)data[pos]; break;
            }
        } else {
            if (wpos < max_out - 1) out[wpos++] = (char)data[pos];
        }
        pos++;
    }
    if (pos < len) {
        pos++; /* skip closing quote */
        if (found_close) *found_close = 1;
    } else {
        if (found_close) *found_close = 0;
    }
    out[wpos] = '\0';
    *out_len = wpos;
    return pos;
}

/* --- Parser: extract a block scalar (literal | or folded >) --- *
 *
 * On entry, pos should point to the '|' or '>' indicator.
 * On return, pos is after the last consumed line of the block scalar.
 * The block scalar content is written to out with style set accordingly.
 */
static size_t parse_block_scalar(const uint8_t *data, size_t len, size_t pos,
                                  char *out, size_t max_out, size_t *out_len,
                                  yaml_scalar_style_t *style)
{
    size_t wpos = 0;

    /* Determine style from indicator */
    if (pos < len && data[pos] == '|') {
        *style = YAML_SCALAR_LITERAL;
    } else {
        *style = YAML_SCALAR_FOLDED;
    }
    pos++; /* skip | or > */

    /* Skip optional chomp/indent indicators (e.g. |-, |+, |2) */
    while (pos < len && !yaml_is_newline(data[pos])) {
        pos++;
    }
    /* Skip the newline */
    if (pos < len) {
        if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') pos++;
        pos++;
    }

    /* Determine block indentation from first non-empty line */
    int block_indent = -1;
    {
        size_t scan = pos;
        while (scan < len) {
            /* Check if line is non-empty */
            int col = 0;
            while (scan < len && (data[scan] == ' ' || data[scan] == '\t')) {
                if (data[scan] == ' ') col++;
                else col = (col + 2) & ~1;
                scan++;
            }
            if (scan < len && !yaml_is_newline(data[scan])) {
                block_indent = col;
                break;
            }
            /* Skip empty line */
            if (scan < len) {
                if (data[scan] == '\r' && scan + 1 < len && data[scan + 1] == '\n') scan++;
                scan++;
            }
        }
    }

    if (block_indent < 0) {
        /* No content lines found */
        *out_len = 0;
        out[0] = '\0';
        return pos;
    }

    /* Collect block scalar lines */
    int prev_was_empty = 0;
    while (pos < len) {
        /* Compute line indent */
        size_t line_start = pos;
        int col = 0;
        while (pos < len && (data[pos] == ' ' || data[pos] == '\t')) {
            if (data[pos] == ' ') col++;
            else col = (col + 2) & ~1;
            pos++;
        }

        /* Empty line (only whitespace or newline) */
        if (pos >= len || yaml_is_newline(data[pos])) {
            if (*style == YAML_SCALAR_LITERAL) {
                /* Literal: preserve empty lines */
                if (wpos < max_out - 1) out[wpos++] = '\n';
            } else {
                /* Folded: empty line becomes newline (paragraph break) */
                if (wpos < max_out - 1) out[wpos++] = '\n';
            }
            if (pos < len) {
                if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') pos++;
                pos++;
            }
            prev_was_empty = 1;
            continue;
        }

        /* If indent is less than block_indent, this line is not part of the block */
        if (col < block_indent) {
            /* Don't consume this line — back up to line_start */
            pos = line_start;
            break;
        }

        /* Content line */
        if (*style == YAML_SCALAR_FOLDED && !prev_was_empty && wpos > 0) {
            /* Folded: replace newline with space (unless preceded by empty line) */
            out[wpos - 1] = ' '; /* replace the trailing newline with space */
        }

        /* Copy line content */
        while (pos < len && !yaml_is_newline(data[pos])) {
            if (wpos < max_out - 1) out[wpos++] = (char)data[pos];
            pos++;
        }

        /* Add newline */
        if (wpos < max_out - 1) out[wpos++] = '\n';

        /* Skip the newline */
        if (pos < len) {
            if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') pos++;
            pos++;
        }
        prev_was_empty = 0;
    }

    /* Trim trailing newline for folded style */
    if (*style == YAML_SCALAR_FOLDED && wpos > 0 && out[wpos - 1] == '\n') {
        wpos--; /* remove last newline, folded adds final newline via spec */
    }

    out[wpos] = '\0';
    *out_len = wpos;
    return pos;
}

/* --- Parser: flow collection helpers --- */

/* Parse a single flow scalar value (plain or quoted) within a flow context.
 * Returns new position. Writes value to out, sets out_len and out_style. */
static size_t parse_flow_scalar(const uint8_t *data, size_t len, size_t pos,
                                 char *out, size_t max_out, size_t *out_len,
                                 yaml_scalar_style_t *out_style)
{
    if (data[pos] == '\"') {
        *out_style = YAML_SCALAR_DOUBLE_QUOTED;
        pos = parse_quoted_scalar(data, len, pos, '\"', out, max_out, out_len, NULL);
    } else if (data[pos] == '\'') {
        *out_style = YAML_SCALAR_SINGLE_QUOTED;
        pos = parse_quoted_scalar(data, len, pos, '\'', out, max_out, out_len, NULL);
    } else {
        *out_style = YAML_SCALAR_PLAIN;
        pos = parse_plain_scalar(data, len, pos, out, max_out, out_len);
    }
    return pos;
}

/* Forward declarations for flow collection parsers */
static int push_indent(yaml_ctx_t *ctx, int column, indent_type_t type);
static size_t parse_flow_mapping(yaml_ctx_t *ctx,
                                  const uint8_t *data, size_t len, size_t pos);

/* Parse a flow sequence [item1, item2, ...] starting at the '[' character.
 * Emits SEQUENCE_START, SCALAR (per item), SEQUENCE_END events.
 * Updates knowledge tree for items as indexed children of current path.
 * Returns position after the closing ']'. */
static size_t parse_flow_sequence(yaml_ctx_t *ctx,
                                   const uint8_t *data, size_t len, size_t pos)
{
    yaml_event_t ev;

    /* Emit SEQUENCE_START */
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_SEQUENCE_START;
    queue_push(&ctx->queue, &ev);

    pos++; /* skip '[' */
    pos = skip_spaces(data, len, pos);

    /* Push a flow sequence indent level for path tracking */
    push_indent(ctx, 0, INDENT_SEQUENCE);
    ctx->indent_seq_idx[ctx->indent_depth - 1] = 0;
    int flow_depth = 1;
    int first_item = 1;

    while (pos < len && flow_depth > 0) {
        pos = skip_spaces(data, len, pos);
        if (pos >= len) break;

        /* Check for closing bracket */
        if (data[pos] == ']') {
            pos++;
            flow_depth--;
            break;
        }

        /* Skip commas (also handles trailing commas) */
        if (data[pos] == ',') {
            pos++;
            pos = skip_spaces(data, len, pos);
            /* If comma is followed by ']', it's a trailing comma — still valid */
            if (pos < len && data[pos] == ']') {
                pos++;
                flow_depth--;
                break;
            }
            continue;
        }

        /* Parse item */
        if (!first_item) {
            /* Between items, we expect a comma — but allow missing comma too */
        }
        first_item = 0;

        /* Handle nested flow collections */
        if (data[pos] == '[') {
            /* Nested flow sequence */
            size_t child_idx = ctx->indent_seq_idx[ctx->indent_depth - 1];
            ctx->indent_keys[ctx->indent_depth - 1][0] = '\0';
            ctx->indent_key_lens[ctx->indent_depth - 1] = 0;

            pos = parse_flow_sequence(ctx, data, len, pos);
            ctx->indent_seq_idx[ctx->indent_depth - 1] = child_idx + 1;
            pos = skip_spaces(data, len, pos);
        } else if (data[pos] == '{') {
            /* Nested flow mapping */
            pos = parse_flow_mapping(ctx, data, len, pos);
            /* Emit a scalar placeholder for the nested mapping? No — the
             * flow mapping emits its own events. Treat as item. */
            ctx->indent_seq_idx[ctx->indent_depth - 1]++;
            pos = skip_spaces(data, len, pos);
        } else {
            /* Scalar item */
            char val_buf[YAML_MAX_SCALAR_LEN];
            size_t val_len = 0;
            yaml_scalar_style_t val_style = YAML_SCALAR_PLAIN;

            pos = parse_flow_scalar(data, len, pos, val_buf, sizeof(val_buf),
                                     &val_len, &val_style);

            memset(&ev, 0, sizeof(ev));
            ev.type = YAML_EVENT_SCALAR;
            memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
            if (val_len < YAML_MAX_SCALAR_LEN) {
                memcpy(ev.data.scalar.value, val_buf, val_len);
            }
            ev.data.scalar.value_len = val_len;
            ev.data.scalar.style = val_style;
            queue_push(&ctx->queue, &ev);

            /* Update knowledge tree with indexed sequence path */
            {
                char kp[1024];
                build_knowledge_path(ctx, kp, sizeof(kp));
                if (kp[0] != '\0') {
                    knowledge_insert_path(&ctx->knowledge_root, kp,
                                          val_buf, val_len);
                }
            }

            ctx->indent_seq_idx[ctx->indent_depth - 1]++;
            pos = skip_spaces(data, len, pos);
        }
    }

    /* Pop the flow sequence indent level */
    if (ctx->indent_depth > 0) {
        ctx->indent_depth--;
    }

    /* Emit SEQUENCE_END */
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_SEQUENCE_END;
    queue_push(&ctx->queue, &ev);

    return pos;
}

/* Parse a flow mapping {key: value, ...} starting at the '{' character.
 * Emits MAPPING_START, SCALAR (keys and values), MAPPING_END events.
 * Updates knowledge tree for key-value pairs.
 * Returns position after the closing '}'. */
static size_t parse_flow_mapping(yaml_ctx_t *ctx,
                                  const uint8_t *data, size_t len, size_t pos)
{
    yaml_event_t ev;

    /* Emit MAPPING_START */
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_MAPPING_START;
    queue_push(&ctx->queue, &ev);

    pos++; /* skip '{' */
    pos = skip_spaces(data, len, pos);

    /* Push a flow mapping indent level for path tracking */
    push_indent(ctx, 0, INDENT_MAPPING);
    int flow_depth = 1;
    int in_value = 0; /* 0 = expecting key, 1 = expecting value */

    while (pos < len && flow_depth > 0) {
        pos = skip_spaces(data, len, pos);
        if (pos >= len) break;

        /* Check for closing brace */
        if (data[pos] == '}') {
            pos++;
            flow_depth--;
            break;
        }

        /* Skip commas (also handles trailing commas) */
        if (data[pos] == ',') {
            pos++;
            pos = skip_spaces(data, len, pos);
            in_value = 0;
            /* If comma is followed by '}', it's a trailing comma */
            if (pos < len && data[pos] == '}') {
                pos++;
                flow_depth--;
                break;
            }
            continue;
        }

        /* Check for colon separator between key and value */
        if (in_value && data[pos] == ':') {
            pos++;
            pos = skip_spaces(data, len, pos);
            in_value = 0;
            /* Handle value after colon */

            /* Empty value (colon immediately followed by comma/brace) */
            if (pos >= len || data[pos] == ',' || data[pos] == '}') {
                /* Emit empty scalar for missing value */
                memset(&ev, 0, sizeof(ev));
                ev.type = YAML_EVENT_SCALAR;
                memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
                ev.data.scalar.value_len = 0;
                ev.data.scalar.style = YAML_SCALAR_PLAIN;
                queue_push(&ctx->queue, &ev);
                continue;
            }

            /* Parse the value */
            if (data[pos] == '[') {
                pos = parse_flow_sequence(ctx, data, len, pos);
            } else if (data[pos] == '{') {
                pos = parse_flow_mapping(ctx, data, len, pos);
            } else {
                char val_buf[YAML_MAX_SCALAR_LEN];
                size_t val_len = 0;
                yaml_scalar_style_t val_style = YAML_SCALAR_PLAIN;
                pos = parse_flow_scalar(data, len, pos, val_buf, sizeof(val_buf),
                                         &val_len, &val_style);

                memset(&ev, 0, sizeof(ev));
                ev.type = YAML_EVENT_SCALAR;
                memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
                if (val_len < YAML_MAX_SCALAR_LEN) {
                    memcpy(ev.data.scalar.value, val_buf, val_len);
                }
                ev.data.scalar.value_len = val_len;
                ev.data.scalar.style = val_style;
                queue_push(&ctx->queue, &ev);

                /* Update knowledge tree */
                {
                    char kp[1024];
                    build_knowledge_path(ctx, kp, sizeof(kp));
                    if (kp[0] != '\0') {
                        knowledge_insert_path(&ctx->knowledge_root, kp,
                                              val_buf, val_len);
                    }
                }
            }
            pos = skip_spaces(data, len, pos);
            continue;
        }

        /* Parse key */
        {
            char key_buf[YAML_MAX_SCALAR_LEN];
            size_t key_len = 0;
            yaml_scalar_style_t key_style = YAML_SCALAR_PLAIN;

            pos = parse_flow_scalar(data, len, pos, key_buf, sizeof(key_buf),
                                     &key_len, &key_style);

            memset(&ev, 0, sizeof(ev));
            ev.type = YAML_EVENT_SCALAR;
            memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
            if (key_len < YAML_MAX_SCALAR_LEN) {
                memcpy(ev.data.scalar.value, key_buf, key_len);
            }
            ev.data.scalar.value_len = key_len;
            ev.data.scalar.style = key_style;
            queue_push(&ctx->queue, &ev);

            /* Store key for path tracking */
            if (ctx->indent_depth > 0) {
                size_t kl = key_len < 255 ? key_len : 255;
                memcpy(ctx->indent_keys[ctx->indent_depth - 1], key_buf, kl);
                ctx->indent_keys[ctx->indent_depth - 1][kl] = '\0';
                ctx->indent_key_lens[ctx->indent_depth - 1] = (int)kl;
            }

            in_value = 1;
        }
        pos = skip_spaces(data, len, pos);
    }

    /* Pop the flow mapping indent level */
    if (ctx->indent_depth > 0) {
        ctx->indent_depth--;
    }

    /* Emit MAPPING_END */
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_MAPPING_END;
    queue_push(&ctx->queue, &ev);

    return pos;
}

/* --- Core parser: process accumulated input and enqueue events --- */

/* Check if the next lines after a plain scalar are continuations.
 * A continuation line is more indented than parent_indent, is not empty,
 * not a comment, not a new key (contains ': '), and not a sequence marker.
 * Appends continuation content to out with space separator.
 * Returns new position after all consumed continuation lines. (1.11) */
static size_t parse_multiline_continuation(const uint8_t *data, size_t len, size_t pos,
                                            int parent_indent,
                                            char *out, size_t max_out, size_t *out_len)
{
    while (1) {
        /* Skip past newline at current position */
        size_t try_pos = pos;
        if (try_pos < len && data[try_pos] == '\r' && try_pos + 1 < len && data[try_pos + 1] == '\n') {
            try_pos += 2;
        } else if (try_pos < len && data[try_pos] == '\n') {
            try_pos++;
        } else {
            break; /* no more data */
        }

        /* Measure indent of next line */
        int col = 0;
        size_t sp = try_pos;
        while (sp < len && (data[sp] == ' ' || data[sp] == '\t')) {
            if (data[sp] == ' ') col++;
            else col = (col + 2) & ~1;
            sp++;
        }

        /* Must be more indented than parent and not empty/comment/document markers */
        if (sp >= len || yaml_is_newline(data[sp]) || data[sp] == '#') {
            break;
        }
        if (col <= parent_indent) {
            break;
        }
        /* Not a document marker */
        if (sp + 2 < len && data[sp] == '-' && data[sp+1] == '-' && data[sp+2] == '-') break;
        if (sp + 2 < len && data[sp] == '.' && data[sp+1] == '.' && data[sp+2] == '.') break;
        /* Not a sequence marker */
        if (data[sp] == '-' && (sp + 1 >= len || yaml_is_whitespace(data[sp+1]) || yaml_is_newline(data[sp+1]))) {
            break;
        }
        /* Not a new key (line contains ': ' or ':\n') */
        {
            size_t scan = sp;
            int is_key = 0;
            while (scan < len && !yaml_is_newline(data[scan])) {
                if (data[scan] == ':' && (scan + 1 >= len ||
                    yaml_is_whitespace(data[scan+1]) || yaml_is_newline(data[scan+1]))) {
                    is_key = 1;
                    break;
                }
                scan++;
            }
            if (is_key) break;
        }

        /* It's a continuation — append with space */
        if (*out_len > 0 && *out_len < max_out - 1) {
            out[(*out_len)++] = ' ';
        }
        while (sp < len && !yaml_is_newline(data[sp])) {
            if (*out_len < max_out - 1) {
                out[(*out_len)++] = (char)data[sp];
            }
            sp++;
        }
        /* Trim trailing whitespace */
        while (*out_len > 0 && (out[*out_len - 1] == ' ' || out[*out_len - 1] == '\t')) {
            out[--(*out_len)] = '\0';
        }
        out[*out_len] = '\0';
        pos = sp;
    }
    return pos;
}

/* Helper: close all open indent levels, emitting end events */
static void close_all_levels(yaml_ctx_t *ctx)
{
    yaml_event_t ev;
    while (ctx->indent_depth > 0) {
        ctx->indent_depth--;
        memset(&ev, 0, sizeof(ev));
        if (ctx->indent_types[ctx->indent_depth] == INDENT_SEQUENCE) {
            ev.type = YAML_EVENT_SEQUENCE_END;
        } else {
            ev.type = YAML_EVENT_MAPPING_END;
        }
        queue_push(&ctx->queue, &ev);
    }
}

/* Helper: pop indent levels back to target_depth+1, emitting end events */
static void pop_to_depth(yaml_ctx_t *ctx, int target_depth)
{
    yaml_event_t ev;
    while (ctx->indent_depth > target_depth) {
        ctx->indent_depth--;
        memset(&ev, 0, sizeof(ev));
        if (ctx->indent_types[ctx->indent_depth] == INDENT_SEQUENCE) {
            ev.type = YAML_EVENT_SEQUENCE_END;
        } else {
            ev.type = YAML_EVENT_MAPPING_END;
        }
        queue_push(&ctx->queue, &ev);
    }
}

/* Helper: push a new indent level */
static int push_indent(yaml_ctx_t *ctx, int column, indent_type_t type)
{
    if (ctx->indent_depth >= YAML_MAX_INDENT_LEVELS) return -1;
    ctx->indent_stack[ctx->indent_depth] = column;
    ctx->indent_types[ctx->indent_depth] = type;
    ctx->indent_depth++;
    return 0;
}

/* Helper: find the indent depth that matches a given column, or -1 */
static int find_indent_depth(yaml_ctx_t *ctx, int column)
{
    for (int i = ctx->indent_depth - 1; i >= 0; i--) {
        if (ctx->indent_stack[i] == column) return i;
    }
    return -1;
}

static void parse_input(yaml_ctx_t *ctx)
{
    const uint8_t *data = ctx->input_buf;
    size_t total_len = ctx->input_len;
    size_t len = total_len;
    size_t pos = 0;

    /* 6.1/12.4: Only process up to the last complete line (ending with \n).
     * This prevents losing partial lines when input ends mid-line.
     * If no newline found, buffer everything for next feed_input call. */
    {
        size_t last_nl = len;
        for (size_t i = 0; i < len; i++) {
            if (data[i] == '\n') last_nl = i;
        }
        if (last_nl < len) {
            len = last_nl + 1;
        } else {
            return; /* no complete line yet */
        }
    }

    while (pos < len) {
        /* Compute line indent (leading spaces) */
        int line_indent = 0;
        {
            size_t sp = pos;
            int col = 0;
            int has_tab = 0;
            while (sp < len && (data[sp] == ' ' || data[sp] == '\t')) {
                if (data[sp] == ' ') col++;
                else {
                    col = (col + 2) & ~1; /* tab to next 2-boundary */
                    has_tab = 1;
                }
                sp++;
            }
            if (sp >= len) {
                pos = skip_to_eol(data, len, pos);
                continue;
            }
            /* Strict mode: reject tab indentation */
            if (has_tab && ctx->config.strict_mode) {
                emit_error(ctx, "invalid indentation: tab character not allowed in strict mode");
                return;
            }
            line_indent = col;
            pos = sp; /* advance past whitespace */
        }

        /* Skip comments */
        if (data[pos] == '#') {
            pos = skip_to_eol(data, len, pos);
            continue;
        }

        /* Stream/document marker: --- */
        if (pos + 2 < len && data[pos] == '-' && data[pos+1] == '-' && data[pos+2] == '-') {
            if (ctx->state == YAML_STATE_IDLE) {
                yaml_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = YAML_EVENT_STREAM_START;
                queue_push(&ctx->queue, &ev);

                ev.type = YAML_EVENT_DOCUMENT_START;
                ev.data.document_start.implicit = 0;
                queue_push(&ctx->queue, &ev);

                ctx->state = YAML_STATE_DOCUMENT;
            } else {
                /* Close open collections, then emit doc boundary */
                close_all_levels(ctx);

                yaml_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = YAML_EVENT_DOCUMENT_END;
                ev.data.document_start.implicit = 0;
                queue_push(&ctx->queue, &ev);

                ev.type = YAML_EVENT_DOCUMENT_START;
                ev.data.document_start.implicit = 0;
                queue_push(&ctx->queue, &ev);
                ctx->state = YAML_STATE_DOCUMENT;
            }
            pos = skip_to_eol(data, len, pos);
            continue;
        }

        /* End-of-document marker: ... */
        if (pos + 2 < len && data[pos] == '.' && data[pos+1] == '.' && data[pos+2] == '.') {
            close_all_levels(ctx);

            yaml_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = YAML_EVENT_DOCUMENT_END;
            ev.data.document_start.implicit = 0;
            queue_push(&ctx->queue, &ev);

            pos = skip_to_eol(data, len, pos);
            ctx->state = YAML_STATE_DONE;
            continue;
        }

        /* Transition from IDLE to STREAM + DOCUMENT if needed */
        if (ctx->state == YAML_STATE_IDLE) {
            yaml_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = YAML_EVENT_STREAM_START;
            queue_push(&ctx->queue, &ev);

            ev.type = YAML_EVENT_DOCUMENT_START;
            ev.data.document_start.implicit = 1;
            queue_push(&ctx->queue, &ev);
            ctx->state = YAML_STATE_DOCUMENT;
        }

        /* Pop indent levels if line_indent decreased */
        if (ctx->indent_depth > 0) {
            int current_indent = ctx->indent_stack[ctx->indent_depth - 1];
            if (line_indent < current_indent) {
                int target = find_indent_depth(ctx, line_indent);
                if (target >= 0) {
                    pop_to_depth(ctx, target + 1);
                } else {
                    /* Indent doesn't match any level — pop all */
                    close_all_levels(ctx);
                }
            }
        }

        /* --- Sequence item: '- value' --- */
        if (pos < len && data[pos] == '-' &&
            (pos + 1 >= len || yaml_is_whitespace(data[pos+1]) || yaml_is_newline(data[pos+1]))) {

            /* Check if we need to start a new sequence */
            int need_new_sequence = 1;
            if (ctx->indent_depth > 0 &&
                ctx->indent_types[ctx->indent_depth - 1] == INDENT_SEQUENCE &&
                ctx->indent_stack[ctx->indent_depth - 1] == line_indent) {
                need_new_sequence = 0;
            }

            if (need_new_sequence) {
                yaml_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = YAML_EVENT_SEQUENCE_START;
                ev.data.collection_start.tag_len = 0;
                ev.data.collection_start.anchor_len = 0;
                /* Apply pending anchor from previous mapping value */
                if (ctx->pending_anchor_len > 0) {
                    memcpy(ev.data.collection_start.anchor, ctx->pending_anchor, ctx->pending_anchor_len);
                    ev.data.collection_start.anchor_len = ctx->pending_anchor_len;
                    ctx->pending_anchor_len = 0;
                }
                queue_push(&ctx->queue, &ev);
                push_indent(ctx, line_indent, INDENT_SEQUENCE);
                ctx->indent_seq_idx[ctx->indent_depth - 1] = 0;
            } else {
                /* Continuing existing sequence — increment item index */
                ctx->indent_seq_idx[ctx->indent_depth - 1]++;
            }

            pos++;
            if (pos < len && yaml_is_whitespace(data[pos])) {
                pos = skip_spaces(data, len, pos);
            }

            /* Parse value after '-' */
            if (pos < len && !yaml_is_newline(data[pos])) {
                /* Check if value is a mapping (contains ': ') */
                size_t scan = pos;
                int has_colon = 0;
                while (scan < len && !yaml_is_newline(data[scan])) {
                    if (data[scan] == ':' && (scan + 1 >= len ||
                        yaml_is_whitespace(data[scan+1]) || yaml_is_newline(data[scan+1]))) {
                        has_colon = 1;
                        break;
                    }
                    scan++;
                }

                if (has_colon) {
                    /* Inline mapping in sequence: - key: value */
                    yaml_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = YAML_EVENT_MAPPING_START;
                    ev.data.collection_start.tag_len = 0;
                    ev.data.collection_start.anchor_len = 0;
                    /* Apply pending anchor from previous mapping value */
                    if (ctx->pending_anchor_len > 0) {
                        memcpy(ev.data.collection_start.anchor, ctx->pending_anchor, ctx->pending_anchor_len);
                        ev.data.collection_start.anchor_len = ctx->pending_anchor_len;
                        ctx->pending_anchor_len = 0;
                    }
                    queue_push(&ctx->queue, &ev);
                    push_indent(ctx, line_indent + 2, INDENT_MAPPING);

                    /* Parse key */
                    char key_buf[YAML_MAX_SCALAR_LEN];
                    size_t key_len = 0;
                    if (data[pos] == '"') {
                        pos = parse_quoted_scalar(data, len, pos, '"',
                                                  key_buf, sizeof(key_buf), &key_len, NULL);
                    } else if (data[pos] == '\'') {
                        pos = parse_quoted_scalar(data, len, pos, '\'',
                                                  key_buf, sizeof(key_buf), &key_len, NULL);
                    } else {
                        pos = parse_plain_scalar(data, len, pos,
                                                 key_buf, sizeof(key_buf), &key_len);
                    }

                    ev.type = YAML_EVENT_SCALAR;
                    memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
                    if (key_len < YAML_MAX_SCALAR_LEN) {
                        memcpy(ev.data.scalar.value, key_buf, key_len);
                    }
                    ev.data.scalar.value_len = key_len;
                    ev.data.scalar.style = YAML_SCALAR_PLAIN;
                    queue_push(&ctx->queue, &ev);

                    /* Store key for path tracking */
                    if (ctx->indent_depth > 0) {
                        size_t kl = key_len < 255 ? key_len : 255;
                        memcpy(ctx->indent_keys[ctx->indent_depth - 1], key_buf, kl);
                        ctx->indent_keys[ctx->indent_depth - 1][kl] = '\0';
                        ctx->indent_key_lens[ctx->indent_depth - 1] = (int)kl;
                    }

                    /* Skip colon */
                    pos = skip_spaces(data, len, pos);
                    if (pos < len && data[pos] == ':') pos++;
                    pos = skip_spaces(data, len, pos);

                    /* Parse value */
                    if (pos < len && !yaml_is_newline(data[pos])) {
                        /* Check for flow collection values */
                        if (data[pos] == '[') {
                            pos = parse_flow_sequence(ctx, data, len, pos);
                        } else if (data[pos] == '{') {
                            pos = parse_flow_mapping(ctx, data, len, pos);
                        } else {
                        char val_buf[YAML_MAX_SCALAR_LEN];
                        size_t val_len = 0;
                        yaml_scalar_style_t val_style = YAML_SCALAR_PLAIN;
                        if (data[pos] == '"') {
                            pos = parse_quoted_scalar(data, len, pos, '"',
                                                      val_buf, sizeof(val_buf), &val_len, NULL);
                        } else if (data[pos] == '\'') {
                            pos = parse_quoted_scalar(data, len, pos, '\'',
                                                      val_buf, sizeof(val_buf), &val_len, NULL);
                        } else if (data[pos] == '|' || data[pos] == '>') {
                            pos = parse_block_scalar(data, len, pos,
                                                     val_buf, sizeof(val_buf), &val_len,
                                                     &val_style);
                        } else {
                            pos = parse_plain_scalar(data, len, pos,
                                                     val_buf, sizeof(val_buf), &val_len);
                            /* 1.11: Check for multiline continuation */
                            pos = parse_multiline_continuation(data, len, pos,
                                                                line_indent,
                                                                val_buf, sizeof(val_buf), &val_len);
                        }

                        ev.type = YAML_EVENT_SCALAR;
                        memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
                        if (val_len < YAML_MAX_SCALAR_LEN) {
                            memcpy(ev.data.scalar.value, val_buf, val_len);
                        }
                        ev.data.scalar.value_len = val_len;
                        ev.data.scalar.style = val_style;
                        queue_push(&ctx->queue, &ev);

                        /* Knowledge tree update */
                        {
                            char kp[1024];
                            build_knowledge_path(ctx, kp, sizeof(kp));
                            if (kp[0] != '\0') {
                                knowledge_insert_path(&ctx->knowledge_root, kp,
                                                      val_buf, val_len);
                            }
                        }
                        } /* end else (not flow) */
                    }
                    /* Mapping stays open — indent tracking will close it
                     * when a line at lesser indent is encountered */
                } else if (data[pos] == '[') {
                    /* Flow sequence value: - [a, b, c] */
                    pos = parse_flow_sequence(ctx, data, len, pos);
                } else if (data[pos] == '{') {
                    /* Flow mapping value: - {key: value} */
                    pos = parse_flow_mapping(ctx, data, len, pos);
                } else {
                    /* Simple scalar value */
                    char scalar_buf[YAML_MAX_SCALAR_LEN];
                    size_t scalar_len = 0;
                    yaml_scalar_style_t scalar_style = YAML_SCALAR_PLAIN;
                    if (data[pos] == '"') {
                        pos = parse_quoted_scalar(data, len, pos, '"',
                                                  scalar_buf, sizeof(scalar_buf), &scalar_len, NULL);
                    } else if (data[pos] == '\'') {
                        pos = parse_quoted_scalar(data, len, pos, '\'',
                                                  scalar_buf, sizeof(scalar_buf), &scalar_len, NULL);
                    } else if (data[pos] == '|' || data[pos] == '>') {
                        pos = parse_block_scalar(data, len, pos,
                                                 scalar_buf, sizeof(scalar_buf), &scalar_len,
                                                 &scalar_style);
                    } else {
                        pos = parse_plain_scalar(data, len, pos,
                                                 scalar_buf, sizeof(scalar_buf), &scalar_len);
                    }

                    yaml_event_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = YAML_EVENT_SCALAR;
                    memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
                    if (scalar_len < YAML_MAX_SCALAR_LEN) {
                        memcpy(ev.data.scalar.value, scalar_buf, scalar_len);
                    }
                    ev.data.scalar.value_len = scalar_len;
                    ev.data.scalar.style = scalar_style;
                    queue_push(&ctx->queue, &ev);
                }
            }
            pos = skip_to_eol(data, len, pos);
            continue;
        }

        /* --- Mapping key: value --- */
        {
            /* Skip flow collection starts — handled below in standalone section */
            if (data[pos] == '[' || data[pos] == '{') {
                /* Fall through to standalone/flow section */
            } else {
            /* Scan current line for ': ' or ':\n' */
            size_t scan = pos;
            int is_mapping = 0;
            while (scan < len && !yaml_is_newline(data[scan])) {
                if (data[scan] == ':' && (scan + 1 >= len ||
                    yaml_is_whitespace(data[scan+1]) || yaml_is_newline(data[scan+1]))) {
                    is_mapping = 1;
                    break;
                }
                scan++;
            }

            if (is_mapping) {
                yaml_event_t ev;
                memset(&ev, 0, sizeof(ev));

                /* Check if we need to start a new mapping */
                int need_new_mapping = 1;
                if (ctx->indent_depth > 0 &&
                    ctx->indent_types[ctx->indent_depth - 1] == INDENT_MAPPING &&
                    ctx->indent_stack[ctx->indent_depth - 1] == line_indent) {
                    need_new_mapping = 0;
                }

                if (need_new_mapping) {
                    ev.type = YAML_EVENT_MAPPING_START;
                    ev.data.collection_start.tag_len = 0;
                    ev.data.collection_start.anchor_len = 0;
                    /* Apply pending anchor from previous mapping value */
                    if (ctx->pending_anchor_len > 0) {
                        memcpy(ev.data.collection_start.anchor, ctx->pending_anchor, ctx->pending_anchor_len);
                        ev.data.collection_start.anchor_len = ctx->pending_anchor_len;
                        ctx->pending_anchor_len = 0;
                    }
                    queue_push(&ctx->queue, &ev);
                    push_indent(ctx, line_indent, INDENT_MAPPING);
                }

                /* Parse key — check for anchor, alias, or tag prefix */
                char key_buf[YAML_MAX_SCALAR_LEN];
                size_t key_len = 0;
                char anchor_buf[YAML_MAX_ALIAS_LEN] = {0};
                size_t anchor_len = 0;
                char tag_buf[YAML_MAX_TAG_LEN] = {0};
                size_t tag_len = 0;

                /* Check for anchor (&name) */
                if (pos < len && data[pos] == '&') {
                    pos = parse_anchor(data, len, pos,
                                       anchor_buf, sizeof(anchor_buf), &anchor_len);
                    pos = skip_spaces(data, len, pos);
                }
                /* Check for alias (*name) */
                if (pos < len && data[pos] == '*') {
                    char alias_buf[YAML_MAX_ALIAS_LEN];
                    size_t alias_len = 0;
                    pos = parse_alias_name(data, len, pos,
                                           alias_buf, sizeof(alias_buf), &alias_len);
                    yaml_event_t alias_ev;
                    memset(&alias_ev, 0, sizeof(alias_ev));
                    alias_ev.type = YAML_EVENT_ALIAS;
                    if (alias_len < YAML_MAX_ALIAS_LEN) {
                        memcpy(alias_ev.data.alias.anchor, alias_buf, alias_len);
                        alias_ev.data.alias.anchor_len = alias_len;
                    }
                    queue_push(&ctx->queue, &alias_ev);
                    pos = skip_to_eol(data, len, pos);
                    continue;
                }
                /* Check for tag (!tag or !!tag) */
                if (pos < len && data[pos] == '!') {
                    pos = parse_tag(data, len, pos,
                                    tag_buf, sizeof(tag_buf), &tag_len);
                    /* Strict mode tag validation */
                    if (ctx->config.strict_mode && tag_len > 0) {
                        if (!is_standard_tag(tag_buf, tag_len)) {
                            emit_error(ctx, "invalid tag: non-standard tag in strict mode");
                            return;
                        }
                    }
                    pos = skip_spaces(data, len, pos);
                }

                {
                    int key_found_close = 1;
                    if (data[pos] == '"') {
                        pos = parse_quoted_scalar(data, len, pos, '"',
                                                  key_buf, sizeof(key_buf), &key_len,
                                                  &key_found_close);
                    } else if (data[pos] == '\'') {
                        pos = parse_quoted_scalar(data, len, pos, '\'',
                                                  key_buf, sizeof(key_buf), &key_len,
                                                  &key_found_close);
                    } else {
                        pos = parse_plain_scalar(data, len, pos,
                                                 key_buf, sizeof(key_buf), &key_len);
                    }
                    if (!key_found_close) {
                        emit_error(ctx, "unclosed quoted scalar");
                        return;
                    }
                }

                ev.type = YAML_EVENT_SCALAR;
                memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
                if (key_len < YAML_MAX_SCALAR_LEN) {
                    memcpy(ev.data.scalar.value, key_buf, key_len);
                }
                ev.data.scalar.value_len = key_len;
                ev.data.scalar.style = YAML_SCALAR_PLAIN;
                if (tag_len > 0 && tag_len < YAML_MAX_TAG_LEN) {
                    memcpy(ev.data.scalar.tag, tag_buf, tag_len);
                    ev.data.scalar.tag_len = tag_len;
                }
                queue_push(&ctx->queue, &ev);

                /* Store key for path tracking */
                if (ctx->indent_depth > 0) {
                    size_t kl = key_len < 255 ? key_len : 255;
                    memcpy(ctx->indent_keys[ctx->indent_depth - 1], key_buf, kl);
                    ctx->indent_keys[ctx->indent_depth - 1][kl] = '\0';
                    ctx->indent_key_lens[ctx->indent_depth - 1] = (int)kl;
                }

                /* Store anchor in the mapping start event if present */
                if (anchor_len > 0) {
                    /* We already pushed the mapping start — update the anchor
                     * by finding the last MAPPING_START in the queue and setting it */
                    /* For now, we'll store it on the scalar key event */
                }

                /* Skip colon */
                pos = skip_spaces(data, len, pos);
                if (pos < len && data[pos] == ':') pos++;
                pos = skip_spaces(data, len, pos);

                /* Parse value */
                int is_block_scalar = 0;
                /* Parse anchor for value before checking if value is inline */
                char val_anchor[YAML_MAX_ALIAS_LEN] = {0};
                size_t val_anchor_len = 0;
                if (pos < len && data[pos] == '&') {
                    pos = parse_anchor(data, len, pos,
                                       val_anchor, sizeof(val_anchor), &val_anchor_len);
                    pos = skip_spaces(data, len, pos);
                }
                if (pos < len && !yaml_is_newline(data[pos])) {
                    char val_buf[YAML_MAX_SCALAR_LEN];
                    size_t val_len = 0;
                    yaml_scalar_style_t val_style = YAML_SCALAR_PLAIN;
                    char val_tag[YAML_MAX_TAG_LEN] = {0};
                    size_t val_tag_len = 0;
                    /* Check for alias (*name) */
                    if (pos < len && data[pos] == '*') {
                        char alias_buf[YAML_MAX_ALIAS_LEN];
                        size_t alias_len = 0;
                        pos = parse_alias_name(data, len, pos,
                                               alias_buf, sizeof(alias_buf), &alias_len);
                        yaml_event_t alias_ev;
                        memset(&alias_ev, 0, sizeof(alias_ev));
                        alias_ev.type = YAML_EVENT_ALIAS;
                        if (alias_len < YAML_MAX_ALIAS_LEN) {
                            memcpy(alias_ev.data.alias.anchor, alias_buf, alias_len);
                            alias_ev.data.alias.anchor_len = alias_len;
                        }
                        queue_push(&ctx->queue, &alias_ev);
                        if (!is_block_scalar) {
                            pos = skip_to_eol(data, len, pos);
                        }
                        continue;
                    }
                    /* Check for tag (!tag or !!tag) */
                    if (pos < len && data[pos] == '!') {
                        pos = parse_tag(data, len, pos,
                                        val_tag, sizeof(val_tag), &val_tag_len);
                        /* Strict mode tag validation */
                        if (ctx->config.strict_mode && val_tag_len > 0) {
                            if (!is_standard_tag(val_tag, val_tag_len)) {
                                emit_error(ctx, "invalid tag: non-standard tag in strict mode");
                                return;
                            }
                        }
                        pos = skip_spaces(data, len, pos);
                    }

                    {
                        int val_is_flow = 0;
                        if (data[pos] == '[') {
                            pos = parse_flow_sequence(ctx, data, len, pos);
                            val_is_flow = 1;
                        } else if (data[pos] == '{') {
                            pos = parse_flow_mapping(ctx, data, len, pos);
                            val_is_flow = 1;
                        } else {
                        int val_found_close = 1;
                        if (data[pos] == '"') {
                            pos = parse_quoted_scalar(data, len, pos, '"',
                                                      val_buf, sizeof(val_buf), &val_len,
                                                      &val_found_close);
                        } else if (data[pos] == '\'') {
                            pos = parse_quoted_scalar(data, len, pos, '\'',
                                                      val_buf, sizeof(val_buf), &val_len,
                                                      &val_found_close);
                        } else if (data[pos] == '|' || data[pos] == '>') {
                            pos = parse_block_scalar(data, len, pos,
                                                     val_buf, sizeof(val_buf), &val_len,
                                                     &val_style);
                            is_block_scalar = 1;
                        } else {
                            pos = parse_plain_scalar(data, len, pos,
                                                     val_buf, sizeof(val_buf), &val_len);
                            /* 1.11: Check for multiline continuation */
                            pos = parse_multiline_continuation(data, len, pos,
                                                                line_indent,
                                                                val_buf, sizeof(val_buf), &val_len);
                        }
                        if (!val_found_close) {
                            emit_error(ctx, "unclosed quoted scalar");
                            return;
                        }
                        } /* end else (not flow) */

                        if (!val_is_flow) {
                        ev.type = YAML_EVENT_SCALAR;
                        memset(&ev.data.scalar, 0, sizeof(ev.data.scalar));
                        if (val_len < YAML_MAX_SCALAR_LEN) {
                            memcpy(ev.data.scalar.value, val_buf, val_len);
                        }
                        ev.data.scalar.value_len = val_len;
                        ev.data.scalar.style = val_style;
                        if (val_tag_len > 0 && val_tag_len < YAML_MAX_TAG_LEN) {
                            memcpy(ev.data.scalar.tag, val_tag, val_tag_len);
                            ev.data.scalar.tag_len = val_tag_len;
                        }
                        queue_push(&ctx->queue, &ev);

                        /* Update knowledge tree */
                        {
                            char kp[1024];
                            build_knowledge_path(ctx, kp, sizeof(kp));
                            if (kp[0] != '\0') {
                                knowledge_insert_path(&ctx->knowledge_root, kp,
                                                      val_buf, val_len);
                            }
                        }
                        } /* end if (!val_is_flow) */
                    }
                }
                /* else: value is on next line (nested collection) — handled by indent */
                /* If anchor was specified for the value, save it for the next collection start */
                if (val_anchor_len > 0) {
                    memcpy(ctx->pending_anchor, val_anchor, val_anchor_len);
                    ctx->pending_anchor[val_anchor_len] = '\0';
                    ctx->pending_anchor_len = val_anchor_len;
                }

                /* For block scalars, pos is already past all content lines;
                 * skip_to_eol would eat the next real line. For inline values,
                 * skip_to_eol advances past the rest of the current line. */
                if (!is_block_scalar) {
                    pos = skip_to_eol(data, len, pos);
                }
                continue;
            }
            } /* end else (not flow start) */
        }

        /* Standalone flow collection */
        if (data[pos] == '[') {
            pos = parse_flow_sequence(ctx, data, len, pos);
            pos = skip_to_eol(data, len, pos);
        } else if (data[pos] == '{') {
            pos = parse_flow_mapping(ctx, data, len, pos);
            pos = skip_to_eol(data, len, pos);
        } else {
        /* Standalone scalar (bare value not in mapping context) */
        {
            char scalar_buf[YAML_MAX_SCALAR_LEN];
            size_t scalar_len = 0;
            size_t new_pos = parse_plain_scalar(data, len, pos,
                                                scalar_buf, sizeof(scalar_buf), &scalar_len);
            if (new_pos > pos && scalar_len > 0) {
                yaml_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = YAML_EVENT_SCALAR;
                if (scalar_len < YAML_MAX_SCALAR_LEN) {
                    memcpy(ev.data.scalar.value, scalar_buf, scalar_len);
                }
                ev.data.scalar.value_len = scalar_len;
                ev.data.scalar.style = YAML_SCALAR_PLAIN;
                queue_push(&ctx->queue, &ev);
            }
            pos = skip_to_eol(data, len, pos);
        }
        }
    }

    /* 12.4: Remove consumed input (all processed bytes) */
    if (pos > 0 && pos <= total_len) {
        memmove(ctx->input_buf, ctx->input_buf + pos, total_len - pos);
        ctx->input_len -= pos;
    }

    /* If all input consumed and we have open levels, close them.
     * This handles implicit document end (no explicit --- or ... marker). */
    if (ctx->input_len == 0 && ctx->indent_depth > 0) {
        close_all_levels(ctx);

        /* Emit implicit DOCUMENT_END and STREAM_END */
        yaml_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = YAML_EVENT_DOCUMENT_END;
        ev.data.document_start.implicit = 1;
        queue_push(&ctx->queue, &ev);

        ev.type = YAML_EVENT_STREAM_END;
        queue_push(&ctx->queue, &ev);
        ctx->state = YAML_STATE_DONE;
    }
}

/* --- Context lifecycle --- */

yaml_ctx_t *yaml_create(yaml_role_t role)
{
    yaml_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.event_queue_size = DEFAULT_EVENT_QUEUE_SIZE;
    cfg.max_document_size = DEFAULT_MAX_DOCUMENT_SIZE;
    cfg.strict_mode = 0;
    return yaml_create_with_config(role, &cfg);
}

yaml_ctx_t *yaml_create_with_config(yaml_role_t role, const yaml_config_t *config)
{
    if (!config) return NULL;
    if (config->event_queue_size != 0 && config->event_queue_size < 2) return NULL;

    yaml_ctx_t *ctx = (yaml_ctx_t *)calloc(1, sizeof(yaml_ctx_t));
    if (!ctx) return NULL;

    ctx->role = role;
    ctx->config = *config;

    size_t qsize = config->event_queue_size > 0 ? config->event_queue_size
                                                 : DEFAULT_EVENT_QUEUE_SIZE;
    if (queue_init(&ctx->queue, qsize) != 0) {
        free(ctx);
        return NULL;
    }

    size_t out_cap = config->max_document_size > 0 ? config->max_document_size
                                                    : DEFAULT_MAX_DOCUMENT_SIZE;
    if (role == YAML_ROLE_SERIALIZER) {
        if (output_init(&ctx->output, out_cap) != 0) {
            queue_cleanup(&ctx->queue);
            free(ctx);
            return NULL;
        }
    }

    /* Input buffer for parser */
    ctx->input_capacity = 4096;
    ctx->input_buf = (uint8_t *)malloc(ctx->input_capacity);
    if (!ctx->input_buf) {
        output_cleanup(&ctx->output);
        queue_cleanup(&ctx->queue);
        free(ctx);
        return NULL;
    }

    ctx->state = YAML_STATE_IDLE;
    ctx->indent_depth = 0;
    memset(ctx->indent_stack, 0, sizeof(ctx->indent_stack));
    memset(ctx->indent_types, 0, sizeof(ctx->indent_types));
    memset(&ctx->knowledge_root, 0, sizeof(ctx->knowledge_root));
    memset(ctx->indent_keys, 0, sizeof(ctx->indent_keys));
    memset(ctx->indent_key_lens, 0, sizeof(ctx->indent_key_lens));
    memset(ctx->indent_seq_idx, 0, sizeof(ctx->indent_seq_idx));

    return ctx;
}

void yaml_destroy(yaml_ctx_t *ctx)
{
    if (!ctx) return;
    queue_cleanup(&ctx->queue);
    output_cleanup(&ctx->output);
    free(ctx->input_buf);
    knowledge_node_cleanup(&ctx->knowledge_root);
    free(ctx);
}

void yaml_reset(yaml_ctx_t *ctx)
{
    if (!ctx) return;
    ctx->queue.head = ctx->queue.tail = ctx->queue.count = 0;
    ctx->output.write_pos = ctx->output.read_pos = ctx->output.count = 0;
    ctx->input_len = 0;
    ctx->state = YAML_STATE_IDLE;
    ctx->indent_depth = 0;
    memset(ctx->indent_stack, 0, sizeof(ctx->indent_stack));
    memset(ctx->indent_types, 0, sizeof(ctx->indent_types));
    ctx->pending_anchor_len = 0;
    knowledge_node_cleanup(&ctx->knowledge_root);
    memset(&ctx->knowledge_root, 0, sizeof(ctx->knowledge_root));
    memset(ctx->indent_keys, 0, sizeof(ctx->indent_keys));
    memset(ctx->indent_key_lens, 0, sizeof(ctx->indent_key_lens));
    memset(ctx->indent_seq_idx, 0, sizeof(ctx->indent_seq_idx));
}

/* --- Core I/O --- */

size_t yaml_feed_input(yaml_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if (!ctx || !data || len == 0) return 0;

    if (ctx->role == YAML_ROLE_PARSER) {
        /* UTF-8 validation in strict mode */
        if (ctx->config.strict_mode && !yaml_validate_utf8(data, len)) {
            yaml_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = YAML_EVENT_ERROR;
            const char *msg = "invalid UTF-8 input";
            size_t msg_len = strlen(msg);
            if (msg_len >= YAML_MAX_ERROR_LEN) msg_len = YAML_MAX_ERROR_LEN - 1;
            memcpy(ev.data.error.message, msg, msg_len);
            ev.data.error.message[msg_len] = '\0';
            ev.data.error.message_len = msg_len;
            queue_push(&ctx->queue, &ev);
            return 0;
        }

        /* Enforce max_document_size: reject input that would exceed the limit */
        size_t max_doc = ctx->config.max_document_size > 0
                         ? ctx->config.max_document_size
                         : DEFAULT_MAX_DOCUMENT_SIZE;
        if (ctx->input_len + len > max_doc) {
            /* Emit error event and consume nothing */
            emit_error(ctx, "input exceeds max_document_size");
            return 0;
        }

        /* Ensure space in input buffer */
        if (ctx->input_len + len > ctx->input_capacity) {
            size_t new_cap = ctx->input_capacity;
            while (new_cap < ctx->input_len + len) new_cap *= 2;
            uint8_t *new_buf = (uint8_t *)realloc(ctx->input_buf, new_cap);
            if (!new_buf) return 0;
            ctx->input_buf = new_buf;
            ctx->input_capacity = new_cap;
        }
        memcpy(ctx->input_buf + ctx->input_len, data, len);
        ctx->input_len += len;
        parse_input(ctx);
        return len;
    } else {
        /* Serializer role: feed_input accepts serialized output bytes
         * (for round-trip scenarios). Store as input for later. */
        if (ctx->input_len + len > ctx->input_capacity) {
            size_t new_cap = ctx->input_capacity;
            while (new_cap < ctx->input_len + len) new_cap *= 2;
            uint8_t *new_buf = (uint8_t *)realloc(ctx->input_buf, new_cap);
            if (!new_buf) return 0;
            ctx->input_buf = new_buf;
            ctx->input_capacity = new_cap;
        }
        memcpy(ctx->input_buf + ctx->input_len, data, len);
        ctx->input_len += len;
        return len;
    }
}

int yaml_next_event(yaml_ctx_t *ctx, yaml_event_t *event)
{
    if (!ctx || !event) return 0;
    return queue_pop(&ctx->queue, event);
}

size_t yaml_get_output(yaml_ctx_t *ctx, uint8_t *buf, size_t max_len)
{
    if (!ctx || !buf || max_len == 0) return 0;
    if (ctx->role != YAML_ROLE_SERIALIZER) return 0;
    return output_drain(&ctx->output, buf, max_len);
}

/* --- Serializer helpers --- */

static int append_str(output_buffer_t *ob, const char *s, size_t len)
{
    return output_append(ob, (const uint8_t *)s, len);
}

static int append_cstr(output_buffer_t *ob, const char *s)
{
    return append_str(ob, s, strlen(s));
}

/* Emit newline + indentation spaces */
static void emit_newline_indent(yaml_ctx_t *ctx)
{
    append_cstr(&ctx->output, "\n");
    for (int i = 0; i < ctx->ser_indent * 2; i++) {
        append_cstr(&ctx->output, " ");
    }
    ctx->ser_at_line_start = 0;
}

int yaml_emit_stream_start(yaml_ctx_t *ctx)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_STREAM_START;
    return queue_push(&ctx->queue, &ev);
}

int yaml_emit_document_start(yaml_ctx_t *ctx, int implicit)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_DOCUMENT_START;
    ev.data.document_start.implicit = implicit;
    queue_push(&ctx->queue, &ev);
    if (!implicit) {
        append_cstr(&ctx->output, "---\n");
    }
    return 0;
}

int yaml_emit_mapping_start(yaml_ctx_t *ctx, const char *tag, const char *anchor)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_MAPPING_START;
    if (tag) {
        size_t tlen = strlen(tag);
        if (tlen < YAML_MAX_TAG_LEN) {
            memcpy(ev.data.collection_start.tag, tag, tlen);
            ev.data.collection_start.tag_len = tlen;
        }
    }
    if (anchor) {
        size_t alen = strlen(anchor);
        if (alen < YAML_MAX_ALIAS_LEN) {
            memcpy(ev.data.collection_start.anchor, anchor, alen);
            ev.data.collection_start.anchor_len = alen;
        }
    }
    ctx->ser_depth++;
    ctx->ser_indent_types[ctx->ser_depth - 1] = INDENT_MAPPING;

    /* 2.4: Flow style output */
    if (ctx->config.flow_output) {
        if (ctx->ser_expect_value) {
            append_cstr(&ctx->output, ": ");
            ctx->ser_expect_value = 0;
        }
        /* Write tag/anchor if present */
        if (tag && tag[0]) {
            append_str(&ctx->output, tag, strlen(tag));
            append_cstr(&ctx->output, " ");
        }
        if (anchor && anchor[0]) {
            append_cstr(&ctx->output, "&");
            append_str(&ctx->output, anchor, strlen(anchor));
            append_cstr(&ctx->output, " ");
        }
        append_cstr(&ctx->output, "{");
        ctx->ser_at_line_start = 0;
        ctx->ser_expect_value = 0;
        return queue_push(&ctx->queue, &ev);
    }

    ctx->ser_indent++;
    /* If previous emit was a key (expect_value=1), emit ":" for collection value */
    if (ctx->ser_expect_value) {
        append_cstr(&ctx->output, ":");
        ctx->ser_expect_value = 0;
    }
    /* Write tag and anchor to output */
    if (tag && tag[0]) {
        if (ctx->ser_at_line_start) emit_newline_indent(ctx);
        append_str(&ctx->output, tag, strlen(tag));
        append_cstr(&ctx->output, " ");
        ctx->ser_at_line_start = 0;
    }
    if (anchor && anchor[0]) {
        if (ctx->ser_at_line_start) emit_newline_indent(ctx);
        append_cstr(&ctx->output, "&");
        append_str(&ctx->output, anchor, strlen(anchor));
        append_cstr(&ctx->output, " ");
        ctx->ser_at_line_start = 0;
    }
    ctx->ser_at_line_start = 1;
    return queue_push(&ctx->queue, &ev);
}

int yaml_emit_sequence_start(yaml_ctx_t *ctx, const char *tag, const char *anchor)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_SEQUENCE_START;
    if (tag) {
        size_t tlen = strlen(tag);
        if (tlen < YAML_MAX_TAG_LEN) {
            memcpy(ev.data.collection_start.tag, tag, tlen);
            ev.data.collection_start.tag_len = tlen;
        }
    }
    if (anchor) {
        size_t alen = strlen(anchor);
        if (alen < YAML_MAX_ALIAS_LEN) {
            memcpy(ev.data.collection_start.anchor, anchor, alen);
            ev.data.collection_start.anchor_len = alen;
        }
    }
    ctx->ser_depth++;
    ctx->ser_indent_types[ctx->ser_depth - 1] = INDENT_SEQUENCE;

    /* 2.4: Flow style output */
    if (ctx->config.flow_output) {
        if (ctx->ser_expect_value) {
            append_cstr(&ctx->output, ": ");
            ctx->ser_expect_value = 0;
        }
        /* Write tag/anchor if present */
        if (tag && tag[0]) {
            append_str(&ctx->output, tag, strlen(tag));
            append_cstr(&ctx->output, " ");
        }
        if (anchor && anchor[0]) {
            append_cstr(&ctx->output, "&");
            append_str(&ctx->output, anchor, strlen(anchor));
            append_cstr(&ctx->output, " ");
        }
        append_cstr(&ctx->output, "[");
        ctx->ser_at_line_start = 0;
        ctx->ser_expect_value = 0;
        return queue_push(&ctx->queue, &ev);
    }

    ctx->ser_indent++;
    /* If previous emit was a key (expect_value=1), emit ":" for collection value */
    if (ctx->ser_expect_value) {
        append_cstr(&ctx->output, ":");
        ctx->ser_expect_value = 0;
    }
    /* Write tag and anchor to output */
    if (tag && tag[0]) {
        if (ctx->ser_at_line_start) emit_newline_indent(ctx);
        append_str(&ctx->output, tag, strlen(tag));
        append_cstr(&ctx->output, " ");
        ctx->ser_at_line_start = 0;
    }
    if (anchor && anchor[0]) {
        if (ctx->ser_at_line_start) emit_newline_indent(ctx);
        append_cstr(&ctx->output, "&");
        append_str(&ctx->output, anchor, strlen(anchor));
        append_cstr(&ctx->output, " ");
        ctx->ser_at_line_start = 0;
    }
    ctx->ser_at_line_start = 1;
    return queue_push(&ctx->queue, &ev);
}

int yaml_emit_scalar(yaml_ctx_t *ctx, const char *value, size_t value_len,
                     const char *tag, yaml_scalar_style_t style)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER || !value) return -1;

    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_SCALAR;
    if (value_len < YAML_MAX_SCALAR_LEN) {
        memcpy(ev.data.scalar.value, value, value_len);
    }
    ev.data.scalar.value_len = value_len;
    ev.data.scalar.style = style;
    if (tag) {
        size_t tlen = strlen(tag);
        if (tlen < YAML_MAX_TAG_LEN) {
            memcpy(ev.data.scalar.tag, tag, tlen);
            ev.data.scalar.tag_len = tlen;
        }
    }
    queue_push(&ctx->queue, &ev);

    /* 2.4: Flow style scalar output */
    if (ctx->config.flow_output) {
        if (ctx->ser_depth > 0 &&
            ctx->ser_indent_types[ctx->ser_depth - 1] == INDENT_MAPPING) {
            if (!ctx->ser_expect_value) {
                /* Key in flow mapping — emit separator if not first key */
                if (ctx->ser_at_line_start) {
                    append_cstr(&ctx->output, ", ");
                }
                if (tag && tag[0]) {
                    append_str(&ctx->output, tag, strlen(tag));
                    append_cstr(&ctx->output, " ");
                }
                append_str(&ctx->output, value, value_len);
                ctx->ser_expect_value = 1;
                ctx->ser_at_line_start = 0;
            } else {
                /* Value in flow mapping */
                append_cstr(&ctx->output, ": ");
                if (tag && tag[0]) {
                    append_str(&ctx->output, tag, strlen(tag));
                    append_cstr(&ctx->output, " ");
                }
                append_str(&ctx->output, value, value_len);
                ctx->ser_expect_value = 0;
                ctx->ser_at_line_start = 1;
            }
        } else if (ctx->ser_depth > 0 &&
                   ctx->ser_indent_types[ctx->ser_depth - 1] == INDENT_SEQUENCE) {
            /* Item in flow sequence — emit separator if not first item */
            if (ctx->ser_at_line_start) {
                append_cstr(&ctx->output, ", ");
            }
            append_str(&ctx->output, value, value_len);
            ctx->ser_at_line_start = 1;
        } else {
            append_str(&ctx->output, value, value_len);
        }
        return 0;
    }

    /* Write to output buffer with indentation */
    if (ctx->ser_depth > 0 &&
        ctx->ser_indent_types[ctx->ser_depth - 1] == INDENT_MAPPING) {
        /* In mapping context */
        if (!ctx->ser_expect_value) {
            /* This is a key — emit newline + indent + key */
            if (ctx->ser_at_line_start) {
                emit_newline_indent(ctx);
            }
            /* Write tag if present */
            if (tag && tag[0]) {
                append_str(&ctx->output, tag, strlen(tag));
                append_cstr(&ctx->output, " ");
            }
            /* Write key */
            int need_quotes = (style != YAML_SCALAR_PLAIN);
            if (!need_quotes) {
                for (size_t i = 0; i < value_len; i++) {
                    uint8_t c = (uint8_t)value[i];
                    if (c == ':' || c == '#' || c == '"' || c == '\'' ||
                        c == '[' || c == ']' || c == '{' || c == '}' ||
                        c == '&' || c == '*' || c == '!' || c == '|' ||
                        c == '>' || c == '%' || c == '@' || c == '`') {
                        need_quotes = 1;
                        break;
                    }
                }
            }
            if (need_quotes) {
                append_cstr(&ctx->output, "\"");
                for (size_t i = 0; i < value_len; i++) {
                    switch ((uint8_t)value[i]) {
                        case '\n': append_cstr(&ctx->output, "\\n"); break;
                        case '\t': append_cstr(&ctx->output, "\\t"); break;
                        case '\\': append_cstr(&ctx->output, "\\\\"); break;
                        case '"':  append_cstr(&ctx->output, "\\\""); break;
                        default:   append_str(&ctx->output, &value[i], 1); break;
                    }
                }
                append_cstr(&ctx->output, "\"");
            } else {
                append_str(&ctx->output, value, value_len);
            }
            ctx->ser_expect_value = 1;
            ctx->ser_at_line_start = 0;
        } else {
            /* This is a value — write ": " + value */
            append_cstr(&ctx->output, ": ");
            /* Write tag if present */
            if (tag && tag[0]) {
                append_str(&ctx->output, tag, strlen(tag));
                append_cstr(&ctx->output, " ");
            }
            /* Handle block scalar styles (literal | and folded >) */
            if (style == YAML_SCALAR_LITERAL || style == YAML_SCALAR_FOLDED) {
                append_cstr(&ctx->output, style == YAML_SCALAR_LITERAL ? "|" : ">");
                /* Write each line of the value indented */
                size_t line_start = 0;
                int block_indent = (ctx->ser_indent + 1) * 2;
                for (size_t i = 0; i <= value_len; i++) {
                    if (i == value_len || value[i] == '\n') {
                        append_cstr(&ctx->output, "\n");
                        for (int s = 0; s < block_indent; s++) {
                            append_cstr(&ctx->output, " ");
                        }
                        append_str(&ctx->output, &value[line_start], i - line_start);
                        line_start = i + 1;
                    }
                }
                ctx->ser_expect_value = 0;
                ctx->ser_at_line_start = 1;
            } else {
            int need_quotes = (style != YAML_SCALAR_PLAIN);
            if (!need_quotes) {
                for (size_t i = 0; i < value_len; i++) {
                    uint8_t c = (uint8_t)value[i];
                    if (c == ':' || c == '#' || c == '"' || c == '\'' ||
                        c == '[' || c == ']' || c == '{' || c == '}' ||
                        c == '&' || c == '*' || c == '!' || c == '|' ||
                        c == '>' || c == '%' || c == '@' || c == '`') {
                        need_quotes = 1;
                        break;
                    }
                }
            }
            if (need_quotes) {
                append_cstr(&ctx->output, "\"");
                for (size_t i = 0; i < value_len; i++) {
                    switch ((uint8_t)value[i]) {
                        case '\n': append_cstr(&ctx->output, "\\n"); break;
                        case '\t': append_cstr(&ctx->output, "\\t"); break;
                        case '\\': append_cstr(&ctx->output, "\\\\"); break;
                        case '"':  append_cstr(&ctx->output, "\\\""); break;
                        default:   append_str(&ctx->output, &value[i], 1); break;
                    }
                }
                append_cstr(&ctx->output, "\"");
            } else {
                append_str(&ctx->output, value, value_len);
            }
            ctx->ser_expect_value = 0;
            ctx->ser_at_line_start = 1;
            } /* end else (not block scalar) */
        }
    } else if (ctx->ser_depth > 0 &&
               ctx->ser_indent_types[ctx->ser_depth - 1] == INDENT_SEQUENCE) {
        /* In sequence context — emit "- " + value */
        if (ctx->ser_at_line_start) {
            emit_newline_indent(ctx);
        }
        append_cstr(&ctx->output, "- ");
        /* Write value */
        {
            int need_quotes = (style != YAML_SCALAR_PLAIN);
            if (!need_quotes) {
                for (size_t i = 0; i < value_len; i++) {
                    uint8_t c = (uint8_t)value[i];
                    if (c == ':' || c == '#' || c == '"' || c == '\'' ||
                        c == '[' || c == ']' || c == '{' || c == '}' ||
                        c == '&' || c == '*' || c == '!' || c == '|' ||
                        c == '>' || c == '%' || c == '@' || c == '`') {
                        need_quotes = 1;
                        break;
                    }
                }
            }
            if (need_quotes) {
                append_cstr(&ctx->output, "\"");
                for (size_t i = 0; i < value_len; i++) {
                    switch ((uint8_t)value[i]) {
                        case '\n': append_cstr(&ctx->output, "\\n"); break;
                        case '\t': append_cstr(&ctx->output, "\\t"); break;
                        case '\\': append_cstr(&ctx->output, "\\\\"); break;
                        case '"':  append_cstr(&ctx->output, "\\\""); break;
                        default:   append_str(&ctx->output, &value[i], 1); break;
                    }
                }
                append_cstr(&ctx->output, "\"");
            } else {
                append_str(&ctx->output, value, value_len);
            }
        }
        ctx->ser_at_line_start = 1;
    } else {
        /* Top-level scalar (not in mapping or sequence) */
        if (ctx->ser_at_line_start) {
            emit_newline_indent(ctx);
        }
        append_str(&ctx->output, value, value_len);
    }

    return 0;
}

int yaml_emit_mapping_end(yaml_ctx_t *ctx)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_MAPPING_END;
    if (ctx->ser_depth > 0) {
        ctx->ser_depth--;
        /* 2.4: In flow mode, don't decrease block indent */
        if (!ctx->config.flow_output) {
            if (ctx->ser_indent > 0) ctx->ser_indent--;
        }
    }
    /* 2.4: Flow style output */
    if (ctx->config.flow_output) {
        append_cstr(&ctx->output, "}");
    }
    ctx->ser_at_line_start = 1;
    ctx->ser_expect_value = 0;
    return queue_push(&ctx->queue, &ev);
}

int yaml_emit_sequence_end(yaml_ctx_t *ctx)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_SEQUENCE_END;
    if (ctx->ser_depth > 0) {
        ctx->ser_depth--;
        /* 2.4: In flow mode, don't decrease block indent */
        if (!ctx->config.flow_output) {
            if (ctx->ser_indent > 0) ctx->ser_indent--;
        }
    }
    /* 2.4: Flow style output */
    if (ctx->config.flow_output) {
        append_cstr(&ctx->output, "]");
    }
    ctx->ser_at_line_start = 1;
    ctx->ser_expect_value = 0;
    return queue_push(&ctx->queue, &ev);
}

int yaml_emit_document_end(yaml_ctx_t *ctx, int implicit)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_DOCUMENT_END;
    ev.data.document_start.implicit = implicit;
    queue_push(&ctx->queue, &ev);
    if (!implicit) {
        append_cstr(&ctx->output, "...\n");
    }
    return 0;
}

int yaml_emit_stream_end(yaml_ctx_t *ctx)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_STREAM_END;
    /* Ensure output ends with newline for proper YAML and incremental parsing */
    if (ctx->output.count > 0) {
        append_cstr(&ctx->output, "\n");
    }
    return queue_push(&ctx->queue, &ev);
}

int yaml_emit_alias(yaml_ctx_t *ctx, const char *name)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER || !name) return -1;
    yaml_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = YAML_EVENT_ALIAS;
    size_t nlen = strlen(name);
    if (nlen < YAML_MAX_ALIAS_LEN) {
        memcpy(ev.data.alias.anchor, name, nlen);
        ev.data.alias.anchor_len = nlen;
    }
    queue_push(&ctx->queue, &ev);

    /* Write *alias to output */
    if (ctx->ser_at_line_start) {
        emit_newline_indent(ctx);
    }
    append_cstr(&ctx->output, "*");
    append_str(&ctx->output, name, nlen);
    ctx->ser_at_line_start = 1;
    ctx->ser_expect_value = 0;

    return 0;
}

/* --- Knowledge / Merge helpers --- */

int yaml_merge_events(yaml_ctx_t *dst, yaml_ctx_t *src)
{
    if (!dst || !src) return -1;
    if (dst->role != YAML_ROLE_SERIALIZER) return -1;

    int count = 0;
    yaml_event_t ev;
    while (yaml_next_event(src, &ev)) {
        switch (ev.type) {
            case YAML_EVENT_STREAM_START:
                yaml_emit_stream_start(dst);
                break;
            case YAML_EVENT_DOCUMENT_START:
                yaml_emit_document_start(dst, ev.data.document_start.implicit);
                break;
            case YAML_EVENT_MAPPING_START:
                yaml_emit_mapping_start(dst,
                    ev.data.collection_start.tag_len > 0 ? ev.data.collection_start.tag : NULL,
                    ev.data.collection_start.anchor_len > 0 ? ev.data.collection_start.anchor : NULL);
                break;
            case YAML_EVENT_SEQUENCE_START:
                yaml_emit_sequence_start(dst,
                    ev.data.collection_start.tag_len > 0 ? ev.data.collection_start.tag : NULL,
                    ev.data.collection_start.anchor_len > 0 ? ev.data.collection_start.anchor : NULL);
                break;
            case YAML_EVENT_SCALAR:
                yaml_emit_scalar(dst, ev.data.scalar.value, ev.data.scalar.value_len,
                    ev.data.scalar.tag_len > 0 ? ev.data.scalar.tag : NULL,
                    ev.data.scalar.style);
                break;
            case YAML_EVENT_MAPPING_END:
                yaml_emit_mapping_end(dst);
                break;
            case YAML_EVENT_SEQUENCE_END:
                yaml_emit_sequence_end(dst);
                break;
            case YAML_EVENT_DOCUMENT_END:
                yaml_emit_document_end(dst, ev.data.document_start.implicit);
                break;
            case YAML_EVENT_STREAM_END:
                yaml_emit_stream_end(dst);
                break;
            default:
                break;
        }
        count++;
    }
    return count;
}

const char *yaml_lookup_scalar(yaml_ctx_t *ctx, const char *path, size_t *out_len)
{
    if (!ctx || !path) return NULL;
    const knowledge_node_t *node = knowledge_lookup(&ctx->knowledge_root, path, out_len);
    if (node && node->value) return node->value;
    return NULL;
}

int yaml_serialize_kv(yaml_ctx_t *ctx, const char *key, const char *value,
                      size_t value_len)
{
    if (!ctx || ctx->role != YAML_ROLE_SERIALIZER || !key || !value) return -1;

    /* Direct-write only: write key: value to output buffer.
     * No event queueing — this is a convenience shortcut. */
    if (ctx->ser_at_line_start) {
        emit_newline_indent(ctx);
    }
    append_str(&ctx->output, key, strlen(key));
    append_cstr(&ctx->output, ": ");
    append_str(&ctx->output, value, value_len);
    ctx->ser_at_line_start = 1;

    return 0;
}

/* --- Status / Query API --- */

/* Map yaml_state_t enum to string */
static const char *state_to_string(yaml_state_t state)
{
    switch (state) {
        case YAML_STATE_IDLE:         return "IDLE";
        case YAML_STATE_STREAM:       return "STREAM";
        case YAML_STATE_DOCUMENT:     return "DOCUMENT";
        case YAML_STATE_MAPPING_KEY:  return "MAPPING_KEY";
        case YAML_STATE_MAPPING_VALUE: return "MAPPING_VALUE";
        case YAML_STATE_SEQUENCE:     return "SEQUENCE";
        case YAML_STATE_DONE:         return "DONE";
        case YAML_STATE_ERROR:        return "ERROR";
        default:                      return "UNKNOWN";
    }
}

const char *yaml_parser_state(yaml_ctx_t *ctx)
{
    if (!ctx) return NULL;
    if (ctx->role != YAML_ROLE_PARSER) return NULL;
    return state_to_string(ctx->state);
}

int yaml_depth(yaml_ctx_t *ctx)
{
    if (!ctx) return -1;
    if (ctx->role == YAML_ROLE_PARSER) {
        return ctx->indent_depth;
    }
    return ctx->ser_depth;
}

int yaml_has_pending_events(yaml_ctx_t *ctx)
{
    if (!ctx) return 0;
    return ctx->queue.count > 0 ? 1 : 0;
}

size_t yaml_event_count(yaml_ctx_t *ctx)
{
    if (!ctx) return 0;
    return ctx->queue.count;
}

size_t yaml_output_pending(yaml_ctx_t *ctx)
{
    if (!ctx) return 0;
    if (ctx->role != YAML_ROLE_SERIALIZER) return 0;
    return ctx->output.count;
}

size_t yaml_output_size(yaml_ctx_t *ctx)
{
    if (!ctx) return 0;
    return ctx->output.count;
}

int yaml_queue_available(yaml_ctx_t *ctx)
{
    if (!ctx) return -1;
    return (int)(ctx->queue.capacity - ctx->queue.count);
}

size_t yaml_queue_capacity(yaml_ctx_t *ctx)
{
    if (!ctx) return 0;
    return ctx->queue.capacity;
}

/* --- Knowledge tree enumeration --- */

size_t yaml_get_child_count(yaml_ctx_t *ctx, const char *path)
{
    if (!ctx || !path) return 0;
    const knowledge_node_t *node;
    if (*path == '\0') {
        node = &ctx->knowledge_root;
    } else {
        node = knowledge_lookup(&ctx->knowledge_root, path, NULL);
    }
    if (!node) return 0;
    return node->child_count;
}

int yaml_get_child_key(yaml_ctx_t *ctx, const char *path, size_t index,
                       const char **out_key, size_t *out_key_len)
{
    if (!ctx || !out_key || !out_key_len) return -1;
    const knowledge_node_t *node;
    if (*path == '\0') {
        node = &ctx->knowledge_root;
    } else {
        node = knowledge_lookup(&ctx->knowledge_root, path, NULL);
    }
    if (!node || index >= node->child_count) return -1;
    *out_key = node->children[index].key;
    *out_key_len = node->children[index].key ? strlen(node->children[index].key) : 0;
    return 0;
}

/* --- Knowledge tree statistics --- */

size_t yaml_knowledge_size(yaml_ctx_t *ctx)
{
    if (!ctx) return 0;
    size_t count = 0;
    for (size_t i = 0; i < ctx->knowledge_root.child_count; i++) {
        knowledge_count_values(&ctx->knowledge_root.children[i], &count);
    }
    return count;
}

size_t yaml_knowledge_depth(yaml_ctx_t *ctx)
{
    if (!ctx) return 0;
    size_t max_d = 0;
    for (size_t i = 0; i < ctx->knowledge_root.child_count; i++) {
        knowledge_max_depth(&ctx->knowledge_root.children[i], 1, &max_d);
    }
    return max_d;
}
