/**
 * @file ui_nav.ori.c
 * @brief [ARCHIVE — original "app-as-root" scheme, for reference only]
 *
 * This is the PREVIOUS version of ui_nav.c, before switching to scheme 1
 * (home as unified root). Kept for comparison/learning. NOT compiled —
 * the active version is ui_nav.c (scheme 1, home as root).
 *
 * Difference from current ui_nav.c:
 *   - Scheme 2 ("app-as-root"): each app's root screen is the stack bottom.
 *     ui_nav_init(root_screen) RESETS the stack to just that app's screen
 *     every time you enter an app. Sub-pages push above it; Back pops to the
 *     app root; the app root's own Back button (in app_launcher.c) returns
 *     to home via a SEPARATE back handler.
 *   - Current scheme 1 ("home-as-root"): home is the immutable stack bottom.
 *     Entering an app PUSHES (no reset); all Back buttons use ui_nav_back
 *     uniformly; no duplicate back-button code in app_launcher.
 *
 * Key API differences vs current:
 *   - ui_nav_init(root_screen)  ← scheme 2: reset stack to app root
 *   - (no ui_nav_push_screen)   ← scheme 2 doesn't need it (apps use lv_screen_load + own back)
 *   - ui_nav_open_page(id)      ← same idea, but stack bottom is the app, not home
 *   - ui_nav_back()             ← pops to app root (not home); stops at app root
 */

#include "ui_nav.h"
#include "ui_engine.h"
#include <string.h>

/*********************
 *   CONFIG
 *********************/
#define NAV_STACK_MAX   8       /* max nesting depth (app root + sub-pages) */
#define PAGE_CACHE_MAX  16      /* max distinct sub-pages cached */

/**********************
 *   STATE
 **********************/
/* The navigation stack: screens from app root (bottom) to current (top). */
static lv_obj_t * nav_stack[NAV_STACK_MAX];
static int nav_top = -1;

/* Sub-page cache: {page_id → screen}, so re-opening a page reuses its UI. */
static char page_cache_id[PAGE_CACHE_MAX][32];
static lv_obj_t * page_cache_screen[PAGE_CACHE_MAX];
static int page_cache_count = 0;

/**********************
 *   HELPERS
 **********************/
static lv_obj_t * find_cached_page(const char * page_id)
{
    for(int i = 0; i < page_cache_count; i++) {
        if(strcmp(page_cache_id[i], page_id) == 0) return page_cache_screen[i];
    }
    return NULL;
}

static void cache_page(const char * page_id, lv_obj_t * screen)
{
    if(page_cache_count >= PAGE_CACHE_MAX) return;
    strncpy(page_cache_id[page_cache_count], page_id, sizeof(page_cache_id[0]) - 1);
    page_cache_id[page_cache_count][sizeof(page_cache_id[0]) - 1] = '\0';
    page_cache_screen[page_cache_count] = screen;
    page_cache_count++;
}

/**********************
 *   PUBLIC API
 **********************/
/* scheme 2: reset the stack to contain just this app's root screen.
 * Called by app_launcher's enter_json_app/enter_builtin each time an app is entered. */
void ui_nav_init(lv_obj_t * root_screen)
{
    nav_top = 0;
    nav_stack[0] = root_screen;
    /* Note: don't load here — caller already loaded the root screen. */
}

void ui_nav_open_page(const char * page_id)
{
    if(!page_id || nav_top < 0) return;
    if(nav_top >= NAV_STACK_MAX - 1) {
        LV_LOG_WARN("ui_nav: stack overflow, cannot open '%s'", page_id);
        return;
    }

    /* Lazy-load: reuse cached screen or build from JSON */
    lv_obj_t * screen = find_cached_page(page_id);
    if(!screen) {
        /* Build path "A:pages/<id>.json" */
        char path[64];
        lv_snprintf(path, sizeof(path), "A:pages/%s.json", page_id);

        char * json = ui_engine_read_file(path);
        if(!json) {
            LV_LOG_ERROR("ui_nav: cannot read page '%s' (%s)", page_id, path);
            return;
        }
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);
        ui_engine_build_controls(screen, json);
        lv_free(json);

        /* Add a Back button so every sub-page can return */
        ui_nav_add_back_button(screen);

        cache_page(page_id, screen);
    }

    nav_stack[++nav_top] = screen;
    lv_screen_load(screen);
    LV_LOG_USER("ui_nav: open '%s' (depth %d)", page_id, nav_top);
}

/* scheme 2: pop to app root. Stops at the app root (nav_top==0), which is
 * NOT home — returning to home is handled by app_launcher's own back button. */
void ui_nav_back(void)
{
    if(nav_top <= 0) {
        LV_LOG_WARN("ui_nav: already at root, cannot go back");
        return;
    }
    nav_top--;
    lv_screen_load(nav_stack[nav_top]);
    LV_LOG_USER("ui_nav: back (depth %d)", nav_top);
}

/**********************
 *   BACK BUTTON
 **********************/
static void back_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    ui_nav_back();
}

void ui_nav_add_back_button(lv_obj_t * screen)
{
    lv_obj_t * btn = lv_button_create(screen);
    lv_obj_set_size(btn, 70, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
}
