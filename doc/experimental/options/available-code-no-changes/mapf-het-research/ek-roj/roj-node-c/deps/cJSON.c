/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
*/

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

#include "cJSON.h"

/* Minimal cJSON implementation for ROJ */

static void *cJSON_malloc(size_t size) {
    return malloc(size);
}

static void cJSON_free(void *ptr) {
    free(ptr);
}

static cJSON *cJSON_New_Item(void) {
    cJSON *node = (cJSON *)cJSON_malloc(sizeof(cJSON));
    if (node) {
        memset(node, 0, sizeof(cJSON));
    }
    return node;
}

void cJSON_Delete(cJSON *item) {
    cJSON *next = NULL;
    while (item != NULL) {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && item->child) {
            cJSON_Delete(item->child);
        }
        if (!(item->type & cJSON_IsReference) && item->valuestring) {
            cJSON_free(item->valuestring);
        }
        if (!(item->type & cJSON_StringIsConst) && item->string) {
            cJSON_free(item->string);
        }
        cJSON_free(item);
        item = next;
    }
}

static const char *skip_whitespace(const char *in) {
    while (in && *in && (*in <= 32)) in++;
    return in;
}

static const char *parse_string(cJSON *item, const char *str) {
    const char *ptr = str + 1;
    const char *end_ptr = ptr;
    size_t len = 0;
    char *out;

    if (*str != '\"') return NULL;

    while (*end_ptr != '\"' && *end_ptr) {
        if (*end_ptr++ == '\\') end_ptr++;
    }

    len = end_ptr - ptr;
    out = (char *)cJSON_malloc(len + 1);
    if (!out) return NULL;

    char *out_ptr = out;
    while (ptr < end_ptr) {
        if (*ptr != '\\') {
            *out_ptr++ = *ptr++;
        } else {
            ptr++;
            switch (*ptr) {
                case 'b': *out_ptr++ = '\b'; break;
                case 'f': *out_ptr++ = '\f'; break;
                case 'n': *out_ptr++ = '\n'; break;
                case 'r': *out_ptr++ = '\r'; break;
                case 't': *out_ptr++ = '\t'; break;
                default: *out_ptr++ = *ptr; break;
            }
            ptr++;
        }
    }
    *out_ptr = '\0';

    item->valuestring = out;
    item->type = cJSON_String;
    return end_ptr + 1;
}

static const char *parse_number(cJSON *item, const char *num) {
    double n = 0, sign = 1, scale = 0;
    int subscale = 0, signsubscale = 1;

    if (*num == '-') { sign = -1; num++; }
    if (*num == '0') num++;
    if (*num >= '1' && *num <= '9') {
        do { n = (n * 10.0) + (*num++ - '0'); } while (*num >= '0' && *num <= '9');
    }
    if (*num == '.' && num[1] >= '0' && num[1] <= '9') {
        num++;
        do { n = (n * 10.0) + (*num++ - '0'); scale--; } while (*num >= '0' && *num <= '9');
    }
    if (*num == 'e' || *num == 'E') {
        num++;
        if (*num == '+') num++;
        else if (*num == '-') { signsubscale = -1; num++; }
        while (*num >= '0' && *num <= '9') subscale = (subscale * 10) + (*num++ - '0');
    }

    n = sign * n * pow(10.0, (scale + subscale * signsubscale));
    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return num;
}

static const char *parse_value(cJSON *item, const char *value);

static const char *parse_array(cJSON *item, const char *value) {
    cJSON *child = NULL;
    if (*value != '[') return NULL;

    item->type = cJSON_Array;
    value = skip_whitespace(value + 1);
    if (*value == ']') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!child) return NULL;
    value = skip_whitespace(parse_value(child, skip_whitespace(value)));
    if (!value) return NULL;

    while (*value == ',') {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
        if (!value) return NULL;
    }

    if (*value == ']') return value + 1;
    return NULL;
}

static const char *parse_object(cJSON *item, const char *value) {
    cJSON *child = NULL;
    if (*value != '{') return NULL;

    item->type = cJSON_Object;
    value = skip_whitespace(value + 1);
    if (*value == '}') return value + 1;

    item->child = child = cJSON_New_Item();
    if (!child) return NULL;
    value = skip_whitespace(parse_string(child, skip_whitespace(value)));
    if (!value) return NULL;
    child->string = child->valuestring;
    child->valuestring = NULL;
    if (*value != ':') return NULL;
    value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
    if (!value) return NULL;

    while (*value == ',') {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip_whitespace(parse_string(child, skip_whitespace(value + 1)));
        if (!value) return NULL;
        child->string = child->valuestring;
        child->valuestring = NULL;
        if (*value != ':') return NULL;
        value = skip_whitespace(parse_value(child, skip_whitespace(value + 1)));
        if (!value) return NULL;
    }

    if (*value == '}') return value + 1;
    return NULL;
}

