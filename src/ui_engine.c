/**
 * @file ui_engine.c
 * @brief Generic data-driven UI engine implementation.
 */

#include "ui_engine.h"
#include "ui_actions.h"
#include "cjson/mini_json.h"
#include <string.h>
#include <stdio.h>

/**********************
 *   ID REGISTRY (for ui_engine_find_by_id)
 **********************/
#define ID_TABLE_MAX 32
static struct { char id[32]; lv_obj_t * obj; } id_table[ID_TABLE_MAX];
static int id_table_count = 0;

static void register_id(const char * id, lv_obj_t * obj)
{
    if(!id || !obj || id_table_count >= ID_TABLE_MAX) return;
    strncpy(id_table[id_table_count].id, id, sizeof(id_table[0].id) - 1);
    id_table[id_table_count].id[sizeof(id_table[0].id) - 1] = '\0';
    id_table[id_table_count].obj = obj;
    id_table_count++;
}

lv_obj_t * ui_engine_find_by_id(const char * id)
{
    if(!id) return NULL;
    for(int i = 0; i < id_table_count; i++) {
        if(strcmp(id_table[i].id, id) == 0) return id_table[i].obj;
    }
    return NULL;
}

/**********************
 *   ACTION BINDING (trampoline: bridges LVGL event → action with param)
 **********************/
/* LVGL event cb signature is void(*)(lv_event_t*), but our actions need an
 * extra param string. We store {action_func, param} as user_data and use this
 * trampoline to call the action with the param. */
typedef struct {
    void (*func)(lv_event_t * e, const char * param);
    char * param;   /* malloc'd copy of the param (or NULL) */
} action_binding_t;

static void action_trampoline_cb(lv_event_t * e)
{
    action_binding_t * b = (action_binding_t *)lv_event_get_user_data(e);
    if(b && b->func) b->func(e, b->param);
}

