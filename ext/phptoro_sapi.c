/*
 * phpToro SAPI — Embeds PHP via the C embed API.
 */

#include "php.h"
#include "SAPI.h"
#include "php_main.h"
#include "php_variables.h"
#include "zend_stream.h"

#include "phptoro_ext.h"
#include "phptoro_phpinfo.h"
#include "phptoro_sapi.h"

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>

/* ── Request context (stored in sapi_globals.server_context) ──────── */

typedef struct {
    /* Request */
    const char *method;
    char       *query_string;   /* allocated copy — PHP may modify */
    char       *cookie_data;    /* allocated copy */
    const char *content_type;
    const uint8_t *post_data;
    size_t      post_data_len;
    size_t      post_read;

    /* Incoming HTTP headers (for HTTP_* forwarding) */
    const char **req_header_names;
    const char **req_header_values;
    int         req_header_count;

    /* Response output */
    uint8_t *output;
    size_t   output_len;
    size_t   output_cap;
    int      status_code;

    /* Response headers */
    char  **header_names;
    char  **header_values;
    int     header_count;
    int     header_cap;

    /* $_SERVER data */
    const char *request_uri;
    char       *script_name;    /* path-only (no query string) */
    const char *script_filename;
    const char *document_root;

    /* Request timing */
    double request_time;
} request_ctx;

/* ── Helpers ─────────────────────────────────────────────────────── */

static void ctx_append_output(request_ctx *ctx, const uint8_t *data, size_t len) {
    if (ctx->output_len + len > ctx->output_cap) {
        size_t cap = ctx->output_cap ? ctx->output_cap : 4096;
        while (cap < ctx->output_len + len) cap *= 2;
        ctx->output = realloc(ctx->output, cap);
        ctx->output_cap = cap;
    }
    memcpy(ctx->output + ctx->output_len, data, len);
    ctx->output_len += len;
}

static void ctx_add_header(request_ctx *ctx, const char *name, const char *value) {
    if (ctx->header_count >= ctx->header_cap) {
        ctx->header_cap = ctx->header_cap ? ctx->header_cap * 2 : 16;
        ctx->header_names  = realloc(ctx->header_names,  sizeof(char *) * ctx->header_cap);
        ctx->header_values = realloc(ctx->header_values, sizeof(char *) * ctx->header_cap);
    }
    ctx->header_names[ctx->header_count]  = strdup(name);
    ctx->header_values[ctx->header_count] = strdup(value);
    ctx->header_count++;
}

/* ── SAPI callbacks ──────────────────────────────────────────────── */

static int cb_startup(sapi_module_struct *m)    { return SUCCESS; }
static int cb_shutdown(sapi_module_struct *m)   { return SUCCESS; }
static int cb_activate(void)                    { return SUCCESS; }
static int cb_deactivate(void)                  { return SUCCESS; }

static size_t cb_ub_write(const char *str, size_t len) {
    if (!str || len == 0) return 0;

    if (!SG(headers_sent)) sapi_send_headers();

    request_ctx *ctx = SG(server_context);
    if (!ctx) return 0;

    ctx_append_output(ctx, (const uint8_t *)str, len);
    return len;
}

static void cb_flush(void *server_context) {
    if (!SG(headers_sent)) sapi_send_headers();
}

static int cb_send_headers(sapi_headers_struct *hdrs) {
    request_ctx *ctx = SG(server_context);
    if (!ctx) return SAPI_HEADER_SEND_FAILED;

    int code = hdrs->http_response_code;
    ctx->status_code = (code >= 100 && code <= 599) ? code : 500;

    /* Clear previous headers */
    for (int i = 0; i < ctx->header_count; i++) {
        free(ctx->header_names[i]);
        free(ctx->header_values[i]);
    }
    ctx->header_count = 0;

    /* Walk PHP's header linked list */
    zend_llist_element *elem = hdrs->headers.head;
    while (elem) {
        sapi_header_struct *h = (sapi_header_struct *)elem->data;
        if (h->header && h->header_len > 0) {
            char *colon = memchr(h->header, ':', h->header_len);
            if (colon && colon != h->header) {
                char *name = strndup(h->header, colon - h->header);
                const char *val = colon + 1;
                while (*val == ' ') val++;
                size_t vlen = h->header_len - (val - h->header);
                while (vlen > 0 && (val[vlen-1] == '\r' || val[vlen-1] == '\n')) vlen--;
                char *value = strndup(val, vlen);
                ctx_add_header(ctx, name, value);
                free(name);
                free(value);
            }
        }
        elem = elem->next;
    }

    return SAPI_HEADER_SENT_SUCCESSFULLY;
}

static void cb_send_header(sapi_header_struct *h, void *ctx) {
    /* Handled in cb_send_headers */
}

static size_t cb_read_post(char *buf, size_t count) {
    request_ctx *ctx = SG(server_context);
    if (!ctx || !ctx->post_data) return 0;

    size_t remaining = ctx->post_data_len - ctx->post_read;
    size_t n = count < remaining ? count : remaining;
    if (n > 0) {
        memcpy(buf, ctx->post_data + ctx->post_read, n);
        ctx->post_read += n;
    }
    return n;
}

