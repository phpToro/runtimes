#ifndef PHPTORO_PLUGIN_H
#define PHPTORO_PLUGIN_H

/*
 * phpToro Plugin Interface
 *
 * Each plugin provides a namespace (e.g. "notification") and a handler
 * function that receives the sub-command and JSON arguments.
 *
 * Commands are formatted as "namespace.command" — the registry splits
 * on the first dot and routes to the correct plugin.
 */

#define PHPTORO_MAX_PLUGINS 32

typedef struct {
    const char *ns;  /* namespace, e.g. "notification", "clipboard" */
    char *(*handle)(const char *command, const char *json);
} phptoro_plugin;

/* Register a plugin. Call before phptoro_dispatch(). */
void phptoro_register_plugin(phptoro_plugin plugin);

/* Dispatch "namespace.command" to the right plugin. Returns malloc'd JSON string. */
char *phptoro_dispatch(const char *full_command, const char *json);

#endif