/* Bind an event string like "open_page:bt_page" to obj on the given event code. */
static void bind_action(lv_obj_t * obj, const char * action_str, lv_event_code_t code)
{
    if(!action_str) return;

    /* split "name:param" at first ':' */
    char name[32];
    const char * param_src = NULL;
    const char * colon = strchr(action_str, ':');
    if(colon) {
        int len = (int)(colon - action_str);
        if(len >= (int)sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, action_str, len);
        name[len] = '\0';
        param_src = colon + 1;
    }
    else {
        strncpy(name, action_str, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    const ui_action_t * a = ui_actions_find(name);
    if(!a) return;

    /* build binding (lives for obj's lifetime) */
    action_binding_t * b = lv_malloc(sizeof(action_binding_t));
    b->func = a->func;
    b->param = param_src ? lv_strdup(param_src) : NULL;
    lv_obj_add_event_cb(obj, action_trampoline_cb, code, b);
}

/**********************
 *   HELPERS
 **********************/
static int32_t get_int(mj_value * obj, const char * key, int32_t def)
{
    mj_value * v = mj_get(obj, key);
    if(!v) return def;
    return (int32_t)mj_num(v);
}

static const char * get_str(mj_value * obj, const char * key, const char * def)
{
    mj_value * v = mj_get(obj, key);
    if(!v) return def;
    return mj_str(v);
}

lv_color_t ui_engine_parse_color(const char * hex)
{
    if(!hex || hex[0] != '#' ) return lv_color_black();
    /* skip '#', parse 6 hex digits */
    uint32_t r = 0, g = 0, b = 0;
    if(sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
        return lv_color_hex((r << 16) | (g << 8) | b);
    }
    return lv_color_black();
}

const char * ui_engine_symbol(const char * name)
{
    if(!name) return LV_SYMBOL_BULLET;
    if(strcmp(name, "AUDIO") == 0)        return LV_SYMBOL_AUDIO;
    if(strcmp(name, "LIST") == 0)         return LV_SYMBOL_LIST;
    if(strcmp(name, "SETTINGS") == 0)     return LV_SYMBOL_SETTINGS;
    if(strcmp(name, "HOME") == 0)         return LV_SYMBOL_HOME;
    if(strcmp(name, "WIFI") == 0)         return LV_SYMBOL_WIFI;
    if(strcmp(name, "BLUETOOTH") == 0)    return LV_SYMBOL_BLUETOOTH;
    if(strcmp(name, "BELL") == 0)         return LV_SYMBOL_BELL;
    if(strcmp(name, "BATTERY") == 0)      return LV_SYMBOL_BATTERY_FULL;
    if(strcmp(name, "PLAY") == 0)         return LV_SYMBOL_PLAY;
    if(strcmp(name, "PAUSE") == 0)        return LV_SYMBOL_PAUSE;
    if(strcmp(name, "REFRESH") == 0)      return LV_SYMBOL_REFRESH;
    if(strcmp(name, "IMAGE") == 0)        return LV_SYMBOL_IMAGE;
    if(strcmp(name, "FILE") == 0)         return LV_SYMBOL_FILE;
    if(strcmp(name, "CLOSE") == 0)        return LV_SYMBOL_CLOSE;
    if(strcmp(name, "OK") == 0)           return LV_SYMBOL_OK;
    if(strcmp(name, "LEFT") == 0)         return LV_SYMBOL_LEFT;
    if(strcmp(name, "RIGHT") == 0)        return LV_SYMBOL_RIGHT;
    if(strcmp(name, "UP") == 0)           return LV_SYMBOL_UP;
    if(strcmp(name, "DOWN") == 0)         return LV_SYMBOL_DOWN;
    if(strcmp(name, "PLUS") == 0)         return LV_SYMBOL_PLUS;
    if(strcmp(name, "MINUS") == 0)        return LV_SYMBOL_MINUS;
    return LV_SYMBOL_BULLET;
}

/**********************
 *   FILE READING
 **********************/
char * ui_engine_read_file(const char * path)
{
    uint32_t size = 0;
    lv_fs_res_t res = lv_fs_path_get_size(path, &size);
    if(res != LV_FS_RES_OK || size == 0) {
        LV_LOG_ERROR("ui_engine: cannot get size of '%s' (res=%d)", path, res);
        return NULL;
    }

    char * buf = lv_malloc(size + 1);
    if(!buf) return NULL;

    lv_fs_file_t f;
    res = lv_fs_open(&f, path, LV_FS_MODE_RD);
    if(res != LV_FS_RES_OK) {
        LV_LOG_ERROR("ui_engine: cannot open '%s' (res=%d)", path, res);
        lv_free(buf);
        return NULL;
    }

    uint32_t br = 0;
    res = lv_fs_read(&f, buf, size, &br);
    lv_fs_close(&f);
    if(res != LV_FS_RES_OK || br == 0) {
        LV_LOG_ERROR("ui_engine: read failed (res=%d, br=%d)", res, (int)br);
        lv_free(buf);
        return NULL;
    }
    buf[br] = '\0';
    return buf;
}

/**********************
 *   CONTROL CREATION
 **********************/
static lv_obj_t * create_one_control(lv_obj_t * parent, mj_value * node)
{
    const char * type = get_str(node, "type", NULL);
    if(!type) return NULL;

    int32_t x = get_int(node, "x", 0);
    int32_t y = get_int(node, "y", 0);
    int32_t w = get_int(node, "w", LV_SIZE_CONTENT);
    const char * color_hex = get_str(node, "color", NULL);

    lv_obj_t * obj = NULL;

    if(strcmp(type, "label") == 0) {
        obj = lv_label_create(parent);
        const char * text = get_str(node, "text", "");
        lv_label_set_text(obj, text);
        /* default to white text (our screens have dark backgrounds);
         * JSON can override with "color" */
        if(color_hex) lv_obj_set_style_text_color(obj, ui_engine_parse_color(color_hex), 0);
        else lv_obj_set_style_text_color(obj, lv_color_white(), 0);
    }
    else if(strcmp(type, "button") == 0) {
        obj = lv_button_create(parent);
        if(color_hex) lv_obj_set_style_bg_color(obj, ui_engine_parse_color(color_hex), 0);
        const char * text = get_str(node, "text", NULL);
        if(text) {
            lv_obj_t * lbl = lv_label_create(obj);
            lv_label_set_text(lbl, text);
            lv_obj_center(lbl);
        }
    }
    else if(strcmp(type, "slider") == 0) {
        obj = lv_slider_create(parent);
        int32_t rmin = get_int(node, "range_min", 0);
        int32_t rmax = get_int(node, "range_max", 100);
        int32_t val = get_int(node, "value", rmin);
        lv_slider_set_range(obj, rmin, rmax);
        lv_slider_set_value(obj, val, LV_ANIM_OFF);
    }
    else if(strcmp(type, "switch") == 0) {
        obj = lv_switch_create(parent);
        bool checked = mj_bool(mj_get(node, "checked"));
        if(checked) lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    else if(strcmp(type, "image") == 0) {
        obj = lv_image_create(parent);
        const char * sym = get_str(node, "symbol", NULL);
        if(sym) lv_image_set_src(obj, ui_engine_symbol(sym));
    }
    else if(strcmp(type, "list_item") == 0) {
        /* A clickable row: icon + text, used in list pages.
         * Clicking triggers "on_click" (usually open_page:<id>).
         * Positioned by x/y like other controls; default width 240. */
        obj = lv_obj_create(parent);
        lv_obj_set_size(obj, 240, 50);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x2a2a4e), 0);
        lv_obj_set_style_radius(obj, 8, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_pad_left(obj, 15, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

        const char * sym = get_str(node, "symbol", NULL);
        if(sym) {
            lv_obj_t * sym_lbl = lv_label_create(obj);
            lv_label_set_text(sym_lbl, ui_engine_symbol(sym));
            lv_obj_set_style_text_color(sym_lbl, lv_color_white(), 0);
            lv_obj_align(sym_lbl, LV_ALIGN_LEFT_MID, 0, 0);
        }
        const char * text = get_str(node, "text", "");
        lv_obj_t * txt_lbl = lv_label_create(obj);
        lv_label_set_text(txt_lbl, text);
        lv_obj_set_style_text_color(txt_lbl, lv_color_white(), 0);
        lv_obj_align(txt_lbl, LV_ALIGN_LEFT_MID, 30, 0);
    }
    else {
        LV_LOG_WARN("ui_engine: unknown control type '%s'", type);
        return NULL;
    }

    /* common: position + size */
    lv_obj_set_pos(obj, x, y);
    if(w != LV_SIZE_CONTENT) lv_obj_set_width(obj, w);

    /* register id (for ui_engine_find_by_id) */
    const char * id = get_str(node, "id", NULL);
    if(id) register_id(id, obj);

    /* event binding: "on_change" (slider/switch) and "on_click" (button/list_item) */
    const char * on_change = get_str(node, "on_change", NULL);
    const char * on_click = get_str(node, "on_click", NULL);
    if(on_change) {
        lv_event_code_t code = (strcmp(type, "button") == 0) ? LV_EVENT_CLICKED : LV_EVENT_VALUE_CHANGED;
        bind_action(obj, on_change, code);
    }
    if(on_click) {
        bind_action(obj, on_click, LV_EVENT_CLICKED);
    }

    return obj;
}

/**********************
 *   PUBLIC API
 **********************/
int ui_engine_build_controls(lv_obj_t * parent, const char * json_str)
{
    if(!json_str) return 0;

    /* reset ID table for this page (ids are page-scoped) */
    id_table_count = 0;

    mj_value * root = mj_parse(json_str);
    if(!root) {
        LV_LOG_ERROR("ui_engine: JSON parse failed");
        return 0;
    }

    /* optional: apply title/background */
    const char * bg = get_str(root, "bg_color", NULL);
    if(bg) lv_obj_set_style_bg_color(parent, ui_engine_parse_color(bg), 0);
    const char * title_text = get_str(root, "title", NULL);
    if(title_text) {
        lv_obj_t * title = lv_label_create(parent);
        lv_label_set_text(title, title_text);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    }

    mj_value * controls = mj_get(root, "controls");
    if(!controls) {
        LV_LOG_WARN("ui_engine: no 'controls' array in JSON");
        mj_free(root);
        return 0;
    }

    int n = mj_count(controls);
    int created = 0;
    for(int i = 0; i < n; i++) {
        mj_value * node = mj_at(controls, i);
        if(create_one_control(parent, node)) created++;
    }

    mj_free(root);
    LV_LOG_USER("ui_engine: built %d controls", created);
    return created;
}