static char *cb_read_cookies(void) {
    request_ctx *ctx = SG(server_context);
    return ctx ? ctx->cookie_data : NULL;
}

static void cb_register_server_variables(zval *arr) {
    request_ctx *ctx = SG(server_context);
    if (!ctx) return;

    #define REG(name, val) do { \
        if (val) php_register_variable_safe((name), (val), strlen(val), arr); \
    } while(0)

    /* Standard CGI/1.1 variables */
    REG("REQUEST_METHOD",    ctx->method);
    REG("REQUEST_URI",       ctx->request_uri);
    REG("QUERY_STRING",      ctx->query_string);
    REG("SCRIPT_FILENAME",   ctx->script_filename);
    REG("SCRIPT_NAME",       ctx->script_name);
    REG("PHP_SELF",          ctx->script_name);
    REG("DOCUMENT_ROOT",     ctx->document_root);
    REG("CONTENT_TYPE",      ctx->content_type);
    REG("HTTP_COOKIE",       ctx->cookie_data);
    REG("SERVER_SOFTWARE",   "phpToro/1.0");
    REG("SERVER_PROTOCOL",   "HTTP/1.1");
    REG("GATEWAY_INTERFACE", "CGI/1.1");
    REG("SERVER_NAME",       "localhost");
    REG("SERVER_PORT",       "0");
    REG("SERVER_ADDR",       "127.0.0.1");
    REG("REMOTE_ADDR",       "127.0.0.1");
    REG("REMOTE_PORT",       "0");
    REG("REQUEST_SCHEME",    "phptoro");

    /* Content-Length */
    char cl[32];
    snprintf(cl, sizeof(cl), "%zu", ctx->post_data_len);
    php_register_variable_safe("CONTENT_LENGTH", cl, strlen(cl), arr);

    /* REQUEST_TIME and REQUEST_TIME_FLOAT */
    char rt[32];
    snprintf(rt, sizeof(rt), "%ld", (long)ctx->request_time);
    php_register_variable_safe("REQUEST_TIME", rt, strlen(rt), arr);

    char rtf[64];
    snprintf(rtf, sizeof(rtf), "%.6f", ctx->request_time);
    php_register_variable_safe("REQUEST_TIME_FLOAT", rtf, strlen(rtf), arr);

    /* Forward incoming HTTP headers as HTTP_* variables */
    for (int i = 0; i < ctx->req_header_count; i++) {
        const char *name = ctx->req_header_names[i];
        const char *value = ctx->req_header_values[i];
        if (!name || !value) continue;

        if (strcasecmp(name, "Content-Type") == 0) continue;
        if (strcasecmp(name, "Content-Length") == 0) continue;
        if (strcasecmp(name, "Cookie") == 0) continue;

        size_t nlen = strlen(name);
        char *key = malloc(5 + nlen + 1);
        memcpy(key, "HTTP_", 5);
        for (size_t j = 0; j < nlen; j++) {
            char c = name[j];
            if (c == '-') c = '_';
            else if (c >= 'a' && c <= 'z') c -= 32;
            key[5 + j] = c;
        }
        key[5 + nlen] = '\0';

        php_register_variable_safe(key, value, strlen(value), arr);
        free(key);
    }

    #undef REG
}

static void cb_log_message(const char *msg, int syslog_type) {
    if (msg) fprintf(stderr, "[PHP] %s\n", msg);
}

static int cb_get_request_time(double *t) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *t = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
    return SUCCESS;
}

static char *cb_getenv(const char *name, size_t len) {
    return NULL;
}

static void cb_default_post_reader(void) {}

static void cb_treat_data(int arg, char *str, zval *dest) {
    php_default_treat_data(arg, str, dest);
}

/* ── INI defaults ────────────────────────────────────────────────── */

static const char ini_base[] =
    "variables_order=EGPCS\n"
    "request_order=GP\n"
    "output_buffering=4096\n"
    "implicit_flush=0\n"
    "html_errors=0\n"
    "display_errors=1\n"
    "log_errors=1\n"
    "opcache.enable=0\n"
    "opcache.enable_cli=0\n";

static char *ini_entries_dynamic = NULL;

/* ── SAPI name storage ──────────────────────────────────────────── */

static char sapi_name[]        = "phptoro";
static char sapi_pretty_name[] = "phpToro Embedded";

/* ── Public API ──────────────────────────────────────────────────── */

static int initialized = 0;

