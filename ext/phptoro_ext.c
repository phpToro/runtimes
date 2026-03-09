/*
 * phptoro_ext.c — PHP extension for phpToro.
 *
 * Exposes two functions to PHP:
 *
 *   phptoro_native_call(string $namespace, string $method, string $args_json): mixed
 *   phptoro_respond(mixed $data): bool
 *
 * phptoro_native_call() is the ONLY bridge between PHP and native code.
 * The PHP framework wraps this in a developer-friendly API
 * (e.g. phptoro('state')->get('key')), but underneath it all goes
 * through this single C function.
 *
 * phptoro_respond() sets the structured JSON response, bypassing echo/print.
 *
 * The host (Swift/Kotlin) registers a native handler via
 * phptoro_set_native_handler() before PHP init. All routing, plugin
 * resolution, and dispatch happens on the native side.
 */

#include "php.h"
#include "ext/json/php_json.h"
#include "zend_smart_str.h"

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

/* ── phptoro_respond() ─────────────────────────────────────────────────── */

/*
 * Set the structured response directly, bypassing echo/print.
 *
 * The SAPI stores this JSON-encoded payload separately from output.
 * When both exist, the structured response takes priority and any
 * echo/print output is forwarded as debug info.
 *
 * Usage in entry.php:
 *   phptoro_respond($responseArray);
 */

/* Forward declaration — defined in phptoro_sapi.c */
void phptoro_set_response(const uint8_t *data, size_t len);

ZEND_BEGIN_ARG_INFO_EX(arginfo_phptoro_respond, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

PHP_FUNCTION(phptoro_respond)
{
    zval *data;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(data)
    ZEND_PARSE_PARAMETERS_END();

    /* JSON-encode the data */
    smart_str buf = {0};
    php_json_encode(&buf, data, PHP_JSON_UNESCAPED_UNICODE | PHP_JSON_INVALID_UTF8_SUBSTITUTE);
    smart_str_0(&buf);

    if (buf.s) {
        phptoro_set_response((const uint8_t *)ZSTR_VAL(buf.s), ZSTR_LEN(buf.s));
        smart_str_free(&buf);
        RETURN_TRUE;
    }

    RETURN_FALSE;
}

/* ── Function table ────────────────────────────────────────────────────── */

static const zend_function_entry phptoro_functions[] = {
    PHP_FE(phptoro_native_call, arginfo_phptoro_native_call)
    PHP_FE(phptoro_respond, arginfo_phptoro_respond)
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
