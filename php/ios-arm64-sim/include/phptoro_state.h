#ifndef PHPTORO_STATE_H
#define PHPTORO_STATE_H

#include "phptoro_plugin.h"

/*
 * phpToro State Plugin
 *
 * Per-screen key-value state stored in C memory.
 * No JSON serialization on every request — PHP reads/writes directly.
 *
 * PHP usage:
 *   phptoro('state')->get('email')        → returns current value
 *   phptoro('state')->set('email', 'x')   → stores value
 *
 * Swift usage:
 *   phptoro_state_set("LoginScreen", "email", "\"user@test.com\"")
 *   phptoro_state_get("LoginScreen")  → JSON object of all state
 *   phptoro_state_clear("LoginScreen")
 */

/* Register the "state" plugin. Call before phptoro_php_init(). */
void phptoro_state_register(void);

/* Set the active screen (call before phptoro_php_execute). */
void phptoro_state_set_screen(const char *screen);

/* Set a single state value (JSON-encoded). Called by Swift for binds. */
void phptoro_state_set(const char *screen, const char *key, const char *json_value);

/* Get all state for a screen as a JSON object string. Caller must free(). */
char *phptoro_state_get(const char *screen);

/* Clear all state for a screen. */
void phptoro_state_clear(const char *screen);

#endif
