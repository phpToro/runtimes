/*
 * phptoro_ext.c — Minimal PHP extension for phpToro.
 *
 * Exposes a single function to PHP:
 *
 *   phptoro_native_call(string $namespace, string $method, string $args_json): mixed
 *
 * This is the ONLY bridge between PHP and native code. The PHP framework
 * wraps this in a developer-friendly API (e.g. phptoro('state')->get('key')),
 * but underneath it all goes through this single C function.
 *
 * The host (Swift/Kotlin) registers a native handler via
 * phptoro_set_native_handler() before PHP init. All routing, plugin
 * resolution, and dispatch happens on the native side.
 */

#include "php.h"
#include "ext/json/php_json.h"

#include <string.h>
#include <stdlib.h>

/* ── Native handler (set by host before init) ──────────────────────────── */

typedef char *(*phptoro_native_handler_t)(const char *ns, const char *method, const char *args_json);

static phptoro_native_handler_t g_native_handler = NULL;

void phptoro_set_native_handler(phptoro_native_handler_t handler) {
    g_native_handler = handler;
}

/* ── phptoro_native_call() ─────────────────────────────────────────────── */

/*
 * PHP signature:
 *   phptoro_native_call(string $namespace, string $method, string $args_json): mixed
 *
 * Returns the JSON-decoded result from the native handler, or false on error.
 */
ZEND_BEGIN_ARG_INFO_EX(arginfo_phptoro_native_call, 0, 0, 3)
    ZEND_ARG_TYPE_INFO(0, namespace, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, method, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, args_json, IS_STRING, 0)
ZEND_END_ARG_INFO()

PHP_FUNCTION(phptoro_native_call)
{
    zend_string *ns, *method, *args_json;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STR(ns)
        Z_PARAM_STR(method)
        Z_PARAM_STR(args_json)
    ZEND_PARSE_PARAMETERS_END();

    if (!g_native_handler) {
        php_error_docref(NULL, E_WARNING, "phptoro: native handler not registered");
        RETURN_FALSE;
    }

    /* Dispatch to native */
    char *result = g_native_handler(
        ZSTR_VAL(ns),
        ZSTR_VAL(method),
        ZSTR_VAL(args_json)
    );

    if (!result) {
        RETURN_FALSE;
    }

    /* JSON-decode the result into return_value */
    php_json_decode(return_value, result, strlen(result), 1,
        PHP_JSON_PARSER_DEFAULT_DEPTH);
    free(result);
}

/* ── Function table ────────────────────────────────────────────────────── */

static const zend_function_entry phptoro_functions[] = {
    PHP_FE(phptoro_native_call, arginfo_phptoro_native_call)
    PHP_FE_END
};

/* ── Module entry ──────────────────────────────────────────────────────── */

zend_module_entry phptoro_module_entry = {
    STANDARD_MODULE_HEADER,
    "phptoro",              /* name */
    phptoro_functions,      /* functions */
    NULL,                   /* MINIT */
    NULL,                   /* MSHUTDOWN */
    NULL,                   /* RINIT */
    NULL,                   /* RSHUTDOWN */
    NULL,                   /* MINFO */
    "1.0",                  /* version */
    STANDARD_MODULE_PROPERTIES
};
