#include "source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

// CONF_DIR is injected at compile time via -DCONF_DIR="..."
// Fall back to current directory if not set.
#ifndef CONF_DIR
#define CONF_DIR "."
#endif

#define SOURCES_MAX  512

static rss_sources_t s_sources = { NULL, 0 };

// Resolve the path to rssproxy.conf:
//   1. <dir of executable>/rssproxy.conf
//   2. CONF_DIR/rssproxy.conf  (only when CONF_DIR != ".")
// Returns 1 and fills 'out' (size PATH_MAX) on success, 0 otherwise.
static int resolve_conf_path(char *out, size_t outsz) {
  // --- attempt 1: directory of the running executable ---
  char exe[4096];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n > 0) {
    exe[n] = '\0';
    char *last_slash = strrchr(exe, '/');
    if (last_slash != NULL) {
      *last_slash = '\0';
      int r = snprintf(out, outsz, "%s/%s", exe, SOURCE_CONF_FILE);
      if (r > 0 && (size_t)r < outsz && access(out, R_OK) == 0) {
        return 1;
      }
    }
  }

  // --- attempt 2: compile-time CONF_DIR (skip when it is ".") ---
  if (strcmp(CONF_DIR, ".") != 0) {
    int r = snprintf(out, outsz, "%s/%s", CONF_DIR, SOURCE_CONF_FILE);
    if (r > 0 && (size_t)r < outsz) {
      if (access(out, R_OK) == 0)
        return 1;
    }
  }

  return 0;
}

// Load sources from SOURCE_FILE.
// File format — one entry per line:
//   <name> <uri>
// Lines starting with '#' and blank lines are ignored.
// Returns the number of entries loaded, or 0 on failure.
int sources_load(void) {
  sources_free();

  char conf_path[4096];
  if (!resolve_conf_path(conf_path, sizeof(conf_path))) {
    fprintf(stderr, "sources: %s not found (tried executable dir"
                    "%s)\n",
            SOURCE_CONF_FILE,
            strcmp(CONF_DIR, ".") != 0 ? ", " CONF_DIR : "");
    return 0;
  }

  FILE *f = fopen(conf_path, "r");
  if (!f) {
    fprintf(stderr, "sources: cannot open %s\n", conf_path);
    return 0;
  }

  rss_source_t *entries = NULL;
  size_t count = 0;
  char line[768];

  while (fgets(line, sizeof(line), f) && count < SOURCES_MAX) {
    // Strip trailing whitespace / newline
    char *p = line + strlen(line) - 1;
    while (p >= line && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t'))
      *p-- = '\0';

    // Strip leading whitespace
    char *start = line;
    while (*start == ' ' || *start == '\t')
      start++;

    // Skip blank lines and comments
    if (*start == '\0' || *start == '#')
      continue;

    // Split on first whitespace to get name and uri
    char *sep = start;
    while (*sep && *sep != ' ' && *sep != '\t')
      sep++;

    if (*sep == '\0') {
      fprintf(stderr, "sources: skipping malformed line: %s\n", start);
      continue;
    }

    *sep = '\0';
    char *uri = sep + 1;
    while (*uri == ' ' || *uri == '\t')
      uri++;

    if (*uri == '\0') {
      fprintf(stderr, "sources: skipping entry with empty URI: %s\n", start);
      continue;
    }

    if (strlen(uri) > SOURCE_URI_MAX) {
      fprintf(stderr, "sources: skipping entry '%s': URI too long (max %zu chars)\n",
              start, SOURCE_URI_MAX);
      continue;
    }

    rss_source_t *tmp = realloc(entries, (count + 1) * sizeof(rss_source_t));
    if (!tmp) {
      fprintf(stderr, "sources: out of memory\n");
      break;
    }
    entries = tmp;

    rss_source_t *e = &entries[count];
    memset(e, 0, sizeof(*e));

    int too_long = 0;
    size_t nlen = (size_t)(sep - start);
    if (nlen >= sizeof(e->name)) {
      nlen = sizeof(e->name) - 1;
      too_long = 1;
    }
    memcpy(e->name, start, nlen);
    if (too_long) {
      fprintf(stderr, "sources: name too long, truncated to: %s\n", e->name);
    }

    // we already checked that uri length is within SOURCE_URI_MAX, which is sizeof(e->uri) - 1, so no need to check again here
    size_t ulen = strlen(uri);
    memcpy(e->uri, uri, ulen);

    count++;
  }

  fclose(f);

  if (count == 0) {
    free(entries);
    fprintf(stderr, "sources: no valid entries found in %s\n", conf_path);
    return 0;
  }

  s_sources.entries = entries;
  s_sources.count   = count;

#ifdef DEBUG
  fprintf(stdout, "sources: loaded %zu entries from %s\n", count, conf_path);
  for (size_t i = 0; i < count; i++)
    fprintf(stdout, "  [%zu] %s -> %s\n", i, entries[i].name, entries[i].uri);
  fflush(stdout);
#endif

  return (int)count;
}

rss_sources_t *sources_get(void) {
  return &s_sources;
}

void sources_free(void) {
  free(s_sources.entries);
  s_sources.entries = NULL;
  s_sources.count   = 0;
}