static const char *parse_value(cJSON *item, const char *value) {
    if (!value) return NULL;
    if (!strncmp(value, "null", 4)) { item->type = cJSON_NULL; return value + 4; }
    if (!strncmp(value, "false", 5)) { item->type = cJSON_False; return value + 5; }
    if (!strncmp(value, "true", 4)) { item->type = cJSON_True; item->valueint = 1; return value + 4; }
    if (*value == '\"') return parse_string(item, value);
    if (*value == '-' || (*value >= '0' && *value <= '9')) return parse_number(item, value);
    if (*value == '[') return parse_array(item, value);
    if (*value == '{') return parse_object(item, value);
    return NULL;
}

cJSON *cJSON_Parse(const char *value) {
    cJSON *item = cJSON_New_Item();
    if (!item) return NULL;
    if (!parse_value(item, skip_whitespace(value))) {
        cJSON_Delete(item);
        return NULL;
    }
    return item;
}

int cJSON_GetArraySize(const cJSON *array) {
    cJSON *child = array ? array->child : NULL;
    int size = 0;
    while (child) { size++; child = child->next; }
    return size;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index) {
    cJSON *child = array ? array->child : NULL;
    while (child && index > 0) { index--; child = child->next; }
    return child;
}

cJSON *cJSON_GetObjectItem(const cJSON *const object, const char *const string) {
    cJSON *child = object ? object->child : NULL;
    while (child && strcmp(child->string, string) != 0) child = child->next;
    return child;
}

/* Printing */
static char *print_number(const cJSON *item) {
    char *str = (char *)cJSON_malloc(64);
    if (!str) return NULL;
    double d = item->valuedouble;
    if (d == 0) strcpy(str, "0");
    else if (fabs(((double)item->valueint) - d) <= DBL_EPSILON && d <= INT_MAX && d >= INT_MIN)
        sprintf(str, "%d", item->valueint);
    else if (fabs(floor(d) - d) <= DBL_EPSILON && fabs(d) < 1.0e60)
        sprintf(str, "%.0f", d);
    else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9)
        sprintf(str, "%e", d);
    else
        sprintf(str, "%f", d);
    return str;
}

static char *print_string_ptr(const char *str) {
    if (!str) return strdup("\"\"");
    size_t len = strlen(str) + 3;
    char *out = (char *)cJSON_malloc(len);
    if (!out) return NULL;
    sprintf(out, "\"%s\"", str);
    return out;
}

static char *print_value(const cJSON *item, int fmt);

static char *print_array(const cJSON *item, int fmt) {
    cJSON *child = item->child;
    size_t len = 2;
    char *out, *ptr;

    /* Count length */
    cJSON *c = child;
    int numentries = 0;
    while (c) { numentries++; c = c->next; }
    if (!numentries) return strdup("[]");

    char **entries = (char **)cJSON_malloc(numentries * sizeof(char *));
    if (!entries) return NULL;

    c = child;
    for (int i = 0; i < numentries; i++) {
        entries[i] = print_value(c, fmt);
        if (!entries[i]) { for (int j = 0; j < i; j++) cJSON_free(entries[j]); cJSON_free(entries); return NULL; }
        len += strlen(entries[i]) + 1;
        c = c->next;
    }

    out = (char *)cJSON_malloc(len + 1);
    if (!out) { for (int i = 0; i < numentries; i++) cJSON_free(entries[i]); cJSON_free(entries); return NULL; }

    *out = '[';
    ptr = out + 1;
    for (int i = 0; i < numentries; i++) {
        strcpy(ptr, entries[i]);
        ptr += strlen(entries[i]);
        if (i < numentries - 1) *ptr++ = ',';
        cJSON_free(entries[i]);
    }
    *ptr++ = ']';
    *ptr = '\0';
    cJSON_free(entries);
    return out;
}

static char *print_object(const cJSON *item, int fmt) {
    cJSON *child = item->child;
    int numentries = 0;
    cJSON *c = child;
    while (c) { numentries++; c = c->next; }
    if (!numentries) return strdup("{}");

    char **names = (char **)cJSON_malloc(numentries * sizeof(char *));
    char **entries = (char **)cJSON_malloc(numentries * sizeof(char *));
    if (!names || !entries) { cJSON_free(names); cJSON_free(entries); return NULL; }

    size_t len = 2;
    c = child;
    for (int i = 0; i < numentries; i++) {
        names[i] = print_string_ptr(c->string);
        entries[i] = print_value(c, fmt);
        if (!names[i] || !entries[i]) {
            for (int j = 0; j <= i; j++) { cJSON_free(names[j]); cJSON_free(entries[j]); }
            cJSON_free(names); cJSON_free(entries);
            return NULL;
        }
        len += strlen(names[i]) + strlen(entries[i]) + 2;
        c = c->next;
    }

    char *out = (char *)cJSON_malloc(len + 1);
    if (!out) {
        for (int i = 0; i < numentries; i++) { cJSON_free(names[i]); cJSON_free(entries[i]); }
        cJSON_free(names); cJSON_free(entries);
        return NULL;
    }

    *out = '{';
    char *ptr = out + 1;
    for (int i = 0; i < numentries; i++) {
        strcpy(ptr, names[i]); ptr += strlen(names[i]);
        *ptr++ = ':';
        strcpy(ptr, entries[i]); ptr += strlen(entries[i]);
        if (i < numentries - 1) *ptr++ = ',';
        cJSON_free(names[i]); cJSON_free(entries[i]);
    }
    *ptr++ = '}';
    *ptr = '\0';
    cJSON_free(names); cJSON_free(entries);
    return out;
}

