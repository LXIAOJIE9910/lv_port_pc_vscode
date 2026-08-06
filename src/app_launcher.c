/**
 * @file app_launcher.c
 * @brief Data-driven phone-like home screen.
 *
 * Architecture (JSON + C behavior):
 *   - apps.json (external file, hot-changeable) describes the app list:
 *     name, icon symbol, color, and how to launch (builtin C app or json-defined UI).
 *   - C code reads apps.json, generates app icons in a loop, and routes taps.
 *   - builtin apps (music/widgets): run the LVGL demo on a dedicated screen (C behavior).
 *   - json apps (e.g. Settings): ui_engine builds their UI from a JSON file too.
 *
 * Changing the home layout (add/remove/reorder apps, rename, recolor) only
 * requires editing apps.json and restarting — NO recompilation.
 *
 * Multi-screen + lazy-load: each app's screen is built on first entry and
 * kept in memory (state preserved across navigation).
 */

#include "app_launcher.h"
#include "ui_engine.h"
#include "ui_nav.h"
#include "cjson/mini_json.h"
#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include <string.h>
#include <stdint.h>

/*********************
 *   STATIC SCREENS
 *********************/
static lv_obj_t * home_screen;

/* Parsed apps.json tree, kept alive for icon callbacks to look up app metadata. */
static mj_value * g_apps_root = NULL;
static mj_value * g_apps_array = NULL;

/* Lazy-init flags for builtin apps (their screens are static) */
static lv_obj_t * music_screen;
static lv_obj_t * widgets_screen;
static bool music_inited = false;
static bool widgets_inited = false;

/* For json apps: cache one screen per APP (indexed by app position in apps.json).
 * Keying by app index (not ui_file path) ensures two different apps that happen
 * to share the same JSON file get independent screen instances with independent state. */
#define JSON_APP_MAX 16
static lv_obj_t * json_screen_cache[JSON_APP_MAX];

/**********************
 *   FORWARD DECLS
  **********************/
static void home_create(void);
static lv_obj_t * create_app_icon(lv_obj_t * grid, mj_value * app, int index, int total);
static void app_icon_event_cb(lv_event_t * e);
static void enter_builtin(const char * entry);
static void enter_json_app(int app_index, const char * ui_file);

/**********************
 *   HOME SCREEN
 *********************/
void app_launcher_create(void)
{
    home_screen = lv_obj_create(NULL);
    home_create();
    lv_screen_load(home_screen);
    /* home is the unified nav root — all app/sub-page screens push above it */
    ui_nav_set_root(home_screen);
}

static void home_create(void)
{
    /* Read apps.json */
    char * json = ui_engine_read_file("A:apps.json");
    if(!json) {
        LV_LOG_ERROR("app_launcher: cannot read apps.json. Falling back to empty home.");
        lv_obj_set_style_bg_color(home_screen, lv_color_hex(0x1a1a2e), 0);
        lv_obj_t * err = lv_label_create(home_screen);
        lv_label_set_text(err, "apps.json not found");
        lv_obj_center(err);
        lv_free(json);
        return;
    }

    mj_value * root = mj_parse(json);
    if(!root) {
        LV_LOG_ERROR("app_launcher: apps.json parse failed");
        lv_free(json);
        return;
    }

    /* Home background */
    mj_value * home = mj_get(root, "home");
    const char * bg = home ? mj_str(mj_get(home, "bg_color")) : NULL;
    const char * bg_grad = home ? mj_str(mj_get(home, "bg_grad_color")) : NULL;
    lv_obj_set_style_bg_color(home_screen, ui_engine_parse_color(bg ? bg : "#1a1a2e"), 0);
    lv_obj_set_style_bg_grad_color(home_screen, ui_engine_parse_color(bg_grad ? bg_grad : "#16213e"), 0);
    lv_obj_set_style_bg_grad_dir(home_screen, LV_GRAD_DIR_VER, 0);

    /* Title */
    const char * title_text = home ? mj_str(mj_get(home, "title")) : "Apps";
    lv_obj_t * title = lv_label_create(home_screen);
    lv_label_set_text(title, title_text ? title_text : "Apps");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 25);

    /* Grid: 2 columns, auto rows */
    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_t * grid = lv_obj_create(home_screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(grid, 30, 0);
    lv_obj_set_style_pad_column(grid, 30, 0);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_align(grid, LV_ALIGN_CENTER, 0, 30);

    /* Build an icon per app entry */
    mj_value * apps = mj_get(root, "apps");
    int n = mj_count(apps);
    for(int i = 0; i < n; i++) {
        mj_value * app = mj_at(apps, i);
        create_app_icon(grid, app, i, n);
    }

    /* Keep the parsed JSON tree alive for the program lifetime so icon
     * callbacks can look up app type/entry/ui_file by index. The tree is small
     * (a few app entries). In production you could snapshot into a struct array
     * and free the tree, but this is simpler and safe. */
    g_apps_root = root;
    g_apps_array = apps;
    /* NOTE: 'json' buffer is also kept (root's strings point into it via dup_str,
     * which copies — so json can actually be freed now). */
    lv_free(json);
}

