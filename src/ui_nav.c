/**
 * @file ui_nav.c
 * @brief Unified navigation stack with home as the root.
 *
 * One stack holds all screens: home (bottom) → app screens → sub-pages (top).
 * Any Back pops one level. Home is the immutable root (Back stops there).
 *
 * Two ways to enter a screen:
 *   - ui_nav_push_screen(screen): for app root screens (built by app_launcher)
 *   - ui_nav_open_page(page_id):  for sub-pages (lazy-loaded from A:pages/<id>.json)
 * Both push onto the same stack. Back is unified.
 *
 * Screens are cached (never destroyed on pop), so control state is preserved
 * across navigation. Only the nav position is lost when you leave an app
 * (re-entering an app shows its root screen, not the last sub-page).
 */

#include "ui_nav.h"
#include "ui_engine.h"
#include <string.h>

/*********************
 *   CONFIG
 *********************/
#define NAV_STACK_MAX   8       /* max depth: home + apps + sub-pages */
#define PAGE_CACHE_MAX  16      /* max distinct sub-pages cached */

/**********************
 *   STATE
 **********************/
static lv_obj_t * nav_stack[NAV_STACK_MAX];
static int nav_top = -1;

/* Sub-page cache: {page_id → screen}, re-opening reuses UI + state. */
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
void ui_nav_set_root(lv_obj_t * root_screen)
{
    nav_top = 0;
    nav_stack[0] = root_screen;
    /* caller loads the root screen itself */
}

void ui_nav_push_screen(lv_obj_t * screen)
{
    if(nav_top < 0) return;
    if(nav_top >= NAV_STACK_MAX - 1) {
        LV_LOG_WARN("ui_nav: stack overflow");
        return;
    }
    nav_stack[++nav_top] = screen;
    lv_screen_load(screen);
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

        ui_nav_add_back_button(screen);
        cache_page(page_id, screen);
    }

    nav_stack[++nav_top] = screen;
    lv_screen_load(screen);
    LV_LOG_USER("ui_nav: open '%s' (depth %d)", page_id, nav_top);
}

void ui_nav_back(void)
{
    if(nav_top <= 0) return;   /* at home (root), stop */
    nav_top--;
    lv_screen_load(nav_stack[nav_top]);
    LV_LOG_USER("ui_nav: back (depth %d)", nav_top);
}

/**********************
 *   BACK BUTTON (used by app screens + sub-pages, NOT home)
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
