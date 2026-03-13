/*
 * phptoro_phpinfo.c — Structured phpinfo() override.
 *
 * Replaces the built-in phpinfo() so it returns a PHP array
 * instead of printing HTML. The array can be rendered as native
 * UI elements on mobile.
 *
 * Usage from PHP:
 *   $info = phpinfo();  // returns array, no output
 *
 * Returned structure:
 *   [
 *     [
 *       'section' => 'PHP Core',
 *       'rows' => [
 *         ['key' => 'PHP Version', 'value' => '8.5.3'],
 *         ['key' => 'System', 'value' => 'Darwin ...'],
 *         ...
 *       ]
 *     ],
 *     ...
 *   ]
 *
 * Call phptoro_phpinfo_install() after PHP startup.
 */

#include "php.h"
#include "ext/standard/info.h"
#include "main/php_output.h"
#include <string.h>
#include <ctype.h>

/* ── HTML tag stripping helper ─────────────────────────────────────────── */

/*
 * Strip HTML tags from a string. Returns a malloc'd copy.
 * Decodes &amp; &lt; &gt; &quot; entities.
 */
static char *strip_tags(const char *html, size_t len) {
    char *out = malloc(len + 1);
    if (!out) return NULL;

    size_t j = 0;
    int in_tag = 0;

    for (size_t i = 0; i < len; i++) {
        if (html[i] == '<') {
            in_tag = 1;
            continue;
        }
        if (html[i] == '>') {
            in_tag = 0;
            continue;
        }
        if (!in_tag) {
            /* Decode common HTML entities */
            if (html[i] == '&' && i + 1 < len) {
                if (strncmp(html + i, "&amp;", 5) == 0) {
                    out[j++] = '&'; i += 4; continue;
                }
                if (strncmp(html + i, "&lt;", 4) == 0) {
                    out[j++] = '<'; i += 3; continue;
                }
                if (strncmp(html + i, "&gt;", 4) == 0) {
                    out[j++] = '>'; i += 3; continue;
                }
                if (strncmp(html + i, "&quot;", 6) == 0) {
                    out[j++] = '"'; i += 5; continue;
                }
                if (strncmp(html + i, "&#039;", 6) == 0) {
                    out[j++] = '\''; i += 5; continue;
                }
                if (strncmp(html + i, "&nbsp;", 6) == 0) {
                    out[j++] = ' '; i += 5; continue;
                }
            }
            out[j++] = html[i];
        }
    }
    out[j] = '\0';

    /* Trim trailing whitespace */
    while (j > 0 && (out[j-1] == ' ' || out[j-1] == '\n' || out[j-1] == '\r' || out[j-1] == '\t')) {
        out[--j] = '\0';
    }

    return out;
}

/* ── Trim whitespace helper ────────────────────────────────────────────── */

static char *trim(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) len--;
    char *out = malloc(len + 1);
    if (out) {
        memcpy(out, s, len);
        out[len] = '\0';
    }
    return out;
}

/* ── HTML table parser → PHP array ─────────────────────────────────────── */

/*
 * Parse the stock phpinfo() HTML output into a structured PHP array.
 *
 * phpinfo HTML structure:
 *   <h2><a ...>Section Name</a></h2>
 *   <table>
 *     <tr class="h"><th>...</th><th>...</th></tr>     (header row)
 *     <tr><td class="e">Key</td><td class="v">Value</td></tr>
 *     ...
 *   </table>
 */
