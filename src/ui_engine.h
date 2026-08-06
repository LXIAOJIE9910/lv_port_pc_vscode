/**
 * @file ui_engine.h
 * @brief Generic data-driven UI engine: build LVGL widget trees from JSON.
 *
 * The engine reads a JSON file (via LVGL's file system), parses it, and
 * recursively creates LVGL widgets per the "controls" array. It also binds
 * events to C actions (from ui_actions.h) by name, so "which control triggers
 * which action" is hot-changeable in JSON while execution stays in fast C.
 *
 * Supported control types: label, button, slider, switch, image (symbol).
 * Supported properties: x, y, w, h, text, color, range_min/max, value, on_change.
 */

#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * Read a file via LVGL FS into a malloc'd buffer (null-terminated).
 * @param path  e.g. "A:apps.json"
 * @return      malloc'd string, caller must lv_free(). NULL on error.
 */
char * ui_engine_read_file(const char * path);

/**
 * Build a controls tree from a JSON string onto a parent.
 * The JSON format: { "controls": [ {type, x, y, ...}, ... ] }
 * Events are bound via ui_actions by the "on_change" field.
 * @return  number of controls created.
 */
int ui_engine_build_controls(lv_obj_t * parent, const char * json_str);

/**
 * Parse a "#rrggbb" color string into lv_color_t.
 */
lv_color_t ui_engine_parse_color(const char * hex);

/**
 * Map a symbol name (e.g. "AUDIO") to LVGL's symbol string (LV_SYMBOL_AUDIO).
 * @return  the symbol string, or LV_SYMBOL_BULLET if unknown.
 */
const char * ui_engine_symbol(const char * name);

/**
 * Look up a control by its "id" field (set during build).
 * @return  the LVGL object, or NULL if not found. Lets C actions operate on
 *          controls by id (e.g. ui_engine_find_by_id("list_devices")).
 */
lv_obj_t * ui_engine_find_by_id(const char * id);

#ifdef __cplusplus
}
#endif

#endif /*UI_ENGINE_H*/
