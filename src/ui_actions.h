/**
 * @file ui_actions.h
 * @brief C action library for the data-driven UI.
 *
 * JSON describes "what triggers what" (event bindings via the "on_change" field),
 * but the actual actions are C functions compiled into the firmware. This keeps
 * behavior fast and debuggable while letting the JSON bindings be hot-changed.
 *
 * Architecture:
 *   JSON:  {"on_change": "set_brightness"}   <- binding (hot-changeable)
 *   C:     action table: {"set_brightness", set_brightness_cb}  <- execution (compiled)
 */

#ifndef UI_ACTIONS_H
#define UI_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * An action: a name (referenced by JSON "on_change"/"on_click") and the C
 * function to call. The function receives the LVGL event plus an optional
 * param string parsed from the JSON binding (e.g. "open_page:bt_page" →
 * action="open_page", param="bt_page").
 */
typedef struct {
    const char * name;
    void (*func)(lv_event_t * e, const char * param);
} ui_action_t;

/**
 * Look up an action by name.
 * @return the action, or NULL if not found.
 */
const ui_action_t * ui_actions_find(const char * name);

/**
 * Get the full action table (for the UI engine to iterate).
 */
const ui_action_t * ui_actions_table(void);

/**
 * Number of actions in the table.
 */
int ui_actions_count(void);

#ifdef __cplusplus
}
#endif

#endif /*UI_ACTIONS_H*/