static char *print_value(const cJSON *item, int fmt) {
    (void)fmt;
    if (!item) return NULL;
    switch ((item->type) & 0xFF) {
        case cJSON_NULL: return strdup("null");
        case cJSON_False: return strdup("false");
        case cJSON_True: return strdup("true");
        case cJSON_Number: return print_number(item);
        case cJSON_String: return print_string_ptr(item->valuestring);
        case cJSON_Array: return print_array(item, fmt);
        case cJSON_Object: return print_object(item, fmt);
        default: return NULL;
    }
}

char *cJSON_Print(const cJSON *item) { return print_value(item, 1); }
char *cJSON_PrintUnformatted(const cJSON *item) { return print_value(item, 0); }

/* Creation */
cJSON *cJSON_CreateNull(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_NULL; return item; }
cJSON *cJSON_CreateTrue(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_True; return item; }
cJSON *cJSON_CreateFalse(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_False; return item; }
cJSON *cJSON_CreateBool(int b) { cJSON *item = cJSON_New_Item(); if (item) item->type = b ? cJSON_True : cJSON_False; return item; }

cJSON *cJSON_CreateNumber(double num) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}

cJSON *cJSON_CreateString(const char *string) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_String;
        item->valuestring = string ? strdup(string) : strdup("");
    }
    return item;
}

cJSON *cJSON_CreateArray(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Array; return item; }
cJSON *cJSON_CreateObject(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Object; return item; }

cJSON *cJSON_CreateStringArray(const char *const *strings, int count) {
    cJSON *a = cJSON_CreateArray();
    if (!a) return NULL;
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(a, cJSON_CreateString(strings[i]));
    }
    return a;
}

static int add_item_to_array(cJSON *array, cJSON *item) {
    if (!item || !array) return 0;
    cJSON *child = array->child;
    if (!child) {
        array->child = item;
    } else {
        while (child->next) child = child->next;
        child->next = item;
        item->prev = child;
    }
    return 1;
}

int cJSON_AddItemToArray(cJSON *array, cJSON *item) {
    return add_item_to_array(array, item);
}

int cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) {
    if (!item || !object) return 0;
    if (item->string) cJSON_free(item->string);
    item->string = strdup(string);
    return add_item_to_array(object, item);
}

cJSON *cJSON_AddNullToObject(cJSON *const object, const char *const name) {
    cJSON *null_item = cJSON_CreateNull();
    if (cJSON_AddItemToObject(object, name, null_item)) return null_item;
    cJSON_Delete(null_item);
    return NULL;
}

cJSON *cJSON_AddTrueToObject(cJSON *const object, const char *const name) {
    cJSON *true_item = cJSON_CreateTrue();
    if (cJSON_AddItemToObject(object, name, true_item)) return true_item;
    cJSON_Delete(true_item);
    return NULL;
}

cJSON *cJSON_AddFalseToObject(cJSON *const object, const char *const name) {
    cJSON *false_item = cJSON_CreateFalse();
    if (cJSON_AddItemToObject(object, name, false_item)) return false_item;
    cJSON_Delete(false_item);
    return NULL;
}

cJSON *cJSON_AddBoolToObject(cJSON *const object, const char *const name, const int boolean) {
    cJSON *bool_item = cJSON_CreateBool(boolean);
    if (cJSON_AddItemToObject(object, name, bool_item)) return bool_item;
    cJSON_Delete(bool_item);
    return NULL;
}

cJSON *cJSON_AddNumberToObject(cJSON *const object, const char *const name, const double number) {
    cJSON *number_item = cJSON_CreateNumber(number);
    if (cJSON_AddItemToObject(object, name, number_item)) return number_item;
    cJSON_Delete(number_item);
    return NULL;
}

cJSON *cJSON_AddStringToObject(cJSON *const object, const char *const name, const char *const string) {
    cJSON *string_item = cJSON_CreateString(string);
    if (cJSON_AddItemToObject(object, name, string_item)) return string_item;
    cJSON_Delete(string_item);
    return NULL;
}
