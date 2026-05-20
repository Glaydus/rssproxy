#include "blacklist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

// Helper function called by __attribute__((cleanup)) — equivalent of 'defer unlock'
static void _mutex_unlock_ptr(pthread_mutex_t **m) { pthread_mutex_unlock(*m); }

// Locks the mutex and automatically unlocks it when leaving the scope (like 'defer' in Go)
#define DEFER_MUTEX(m) \
  pthread_mutex_t *_defer_mutex_##m \
  __attribute__((cleanup(_mutex_unlock_ptr))) = &(m); \
  pthread_mutex_lock(&(m))

#ifndef CONF_DIR
#define CONF_DIR "."
#endif

#define BLACKLIST_FILENAME "blacklist.ip"
#define BLACKLIST_MAX      256

typedef struct {
  uint32_t addr;  // network address in network byte order
  uint32_t mask;  // subnet mask in network byte order
} blacklist_entry_t;

static blacklist_entry_t s_blacklist[BLACKLIST_MAX];
static int               s_blacklist_count = 0;
static pthread_mutex_t   s_mutex = PTHREAD_MUTEX_INITIALIZER;
static char              s_blacklist_path[4096];  // resolved at load time

// Resolve the path to blacklist.ip using the same strategy as rssproxy.conf:
//   1. <dir of executable>/blacklist.ip
//   2. CONF_DIR/blacklist.ip  (only when CONF_DIR != ".")
// Falls back to "./blacklist.ip" when neither location is found (file may not
// exist yet — it will be created on the first auto-blacklist event).
static void resolve_blacklist_path(void) {
  char exe[4096];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  char *last_slash = NULL;

  if (n > 0) {
    exe[n] = '\0';
    last_slash = strrchr(exe, '/');
    if (last_slash != NULL) {
      *last_slash = '\0';  // exe is now the directory; last_slash+1 is the binary name
      int r = snprintf(s_blacklist_path, sizeof(s_blacklist_path),
                       "%s/%s", exe, BLACKLIST_FILENAME);
      if (r > 0 && (size_t)r < sizeof(s_blacklist_path) &&
          access(s_blacklist_path, F_OK) == 0)
        return;
    }
  }

  // Attempt 2: CONF_DIR
  if (strcmp(CONF_DIR, ".") != 0) {
    int r = snprintf(s_blacklist_path, sizeof(s_blacklist_path),
                     "%s/%s", CONF_DIR, BLACKLIST_FILENAME);
    if (r > 0 && (size_t)r < sizeof(s_blacklist_path) &&
        access(s_blacklist_path, F_OK) == 0)
      return;
  }

  // Fallback for write target (file does not exist yet)
  // Prefer the executable directory so new entries land next to the binary
  if (last_slash != NULL) {
    int r = snprintf(s_blacklist_path, sizeof(s_blacklist_path),
                     "%s/%s", exe, BLACKLIST_FILENAME);
    if (r > 0 && (size_t)r < sizeof(s_blacklist_path))
      return;
  }

  if (strcmp(CONF_DIR, ".") != 0) {
    int r = snprintf(s_blacklist_path, sizeof(s_blacklist_path),
                     "%s/%s", CONF_DIR, BLACKLIST_FILENAME);
    if (r > 0 && (size_t)r < sizeof(s_blacklist_path))
      return;
  }

  // Last resort
  snprintf(s_blacklist_path, sizeof(s_blacklist_path), "./%s", BLACKLIST_FILENAME);
}

int blacklist_count(void) {
  DEFER_MUTEX(s_mutex);
  return s_blacklist_count;
}

