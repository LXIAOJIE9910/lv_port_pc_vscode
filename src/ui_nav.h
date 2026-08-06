/**
 * @file ui_nav.h
 * @brief Page navigation stack for the data-driven UI framework.
 *
 * Manages a stack of screens so sub-pages can be pushed (open_page) and
 * popped (back). Each sub-page is lazy-loaded from JSON on first open and
 * cached for reuse. Sub-page JSON files live under pages/<id>.json.
 *
 * Convention: page id "bt_page" → file "A:pages/bt_page.json".
 */

#ifndef UI_NAV_H
#define UI_NAV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

/**
 * Set the home screen as the navigation root (stack bottom).
 * Called once at startup. Home is the immutable root — Back stops here.
 */
void ui_nav_set_root(lv_obj_t * root_screen);

/**
 * Push an arbitrary screen onto the stack (e.g. an app's root screen built
 * by app_launcher). Loads it. Used for non-JSON screens.
 */
void ui_nav_push_screen(lv_obj_t * screen);

/**
 * Open a sub-page by id. Lazy-loads from A:pages/<id>.json on first open,
 * then caches. Pushes onto nav stack and loads the screen.
 */
void ui_nav_open_page(const char * page_id);

/**
 * Go back to the previous page (pop the nav stack).
 * If at the root, does nothing (root stays).
 */
void ui_nav_back(void);

/**
 * Add a "Back" button (top-left) to a screen. Clicking it calls ui_nav_back.
 * Used to give every sub-page a way to return without JSON defining it.
 */
void ui_nav_add_back_button(lv_obj_t * screen);

#ifdef __cplusplus
}
#endif

#endif /*UI_NAV_H*/
