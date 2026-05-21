#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>

#define SOURCE_CONF_FILE "rssproxy.conf"
#define SOURCE_ETAG_MAX  64

// Single RSS source entry
typedef struct {
  char name[32];              // HTTP path, e.g. "/feed1"
  char etag[SOURCE_ETAG_MAX];
  char uri[160];              // upstream URL (sized to pad rss_source_t to 256 bytes)
} rss_source_t;
_Static_assert(sizeof(rss_source_t) == 256, "rss_source_t size must be multiple of 64 bytes");

#define SOURCE_URI_MAX (sizeof(((rss_source_t *)0)->uri) - 1)

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
