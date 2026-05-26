#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>

#define SOURCE_CONF_FILE "rssproxy.conf"

// Response buffer structure for curl
typedef struct {
  char *buf;
  size_t len;
  size_t cap;
  const char *prev_etag; // ETag from previous request (pointer into src->etag)
  char etag[96];         // ETag received from upstream (sized to pad response_t to 128 bytes)
} response_t;
_Static_assert(sizeof(response_t) == 128, "response_t size must be 128 bytes to fit in cache line");

#define ETAG_MAX sizeof(((response_t *)0)->etag)

// Single RSS source entry
typedef struct {
  char name[32];       // HTTP path, e.g. "/feed1"
  char etag[ETAG_MAX]; // Etag from upstream
  char uri[128];       // upstream URL (sized to pad rss_source_t to 256 bytes)
} rss_source_t;
_Static_assert(sizeof(rss_source_t) == 256, "rss_source_t size must be multiple of 64 bytes");

#define SOURCE_URI_LEN (sizeof(((rss_source_t *)0)->uri) - 1)

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
