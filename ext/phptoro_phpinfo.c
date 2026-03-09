/*
 * phptoro_phpinfo.c — Custom branded phpinfo() override.
 *
 * Replaces the built-in phpinfo() handler after PHP startup.
 * Captures the stock output and injects phpToro CSS theming.
 *
 * Call phptoro_phpinfo_install() after php_module_startup().
 */

#include "php.h"
#include "ext/standard/info.h"
#include "main/php_output.h"
#include <string.h>

/* ── phpToro CSS (overrides stock phpinfo styles) ────────────────────────── */

static const char PHPTORO_CSS[] =
    "</style>\n<style>\n"
    ":root { --toro: #a20009; --toro-light: #f5d0d2; --toro-dark: #6b0006; }\n"
    "body { background-color: #fff; color: #222; font-family: sans-serif; }\n"
    "pre { margin: 0; font-family: monospace; }\n"
    "a { color: var(--toro); }\n"
    "a:hover { text-decoration: none; }\n"
    "table { border-collapse: collapse; border: 0; width: 934px; box-shadow: 1px 2px 3px rgba(0,0,0,.2); }\n"
    ".center { text-align: center; }\n"
    ".center table { margin: 1em auto; text-align: left; }\n"
    ".center th { text-align: center !important; }\n"
    "td, th { border: 1px solid #999; font-size: 75%; vertical-align: baseline; padding: 4px 5px; }\n"
    "th { position: sticky; top: 0; background: inherit; }\n"
    "h1 { font-size: 150%; color: var(--toro); }\n"
    "h2 { font-size: 125%; color: var(--toro); }\n"
    "h2 > a { text-decoration: none; }\n"
    "h2 > a:hover { text-decoration: underline; }\n"
    ".p { text-align: left; }\n"
    ".e { background-color: var(--toro-light); width: 300px; font-weight: bold; }\n"
    ".h { background-color: var(--toro); color: #fff; font-weight: bold; }\n"
    ".v { background-color: #f0f0f0; max-width: 300px; overflow-x: auto; word-wrap: break-word; }\n"
    ".v i { color: #999; }\n"
    "img { float: right; border: 0; }\n"
    "hr { width: 934px; background-color: #ddd; border: 0; height: 1px; }\n"
    "@media (prefers-color-scheme: dark) {\n"
    "  body { background: #1a1a1a; color: #e0e0e0; }\n"
    "  .h td, td.e, th { border-color: #555; }\n"
    "  td { border-color: #444; }\n"
    "  .e { background-color: #3d1012; color: var(--toro-light); }\n"
    "  .h { background-color: var(--toro-dark); color: #fff; }\n"
    "  .v { background-color: #1a1a1a; }\n"
    "  hr { background-color: #444; }\n"
    "  h1, h2, a { color: #e05060; }\n"
    "}\n";

/* ── Branded phpinfo handler ─────────────────────────────────────────────── */

PHP_FUNCTION(phptoro_phpinfo)
{
    zend_long flag = PHP_INFO_ALL;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(flag)
    ZEND_PARSE_PARAMETERS_END();

    php_output_start_default();
    php_print_info((int)flag);

    zval buf;
    php_output_get_contents(&buf);
    php_output_discard();

    if (Z_TYPE(buf) != IS_STRING || Z_STRLEN(buf) == 0) {
        zval_ptr_dtor(&buf);
        RETURN_TRUE;
    }

    char *html = Z_STRVAL(buf);
    size_t html_len = Z_STRLEN(buf);

    char *style_end = strstr(html, "</style>");
    if (style_end) {
        size_t before_len = style_end - html;
        size_t after_len = html_len - before_len;

        PHPWRITE(html, before_len);
        PHPWRITE(PHPTORO_CSS, sizeof(PHPTORO_CSS) - 1);
        PHPWRITE(style_end, after_len);
    } else {
        PHPWRITE(html, html_len);
    }

    zval_ptr_dtor(&buf);
    RETURN_TRUE;
}

/* ── Install ─────────────────────────────────────────────────────────────── */

void phptoro_phpinfo_install(void) {
    zend_function *fn = zend_hash_str_find_ptr(
        CG(function_table), "phpinfo", sizeof("phpinfo") - 1
    );
    if (fn && fn->type == ZEND_INTERNAL_FUNCTION) {
        fn->internal_function.handler = ZEND_FN(phptoro_phpinfo);
    }
}
