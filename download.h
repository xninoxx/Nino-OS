/* download.h - saves an HTTP response as a file in /downloads */
#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stdbool.h>
#include "http.h"

/* Name: name_hint if given, else the server's Content-Disposition name, else the last part of the URL,
 * else index.html. Unsafe characters become '_'. The chosen name is returned in saved. */
bool download_save(const http_result_t *res, const char *name_hint, char *saved, int size);

#endif /* DOWNLOAD_H */