int phptoro_php_init(const char *data_dir) {
    if (initialized) return 0;

    const char *dir = data_dir ? data_dir : "/tmp";

    char sessions_dir[1024];
    char tmp_dir[1024];
    snprintf(sessions_dir, sizeof(sessions_dir), "%s/sessions", dir);
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/tmp", dir);

    mkdir(sessions_dir, 0700);
    mkdir(tmp_dir, 0700);

    size_t ini_size = strlen(ini_base) + 2048;
    ini_entries_dynamic = malloc(ini_size);
    snprintf(ini_entries_dynamic, ini_size,
        "%s"
        "session.save_path=%s\n"
        "upload_tmp_dir=%s\n"
        "sys_temp_dir=%s\n",
        ini_base, sessions_dir, tmp_dir, tmp_dir);

    sapi_module.name        = sapi_name;
    sapi_module.pretty_name = sapi_pretty_name;

    sapi_module.startup     = cb_startup;
    sapi_module.shutdown    = cb_shutdown;
    sapi_module.activate    = cb_activate;
    sapi_module.deactivate  = cb_deactivate;

    sapi_module.ub_write    = cb_ub_write;
    sapi_module.flush       = cb_flush;
    sapi_module.send_headers = cb_send_headers;
    sapi_module.send_header  = cb_send_header;

    sapi_module.read_post   = cb_read_post;
    sapi_module.read_cookies = cb_read_cookies;
    sapi_module.register_server_variables = cb_register_server_variables;

    sapi_module.log_message      = cb_log_message;
    sapi_module.get_request_time = cb_get_request_time;
    sapi_module.getenv           = cb_getenv;

    sapi_module.default_post_reader = cb_default_post_reader;
    sapi_module.treat_data          = cb_treat_data;
    sapi_module.input_filter        = php_default_input_filter;

    sapi_module.php_ini_ignore     = 0;
    sapi_module.php_ini_ignore_cwd = 1;
    sapi_module.ini_entries        = ini_entries_dynamic;

    sapi_startup(&sapi_module);

    if (php_module_startup(&sapi_module, &phptoro_module_entry) == FAILURE) {
        sapi_shutdown();
        return -1;
    }

    /* Install branded phpinfo() override */
    phptoro_phpinfo_install();

    initialized = 1;
    fprintf(stderr, "[phpToro] PHP %s engine initialized\n", PHP_VERSION);
    return 0;
}

void phptoro_php_shutdown(void) {
    if (!initialized) return;
    php_module_shutdown();
    sapi_shutdown();
    free(ini_entries_dynamic);
    ini_entries_dynamic = NULL;
    initialized = 0;
}

int phptoro_php_execute(const phptoro_request *req, phptoro_response *resp) {
    if (!initialized || !req || !resp) return -1;
    memset(resp, 0, sizeof(*resp));

    request_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.method      = req->method ? req->method : "GET";
    ctx.status_code = 200;

    const char *uri = req->uri ? req->uri : "/";
    const char *qmark = strchr(uri, '?');
    ctx.query_string  = qmark ? strdup(qmark + 1) : strdup("");
    ctx.request_uri   = uri;

    if (qmark) {
        ctx.script_name = strndup(uri, qmark - uri);
    } else {
        ctx.script_name = strdup(uri);
    }

    ctx.cookie_data   = req->cookie ? strdup(req->cookie) : NULL;
    ctx.content_type  = req->content_type;
    ctx.post_data     = req->body;
    ctx.post_data_len = req->body_len;
    ctx.script_filename = req->script_path;
    ctx.document_root   = req->document_root;

    ctx.req_header_names  = req->header_names;
    ctx.req_header_values = req->header_values;
    ctx.req_header_count  = req->header_count;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    ctx.request_time = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;

    SG(server_context)               = &ctx;
    SG(request_info).request_method  = ctx.method;
    SG(request_info).content_type    = ctx.content_type;
    SG(request_info).content_length  = (zend_long)ctx.post_data_len;
    SG(request_info).query_string    = ctx.query_string;
    SG(sapi_headers).http_response_code = 200;

    if (php_request_startup() == FAILURE) {
        php_request_shutdown(NULL);
        SG(server_context) = NULL;
        free(ctx.query_string);
        free(ctx.cookie_data);
        free(ctx.script_name);
        return -1;
    }

    zend_file_handle fh;
    zend_stream_init_filename(&fh, req->script_path);
    fh.primary_script = 1;
    php_execute_script(&fh);
    zend_destroy_file_handle(&fh);

    SG(post_read) = 1;
    php_request_shutdown(NULL);

    resp->status        = ctx.status_code;
    resp->body          = ctx.output;
    resp->body_len      = ctx.output_len;
    resp->header_names  = ctx.header_names;
    resp->header_values = ctx.header_values;
    resp->header_count  = ctx.header_count;

    SG(server_context)              = NULL;
    SG(request_info).content_type   = NULL;
    SG(request_info).query_string   = NULL;
    SG(request_info).cookie_data    = NULL;

    free(ctx.query_string);
    free(ctx.cookie_data);
    free(ctx.script_name);
    return 0;
}

void phptoro_response_free(phptoro_response *resp) {
    if (!resp) return;
    free(resp->body);
    for (int i = 0; i < resp->header_count; i++) {
        free(resp->header_names[i]);
        free(resp->header_values[i]);
    }
    free(resp->header_names);
    free(resp->header_values);
    memset(resp, 0, sizeof(*resp));
}