/**********************
 *   APP ICON CREATION
 **********************/
static lv_obj_t * create_app_icon(lv_obj_t * grid, mj_value * app, int index, int total)
{
    const char * name = mj_str(mj_get(app, "name"));
    const char * symbol = mj_str(mj_get(app, "symbol"));
    const char * color_hex = mj_str(mj_get(app, "color"));

    lv_obj_t * icon = lv_obj_create(grid);
    lv_obj_set_size(icon, 90, 110);
    lv_obj_set_style_bg_color(icon, ui_engine_parse_color(color_hex ? color_hex : "#6f8af6"), 0);
    lv_obj_set_style_radius(icon, 20, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_shadow_width(icon, 20, 0);
    lv_obj_set_style_shadow_color(icon, ui_engine_parse_color(color_hex ? color_hex : "#6f8af6"), 0);
    lv_obj_set_style_shadow_opa(icon, LV_OPA_40, 0);

    int col = index % 2;
    int row = index / 2;
    lv_obj_set_grid_cell(icon, LV_GRID_ALIGN_CENTER, col, 1, LV_GRID_ALIGN_CENTER, row, 1);

    lv_obj_set_flex_flow(icon, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(icon, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(icon, false);

    lv_obj_t * sym = lv_label_create(icon);
    lv_label_set_text(sym, ui_engine_symbol(symbol));
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(sym, lv_color_white(), 0);

    lv_obj_t * lbl = lv_label_create(icon);
    lv_label_set_text(lbl, name ? name : "?");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    /* Pass app index as user_data so the callback can look it up.
     * We re-read apps.json-free metadata via a small static struct array. */
    lv_obj_add_event_cb(icon, app_icon_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    return icon;
}

/**********************
 *   APP ENTRY DISPATCH
 **********************/
static void app_icon_event_cb(lv_event_t * e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if(!g_apps_array) return;
    mj_value * app = mj_at(g_apps_array, index);
    if(!app) return;

    const char * type = mj_str(mj_get(app, "type"));
    const char * entry = mj_str(mj_get(app, "entry"));
    const char * ui_file = mj_str(mj_get(app, "ui_file"));

    if(strcmp(type ? type : "", "builtin") == 0) {
        enter_builtin(entry ? entry : "");
    }
    else if(strcmp(type ? type : "", "json") == 0) {
        enter_json_app(index, ui_file ? ui_file : "");
    }
}

static void enter_builtin(const char * entry)
{
    if(strcmp(entry, "music") == 0) {
        if(!music_inited) {
            music_screen = lv_obj_create(NULL);
            lv_demo_args_t args;
            lv_demo_args_init(&args);
            args.parent = music_screen;
            lv_demo_music_with_args(&args);
            ui_nav_add_back_button(music_screen);
            music_inited = true;
        }
        ui_nav_push_screen(music_screen);
    }
    else if(strcmp(entry, "widgets") == 0) {
        if(!widgets_inited) {
            widgets_screen = lv_obj_create(NULL);
            lv_demo_args_t args;
            lv_demo_args_init(&args);
            args.parent = widgets_screen;
            lv_demo_widgets_with_args(&args);
            ui_nav_add_back_button(widgets_screen);
            widgets_inited = true;
        }
        ui_nav_push_screen(widgets_screen);
    }
}

static void enter_json_app(int app_index, const char * ui_file)
{
    if(!ui_file || !ui_file[0]) return;
    if(app_index < 0 || app_index >= JSON_APP_MAX) {
        LV_LOG_WARN("app_launcher: app_index %d out of range", app_index);
        return;
    }

    /* lazy cache: each app (by index) gets its own screen instance.
     * Two apps sharing the same JSON file get independent screens/state. */
    if(json_screen_cache[app_index]) {
        ui_nav_push_screen(json_screen_cache[app_index]);
        return;
    }

    /* build new screen from JSON */
    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);

    char * json = ui_engine_read_file(ui_file);
    if(json) {
        ui_engine_build_controls(screen, json);
        lv_free(json);
    }
    ui_nav_add_back_button(screen);

    json_screen_cache[app_index] = screen;
    ui_nav_push_screen(screen);
}
