/**
 * @file mini_json.c
 * @brief Minimal JSON parser implementation.
 *
 * Recursive descent parser. Uses lv_malloc/lv_free so LVGL memory monitoring
 * (LV_USE_ASSERT_MEM_INTEGRITY) covers JSON parsing too.
 */

#include "mini_json.h"
#include "lvgl/lvgl.h"   /* for lv_malloc / lv_free / lv_memcpy / lv_memzero */
#include <string.h>
#include <stdlib.h>      /* atof */

/**********************
 *   STATIC PROTOTYPES
 **********************/
static mj_value * parse_value(const char ** p);
static mj_value * parse_string(const char ** p);
static mj_value * parse_number(const char ** p);
static mj_value * parse_array(const char ** p);
static mj_value * parse_object(const char ** p);
static mj_value * mj_new(mj_type_t type);
static void skip_ws(const char ** p);
static char * dup_str(const char * s, int len);

/**********************
 *   HELPERS
 **********************/
static void skip_ws(const char ** p)
{
    while(** p == ' ' || ** p == '\t' || ** p == '\n' || ** p == '\r') (*p)++;
}

static mj_value * mj_new(mj_type_t type)
{
    mj_value * v = lv_malloc(sizeof(mj_value));
    lv_memzero(v, sizeof(mj_value));
    v->type = type;
    return v;
}

