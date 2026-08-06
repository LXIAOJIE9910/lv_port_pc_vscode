/**
 * @file ui_actions.c
 * @brief C action implementations for data-driven UI.
 *
 * Two kinds of actions:
 *   - Built-in (framework): open_page, back — navigation, reused by all apps.
 *   - Business (user): toggle_bluetooth, set_brightness, etc — call real drivers.
 *
 * JSON binds controls to these by name, optionally with a param after ':'.
 * E.g. "on_click": "open_page:bt_page"  →  func(open_page_cb, param="bt_page").
 */

#include "ui_actions.h"
#include "ui_nav.h"
#include <stdio.h>
#include <string.h>

/**********************
 *   BUILT-IN NAV ACTIONS (framework, all apps reuse)
 **********************/
/* open_page:<id> — open a sub-page (lazy-load from A:pages/<id>.json), push onto nav stack */
static void open_page_cb(lv_event_t * e, const char * param)
{
    LV_UNUSED(e);
    if(!param || !param[0]) {
        LV_LOG_WARN("open_page: missing page id");
        return;
    }
    ui_nav_open_page(param);
}

/* back — pop the nav stack, return to previous page */
static void back_cb(lv_event_t * e, const char * param)
{
    LV_UNUSED(e);
    LV_UNUSED(param);
    ui_nav_back();
}

/**********************
 *   BUSINESS ACTIONS (user-defined, call real drivers)
 **********************/
static void set_brightness_cb(lv_event_t * e, const char * param)
{
    LV_UNUSED(param);
    lv_obj_t * slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    LV_LOG_USER("set_brightness: %d", (int)v);
    /* TODO: call real backlight driver, e.g. hal_backlight_set(v); */
}

static void toggle_bluetooth_cb(lv_event_t * e, const char * param)
{
    LV_UNUSED(param);
    lv_obj_t * sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    LV_LOG_USER("toggle_bluetooth: %s", on ? "ON" : "OFF");
    /* TODO: call real BLE driver */
}

static void toggle_wifi_cb(lv_event_t * e, const char * param)
{
    LV_UNUSED(param);
    lv_obj_t * sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    LV_LOG_USER("toggle_wifi: %s", on ? "ON" : "OFF");
    /* TODO: call real WiFi driver */
}

static void set_signal_cb(lv_event_t * e, const char * param)
{
    LV_UNUSED(param);
    lv_obj_t * slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    LV_LOG_USER("set_signal: %d", (int)v);
    /* TODO: read real RSSI or set TX power */
}

static void toggle_hotspot_cb(lv_event_t * e, const char * param)
{
    LV_UNUSED(param);
    lv_obj_t * sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    LV_LOG_USER("toggle_hotspot: %s", on ? "ON" : "OFF");
}

/**********************
 *   ACTION TABLE
 **********************/
static const ui_action_t actions[] = {
    /* built-in navigation */
    { "open_page",          open_page_cb          },
    { "back",               back_cb               },
    /* business */
    { "set_brightness",     set_brightness_cb     },
    { "toggle_bluetooth",   toggle_bluetooth_cb   },
    { "toggle_wifi",        toggle_wifi_cb        },
    { "set_signal",         set_signal_cb         },
    { "toggle_hotspot",     toggle_hotspot_cb     },
};

const ui_action_t * ui_actions_find(const char * name)
{
    if(!name) return NULL;
    for(int i = 0; i < (int)(sizeof(actions) / sizeof(actions[0])); i++) {
        if(strcmp(actions[i].name, name) == 0) return &actions[i];
    }
    LV_LOG_WARN("ui_actions: action '%s' not found", name);
    return NULL;
}

const ui_action_t * ui_actions_table(void)
{
    return actions;
}

int ui_actions_count(void)
{
    return (int)(sizeof(actions) / sizeof(actions[0]));
}