static void parse_phpinfo_html(const char *html, size_t html_len, zval *result) {
    array_init(result);

    const char *pos = html;
    const char *end = html + html_len;
    zval *current_section = NULL;
    zval rows;
    char *section_name = NULL;

    while (pos < end) {
        /* Look for <h2> section headers */
        const char *h2 = strstr(pos, "<h2");
        const char *table_start = strstr(pos, "<table");
        const char *tr_start = strstr(pos, "<tr");

        /* Find the earliest interesting tag */
        const char *next = NULL;
        int tag_type = 0; /* 1=h2, 2=table, 3=tr */

        if (h2 && (!next || h2 < next)) { next = h2; tag_type = 1; }
        if (table_start && (!next || table_start < next)) { next = table_start; tag_type = 2; }
        if (tr_start && (!next || tr_start < next)) { next = tr_start; tag_type = 3; }

        if (!next) break;

        if (tag_type == 1) {
            /* Extract section name from <h2>...</h2> */
            const char *h2_end = strstr(h2, "</h2>");
            if (!h2_end) { pos = h2 + 3; continue; }

            size_t inner_len = h2_end - h2;
            char *raw = strip_tags(h2, inner_len);

            /* Start new section */
            if (section_name) free(section_name);
            section_name = raw;

            zval section;
            array_init(&section);
            add_assoc_string(&section, "section", section_name);
            array_init(&rows);
            add_assoc_zval(&section, "rows", &rows);
            current_section = zend_hash_next_index_insert(Z_ARRVAL_P(result), &section);

            pos = h2_end + 5;

            /* Capture plain text sections (e.g. PHP License) that have no <table>.
             * If the next structural tag is another <h2> (or end of doc), not a <table>,
             * then the content between is plain text belonging to this section. */
            {
                const char *next_h2 = strstr(pos, "<h2");
                const char *next_tbl = strstr(pos, "<table");
                const char *text_end = NULL;

                if (!next_tbl || (next_h2 && next_h2 < next_tbl)) {
                    text_end = next_h2 ? next_h2 : end;
                }

                if (text_end && text_end > pos) {
                    char *plain = strip_tags(pos, text_end - pos);
                    if (plain) {
                        char *trimmed = trim(plain);
                        free(plain);
                        if (trimmed && strlen(trimmed) > 10 && current_section) {
                            zval *rows_zv = zend_hash_str_find(
                                Z_ARRVAL_P(current_section), "rows", 4);
                            if (rows_zv && Z_TYPE_P(rows_zv) == IS_ARRAY) {
                                zval row;
                                array_init(&row);
                                add_assoc_string(&row, "key", "");
                                add_assoc_string(&row, "value", trimmed);
                                zend_hash_next_index_insert(Z_ARRVAL_P(rows_zv), &row);
                            }
                        }
                        if (trimmed) free(trimmed);
                    }
                }
            }
        } else if (tag_type == 3) {
            /* Parse <tr> row */
            const char *tr_end = strstr(tr_start, "</tr>");
            if (!tr_end) { pos = tr_start + 3; continue; }

            /* Skip header rows (<tr class="h">) */
            const char *class_attr = strstr(tr_start, "class=\"h\"");
            if (class_attr && class_attr < tr_end) {
                pos = tr_end + 5;
                continue;
            }

            /* Extract <td> cells */
            const char *scan = tr_start;
            char *cells[3] = {NULL, NULL, NULL};
            int cell_count = 0;

            while (scan < tr_end && cell_count < 3) {
                const char *td = strstr(scan, "<td");
                if (!td || td >= tr_end) break;

                const char *td_content = strchr(td, '>');
                if (!td_content || td_content >= tr_end) break;
                td_content++; /* skip '>' */

                const char *td_end = strstr(td_content, "</td>");
                if (!td_end || td_end > tr_end) break;

                cells[cell_count] = strip_tags(td_content, td_end - td_content);
                cell_count++;
                scan = td_end + 5;
            }

            /* Add row if we got key-value pair */
            if (cell_count >= 2 && cells[0] && cells[1]) {
                /* Ensure we have a section */
                if (!current_section) {
                    zval section;
                    array_init(&section);
                    add_assoc_string(&section, "section", "General");
                    array_init(&rows);
                    add_assoc_zval(&section, "rows", &rows);
                    current_section = zend_hash_next_index_insert(Z_ARRVAL_P(result), &section);
                }

                /* Get the rows array from current section */
                zval *rows_zv = zend_hash_str_find(Z_ARRVAL_P(current_section), "rows", 4);
                if (rows_zv && Z_TYPE_P(rows_zv) == IS_ARRAY) {
                    zval row;
                    array_init(&row);

                    char *key = trim(cells[0]);
                    char *val = trim(cells[1]);
                    add_assoc_string(&row, "key", key);
                    add_assoc_string(&row, "value", val);
                    free(key);
                    free(val);

                    /* Some rows have a third column (Local/Master values) */
                    if (cell_count >= 3 && cells[2]) {
                        char *local = trim(cells[2]);
                        add_assoc_string(&row, "local", local);
                        free(local);
                    }

                    zend_hash_next_index_insert(Z_ARRVAL_P(rows_zv), &row);
                }
            }

            for (int i = 0; i < cell_count; i++) {
                if (cells[i]) free(cells[i]);
            }

            pos = tr_end + 5;
        } else {
            /* Skip <table> tags, continue */
            pos = next + 1;
        }
    }

    if (section_name) free(section_name);
}

/* ── Branded phpinfo handler ─────────────────────────────────────────── */

/*
 * Override phpinfo() to return a structured PHP array instead of HTML.
 * Captures stock HTML output, parses it, returns array.
 */
PHP_FUNCTION(phptoro_phpinfo)
{
    zend_long flag = PHP_INFO_ALL;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(flag)
    ZEND_PARSE_PARAMETERS_END();

    /* Capture stock phpinfo HTML into a buffer */
    php_output_start_default();
    php_print_info((int)flag);

    zval buf;
    php_output_get_contents(&buf);
    php_output_discard();

    if (Z_TYPE(buf) != IS_STRING || Z_STRLEN(buf) == 0) {
        zval_ptr_dtor(&buf);
        array_init(return_value);
        return;
    }

    /* Parse HTML into structured array */
    parse_phpinfo_html(Z_STRVAL(buf), Z_STRLEN(buf), return_value);
    zval_ptr_dtor(&buf);
}

/* ── Install ─────────────────────────────────────────────────────────── */

void phptoro_phpinfo_install(void) {
    zend_function *fn = zend_hash_str_find_ptr(
        CG(function_table), "phpinfo", sizeof("phpinfo") - 1
    );
    if (fn && fn->type == ZEND_INTERNAL_FUNCTION) {
        fn->internal_function.handler = ZEND_FN(phptoro_phpinfo);
    }
}
