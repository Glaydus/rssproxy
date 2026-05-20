#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>

#define SOURCE_CONF_FILE "rssproxy.conf"
#define SOURCE_ETAG_MAX  64

// Single RSS source entry
typedef struct {
  char name[64];             // HTTP path, e.g. "/tvn24"
  char uri[384];             // upstream URL
  char etag[SOURCE_ETAG_MAX];
} rss_source_t;

// Container — dynamically allocated entries array + count in one place.
typedef struct {
  rss_source_t *entries;
  size_t        count;
} rss_sources_t;

// Load sources from <CONF_DIR>/rssproxy.conf (CONF_DIR set at compile time).
// Returns the number of entries loaded, or 0 if the file is missing / empty.
int sources_load(void);

// Return a pointer to the internal rss_sources_t (never NULL after sources_load()).
rss_sources_t *sources_get(void);

// Free resources allocated by sources_load().
void sources_free(void);

#endif /* SOURCE_H */
