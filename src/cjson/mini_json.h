/**
 * @file mini_json.h
 * @brief Minimal JSON parser for the data-driven UI engine.
 *
 * A tiny single-file JSON parser supporting the subset needed by the UI engine:
 * objects, arrays, strings, numbers, booleans, null. Enough to parse the UI
 * description JSON files (apps.json, settings_ui.json).
 *
 * Uses lv_malloc/lv_free for memory (so LVGL's memory monitoring covers it).
 *
 * Why not cJSON? The dev environment has no network access to fetch cJSON.
 * This hand-written parser is smaller and sufficient for our needs.
 */

#ifndef MINI_JSON_H
#define MINI_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MJ_NULL,
    MJ_FALSE,
    MJ_TRUE,
    MJ_NUMBER,
    MJ_STRING,
    MJ_ARRAY,
    MJ_OBJECT,
} mj_type_t;

typedef struct mj_value {
    mj_type_t type;
    /* string value (null-terminated) for MJ_STRING, MJ_NUMBER(parsed text) */
    char * str;
    /* for MJ_NUMBER: parsed double */
    double num;
    /* for MJ_ARRAY / MJ_OBJECT: linked list of children */
    struct mj_value * child;
    struct mj_value * next;     /* next sibling */
    /* for MJ_OBJECT: key name of this member */
    char * key;
} mj_value;

/**
 * Parse a JSON string into an mj_value tree.
 * @param json  null-terminated JSON text
 * @return      root value, or NULL on parse error. Caller must mj_free() it.
 */
mj_value * mj_parse(const char * json);

/**
 * Free a parsed JSON tree.
 */
void mj_free(mj_value * v);

/**
 * Get a member of an object by key.
 * @return the member value, or NULL if not found.
 */
mj_value * mj_get(mj_value * obj, const char * key);

/**
 * Get the i-th element of an array.
 * @return the element value, or NULL if out of range.
 */
mj_value * mj_at(mj_value * arr, int index);

/**
 * Get string value. Returns v->str for MJ_STRING, NULL otherwise.
 */
const char * mj_str(mj_value * v);

/**
 * Get number value. Returns v->num for MJ_NUMBER, 0 otherwise.
 */
double mj_num(mj_value * v);

/**
 * Get boolean value. Returns true for MJ_TRUE, false otherwise.
 */
bool mj_bool(mj_value * v);

/**
 * Count array/object children.
 */
int mj_count(mj_value * arr);

#ifdef __cplusplus
}
#endif

#endif /*MINI_JSON_H*/
