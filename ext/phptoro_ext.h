#ifndef PHPTORO_EXT_H
#define PHPTORO_EXT_H

#include "php.h"

/*
 * Native handler callback type.
 *
 * Called by PHP's phptoro_native_call() to dispatch to Swift/Kotlin.
 * Returns a malloc'd JSON string (caller frees), or NULL on error.
 *
 * Parameters:
 *   namespace  — e.g. "state", "ui", "camera"
 *   method     — e.g. "get", "set", "alert", "navigate"
 *   args_json  — JSON-encoded arguments string
 */
typedef char *(*phptoro_native_handler_t)(const char *ns, const char *method, const char *args_json);

/*
 * phptoro_set_native_handler() — call BEFORE phptoro_php_init().
 *
 * Registers the single native dispatch function that handles all
 * phptoro_native_call() invocations from PHP.
 */
void phptoro_set_native_handler(phptoro_native_handler_t handler);

/* Module entry — pass to php_module_startup() to register the
 * phptoro_native_call() function during MINIT. */
extern zend_module_entry phptoro_module_entry;

#endif /* PHPTORO_EXT_H */
