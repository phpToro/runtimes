#ifndef PHPTORO_UI_H
#define PHPTORO_UI_H

#include "phptoro_plugin.h"

/*
 * phpToro UI Plugin
 *
 * Collects directives (alert, navigate, flash, etc.) during PHP execution.
 * Swift reads them after phptoro_php_execute() returns.
 *
 * PHP usage:
 *   phptoro('ui')->alert('Title', 'Message');
 *   phptoro('ui')->navigate('HomeScreen', ['id' => 1]);
 *   phptoro('ui')->flash('Saved!', 'success');
 */

/* Register the "ui" plugin. Call before phptoro_php_init(). */
void phptoro_ui_register(void);

/* Read collected directives as a JSON array string. Caller must free(). */
char *phptoro_ui_flush(void);

#endif