int blacklist_load(void) {
  resolve_blacklist_path();

  FILE *f = fopen(s_blacklist_path, "r");
  if (!f) {
    return -1;
  }

  int count = 0;
  char line[64];

  while (fgets(line, sizeof(line), f) && count < BLACKLIST_MAX) {
    // Strip trailing whitespace / newline
    char *p = line + strlen(line) - 1;
    while (p >= line && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t'))
      *p-- = '\0';

    // Strip leading whitespace
    char *subnet = line;
    while (*subnet == ' ' || *subnet == '\t') {
      subnet++;
    }

    // Skip empty lines and comments
    if (*subnet == '\0' || *subnet == '#')
      continue;

    int prefix = 32;
    char *slash = strchr(subnet, '/');
    if (slash) {
      *slash = '\0';
      prefix = atoi(slash + 1);
      if (prefix < 0 || prefix > 32)
        prefix = 32;
    }

    struct in_addr a;
    if (inet_pton(AF_INET, subnet, &a) != 1) {
      fprintf(stdout, "blacklist: skipping invalid entry: %s\n", subnet);
      fflush(stdout);
      continue;
    }

// Unique version key definition: 7 * 10000 + 0 * 100 + 1 = 70001
#if defined(__clang__) && ((__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__) <= 70001)
    // Workaround for older Clang (7.0.1 in this case) on ARM64
    // Prevents incorrect bit shift (modulo 32) when prefix == 32
    uint32_t mask;
    if (prefix == 0) {
      mask = 0u;
    } else if (prefix == 32) {
      mask = 0xFFFFFFFFu;
    } else {
      mask = htonl(~(0xFFFFFFFFu >> prefix));
    }
#else
    // For GCC, newer Clang versions, or other architectures
    uint32_t mask = (prefix == 0) ? 0u : htonl(~(0xFFFFFFFFu >> prefix));
#endif

    s_blacklist[count].addr = a.s_addr & mask;
    s_blacklist[count].mask = mask;
    count++;

#ifdef DEBUG
    fprintf(stdout, "blacklist: added %s/%d => addr: 0x%08x, mask: 0x%08x\n", subnet, prefix, s_blacklist[count].addr, mask);
    fflush(stdout);
#endif
  }

  fclose(f);
  s_blacklist_count = count;
#ifdef DEBUG
  fprintf(stdout, "blacklist: loaded %d entries from %s\n", count, s_blacklist_path);
  fflush(stdout);
#endif
  return count;
}

// Checks whether the given IP address is on the blocklist
int blacklist_check(struct in_addr a) {
  DEFER_MUTEX(s_mutex);
  for (int i = 0; i < s_blacklist_count; i++) {
    if ((a.s_addr & s_blacklist[i].mask) == s_blacklist[i].addr)
      return 1;
  }
  return 0;
}

// Adds an IP address to s_blacklist and writes it to BLACKLIST_FILE.
// Returns 0 on success, -1 if the list is full or the address already exists.
int blacklist_add_ip(struct in_addr a, const char *ip_str) {
  int ret = 0;

  {
    DEFER_MUTEX(s_mutex);

    if (s_blacklist_count >= BLACKLIST_MAX) {
      fprintf(stdout, "blacklist: full (%d entries), cannot add %s\n", BLACKLIST_MAX, ip_str);
      fflush(stdout);
      ret = -1;
    } else {
      // Check for duplicate (without a separate blacklist_check — mutex is already held)
      for (int i = 0; i < s_blacklist_count; i++) {
        if ((a.s_addr & s_blacklist[i].mask) == s_blacklist[i].addr) {
          fprintf(stdout, "blacklist: %s already listed\n", ip_str);
          fflush(stdout);
          ret = -1;
          break;
        }
      }

      if (ret == 0) {
        uint32_t mask = 0xFFFFFFFFu;
        s_blacklist[s_blacklist_count].addr = a.s_addr & mask;
        s_blacklist[s_blacklist_count].mask = mask;
        s_blacklist_count++;
      }
    }
  } // <- mutex unlocked here by cleanup

  // Write to file outside the mutex — fopen/fwrite have their own locks at the libc level
  if (ret == 0) {
    FILE *f = fopen(s_blacklist_path, "a");
    if (f) {
      fprintf(f, "%s\n", ip_str);
      fclose(f);
    }
  }
  return ret;
}