/* Duplicate a string (len chars) using lv_malloc, null-terminated */
static char * dup_str(const char * s, int len)
{
    char * d = lv_malloc(len + 1);
    lv_memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

/**********************
 *   PARSERS
 **********************/
/* Parse a string (already past the opening quote). Returns null-terminated content. */
static mj_value * parse_string(const char ** p)
{
    /* *p points at the opening quote */
    (*p)++; /* skip " */
    /* find end quote, handle escapes minimally */
    const char * start = *p;
    int len = 0;
    const char * s = *p;
    while(* s && * s != '"') {
        if(* s == '\\') {
            s++;
            if(* s) s++;
            len += 2;
        }
        else {
            s++;
            len++;
        }
    }
    /* build unescaped string */
    char * out = lv_malloc(len + 1);
    int oi = 0;
    const char * q = start;
    while(* q && * q != '"') {
        if(* q == '\\') {
            q++;
            switch(* q) {
                case 'n':  out[oi++] = '\n'; break;
                case 't':  out[oi++] = '\t'; break;
                case 'r':  out[oi++] = '\r'; break;
                case '"':  out[oi++] = '"';  break;
                case '\\': out[oi++] = '\\'; break;
                case '/':  out[oi++] = '/';  break;
                case 'u':
                    /* skip 4 hex, keep as-is (not fully supporting unicode) */
                    out[oi++] = '?';
                    q += 4;
                    break;
                default: out[oi++] = * q; break;
            }
            q++;
        }
        else {
            out[oi++] = * q;
            q++;
        }
    }
    out[oi] = '\0';
    *p = q; /* points at closing quote */
    if(** p == '"') (*p)++;

    mj_value * v = mj_new(MJ_STRING);
    v->str = out;
    return v;
}

static mj_value * parse_number(const char ** p)
{
    const char * start = *p;
    if(** p == '-' || ** p == '+') (*p)++;
    while((** p >= '0' && ** p <= '9') || ** p == '.' || ** p == 'e' || ** p == 'E' || ** p == '+' || ** p == '-') (*p)++;
    int len = (int)(*p - start);
    char * tmp = dup_str(start, len);
    double d = atof(tmp);
    lv_free(tmp);

    mj_value * v = mj_new(MJ_NUMBER);
    v->num = d;
    v->str = dup_str(start, len);  /* keep raw text too, in case needed */
    return v;
}

static mj_value * parse_array(const char ** p)
{
    (*p)++; /* skip [ */
    mj_value * arr = mj_new(MJ_ARRAY);
    mj_value * last = NULL;
    skip_ws(p);
    if(** p == ']') { (*p)++; return arr; }
    while(1) {
        skip_ws(p);
        mj_value * item = parse_value(p);
        if(!item) { mj_free(arr); return NULL; }
        if(last) last->next = item; else arr->child = item;
        last = item;
        skip_ws(p);
        if(** p == ',') { (*p)++; continue; }
        if(** p == ']') { (*p)++; break; }
        mj_free(arr);
        return NULL;
    }
    return arr;
}

static mj_value * parse_object(const char ** p)
{
    (*p)++; /* skip { */
    mj_value * obj = mj_new(MJ_OBJECT);
    mj_value * last = NULL;
    skip_ws(p);
    if(** p == '}') { (*p)++; return obj; }
    while(1) {
        skip_ws(p);
        if(** p != '"') { mj_free(obj); return NULL; }
        mj_value * key = parse_string(p);
        if(!key) { mj_free(obj); return NULL; }
        skip_ws(p);
        if(** p != ':') { mj_free(key); mj_free(obj); return NULL; }
        (*p)++;
        skip_ws(p);
        mj_value * val = parse_value(p);
        if(!val) { mj_free(key); mj_free(obj); return NULL; }
        val->key = key->str;   /* transfer ownership of the string */
        lv_free(key);          /* free the wrapper mj_value, keep the str */
        if(last) last->next = val; else obj->child = val;
        last = val;
        skip_ws(p);
        if(** p == ',') { (*p)++; continue; }
        if(** p == '}') { (*p)++; break; }
        mj_free(obj);
        return NULL;
    }
    return obj;
}

static mj_value * parse_value(const char ** p)
{
    skip_ws(p);
    if(** p == '"') return parse_string(p);
    if(** p == '{') return parse_object(p);
    if(** p == '[') return parse_array(p);
    if(** p == '-' || ** p == '+' || (** p >= '0' && ** p <= '9')) return parse_number(p);
    /* literals */
    if(strncmp(*p, "true", 4) == 0)  { *p += 4; return mj_new(MJ_TRUE); }
    if(strncmp(*p, "false", 5) == 0) { *p += 5; return mj_new(MJ_FALSE); }
    if(strncmp(*p, "null", 4) == 0)  { *p += 4; return mj_new(MJ_NULL); }
    return NULL;
}

/**********************
 *   PUBLIC API
 **********************/
mj_value * mj_parse(const char * json)
{
    if(!json) return NULL;
    const char * p = json;
    mj_value * v = parse_value(&p);
    if(!v) {
        LV_LOG_ERROR("mini_json: parse failed");
    }
    return v;
}

void mj_free(mj_value * v)
{
    if(!v) return;
    /* free children (linked list) */
    mj_value * c = v->child;
    while(c) {
        mj_value * next = c->next;
        mj_free(c);
        c = next;
    }
    if(v->str) lv_free(v->str);
    if(v->key) lv_free(v->key);
    lv_free(v);
}

mj_value * mj_get(mj_value * obj, const char * key)
{
    if(!obj || obj->type != MJ_OBJECT) return NULL;
    mj_value * c = obj->child;
    while(c) {
        if(c->key && strcmp(c->key, key) == 0) return c;
        c = c->next;
    }
    return NULL;
}

mj_value * mj_at(mj_value * arr, int index)
{
    if(!arr || (arr->type != MJ_ARRAY && arr->type != MJ_OBJECT)) return NULL;
    mj_value * c = arr->child;
    int i = 0;
    while(c) {
        if(i == index) return c;
        i++;
        c = c->next;
    }
    return NULL;
}

const char * mj_str(mj_value * v)
{
    if(!v || v->type != MJ_STRING) return NULL;
    return v->str;
}

double mj_num(mj_value * v)
{
    if(!v || v->type != MJ_NUMBER) return 0;
    return v->num;
}

bool mj_bool(mj_value * v)
{
    return v && v->type == MJ_TRUE;
}

int mj_count(mj_value * arr)
{
    if(!arr) return 0;
    int n = 0;
    mj_value * c = arr->child;
    while(c) { n++; c = c->next; }
    return n;
}
