#ifndef PHPTORO_SAPI_H
#define PHPTORO_SAPI_H

#include <stdint.h>
#include <stddef.h>

/* Initialize the PHP engine. Call once at startup. Returns 0 on success.
 * data_dir: writable path for sessions, uploads, tmp files (app-private).
 * Plugins must be registered via phptoro_register_plugin() before calling this. */
int phptoro_php_init(const char *data_dir);

/* Shutdown the PHP engine. */
void phptoro_php_shutdown(void);

typedef struct {
    const char *method;       /* "GET", "POST", etc. */
    const char *uri;          /* "/path?query" */
    const char *script_path;  /* Absolute path to .php file */
    const char *document_root;/* App directory */
    const uint8_t *body;
    size_t body_len;
    const char *content_type;
    const char *cookie;

    /* Arbitrary HTTP headers (forwarded as HTTP_* in $_SERVER) */
    const char **header_names;
    const char **header_values;
    int header_count;
} phptoro_request;

typedef struct {
    int status;
    uint8_t *body;        /* Response body (from phptoro_respond or echo) */
    size_t body_len;
    uint8_t *debug;       /* Stray echo/print output (NULL if none) */
    size_t debug_len;
    char **header_names;
    char **header_values;
    int header_count;
} phptoro_response;

/* Execute a PHP script. Caller must free response with phptoro_response_free(). */
int phptoro_php_execute(const phptoro_request *req, phptoro_response *resp);

/* Free response data. */
void phptoro_response_free(phptoro_response *resp);

#endif
