#ifndef PHPTORO_PHPINFO_H
#define PHPTORO_PHPINFO_H

#include "php.h"

/*
 * phptoro_phpinfo_install() — call AFTER RiphtSapi::instance().
 *
 * Replaces the built-in phpinfo() handler with a branded version
 * that uses phpToro CSS theming.
 */
void phptoro_phpinfo_install(void);

#endif /* PHPTORO_PHPINFO_H */
