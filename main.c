#include "blacklist.h"
#include "source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <microhttpd.h>
#include <curl/curl.h>

// Compatibility with older microhttpd that lacks enum MHD_Result (added in 0x00097701)
#if MHD_VERSION < 0x00097701
typedef int MHD_Result;
#else
typedef enum MHD_Result MHD_Result;
#endif

#define UPSTREAM_TIMEOUT_SEC 15L

//  Curl header callback — extract ETag and check if ETag has not changed
static size_t curl_header_cb(char *buffer, size_t size, size_t nitems,
                             void *userdata) {
  response_t *resp = (response_t *)userdata;
  size_t len = size * nitems;

  // Check for ETag header
  if (len > 6 && strncasecmp(buffer, "ETag:", 5) == 0) {
    const char *val = buffer + 5;
    while (*val == ' ' && val < buffer + len)
      val++;
    size_t vlen = len - (val - buffer);
    while (vlen > 0 && (val[vlen - 1] == '\r' || val[vlen - 1] == '\n'))
      vlen--;

    vlen = MIN(vlen, ETAG_MAX - 1);
    memcpy(resp->etag, val, vlen);
    resp->etag[vlen] = '\0';
  }
  return len;
}

// Curl write callback — accumulate response body
static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb,
                            void *userdata) {
  response_t *resp = (response_t *)userdata;
  size_t n = size * nmemb;

  // ETag matched — upstream returned 200 with the same ETag (ignored If-None-Match),
  // abort body download to avoid unnecessary transfer; fetch_upstream will treat this as 304.
  if (resp->prev_etag[0] != '\0' && strcmp(resp->etag, resp->prev_etag) == 0)
    return 0;

  // Reallocate buffer if needed
  if (resp->len + n + 1 > resp->cap) {
    size_t need = resp->len + n + 1;
    size_t new_cap = (need + 4095) & ~(size_t)4095;  // round up to 4K page
    char *tmp = realloc(resp->buf, new_cap);
    if (tmp == NULL)
      return 0;
    resp->buf = tmp;
    resp->cap = new_cap;
  }

  memcpy(resp->buf + resp->len, ptr, n);
  resp->len += n;
  resp->buf[resp->len] = '\0';
  return n;
}

// Fetch from upstream using curl (with HTTP/2 support)
static int fetch_upstream(rss_source_t *src, response_t *resp,
                          long *out_status) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return -1;

  // Pass previous ETag so header callback can detect unchanged content
  resp->prev_etag = src->etag;

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/xml, text/xml, */*");
  headers = curl_slist_append(headers, "Accept-Language: pl-PL, pl;q=0.9, en-US;q=0.8, en;q=0.7");
  headers = curl_slist_append(headers, "Cache-Control: no-cache");

  if (src->etag[0] != '\0') {
    char etag_hdr[16 + sizeof(src->etag)];
    snprintf(etag_hdr, sizeof(etag_hdr), "If-None-Match: %s", src->etag);
    headers = curl_slist_append(headers, etag_hdr);
  }

  curl_easy_setopt(curl, CURLOPT_URL, src->uri);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                   "(KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, UPSTREAM_TIMEOUT_SEC);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, resp);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

  CURLcode result = curl_easy_perform(curl);
  long status = 0;

  int same_etag = resp->etag[0] != '\0' && strcmp(resp->etag, resp->prev_etag) == 0;

  if (result == CURLE_OK || (result == CURLE_WRITE_ERROR && same_etag)) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    *out_status = same_etag ? 304 : status;
  } else {
    *out_status = 0;
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return *out_status ? 0 : -1;
}

// Find RSS source by path
static rss_source_t *find_source(const char *path) {
  rss_sources_t *sources = sources_get();
  for (size_t i = 0; i < sources->count; i++) {
    if (strcmp(path, sources->entries[i].name) == 0)
      return &sources->entries[i];
  }
  return NULL;
}

// Get remote IP address from MHD connection into buf (at least 64 bytes)
static void get_remote_ip(struct MHD_Connection *conn, char *buf, size_t len) {
  snprintf(buf, len, "unknown");

  const union MHD_ConnectionInfo *ci = MHD_get_connection_info(conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
  if (!ci || !ci->client_addr)
    return;

  struct sockaddr *sa = (struct sockaddr *)ci->client_addr;
  if (sa->sa_family == AF_INET) {
    inet_ntop(AF_INET, &((struct sockaddr_in *)sa)->sin_addr, buf, len);
  } else if (sa->sa_family == AF_INET6) {
    inet_ntop(AF_INET6, &((struct sockaddr_in6 *)sa)->sin6_addr, buf, len);
  }
}

// Queue response, destroy it, and return the result
static MHD_Result reply(struct MHD_Connection *conn,
                        struct MHD_Response *response,
                        unsigned int status, response_t *resp) {
  if (resp)
    free(resp->buf);

  MHD_Result ret = MHD_queue_response(conn, status, response);
  MHD_destroy_response(response);
  return ret;
}

// HTTP request handler for microhttpd
static MHD_Result request_handler(void *cls, struct MHD_Connection *conn,
                                        const char *url, const char *method,
                                        const char *version, const char *upload,
                                        size_t *upload_size, void **con_cls) {
  (void) cls; (void) version; (void) upload; (void) upload_size; (void) con_cls;

  if (strcmp(method, "GET") != 0) {
    return reply(conn,
        MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT),
        MHD_HTTP_BAD_REQUEST, NULL);
  }

  char remote[64];

  // Check blacklist and auto-blacklist probing attempts
  const union MHD_ConnectionInfo *ci = MHD_get_connection_info(conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
  if (ci && ci->client_addr && ((struct sockaddr *)ci->client_addr)->sa_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in *)ci->client_addr;
    inet_ntop(AF_INET, &sin->sin_addr, remote, sizeof(remote));

    if (blacklist_check(sin->sin_addr)) {
      fprintf(stdout, "Blocked request: method=%s path=%s remote=%s\n", method, url, remote);
      fflush(stdout);
      return reply(conn,
          MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT),
          MHD_HTTP_FORBIDDEN, NULL);
    }

    // Block path traversal attempts
    if (strstr(url, "..") != NULL) {
      fprintf(stdout, "Auto-blacklisting %s for path traversal attempt: %s\n", remote, url);
      fflush(stdout);
      blacklist_add_ip(sin->sin_addr, remote);
      return reply(conn,
          MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT),
          MHD_HTTP_FORBIDDEN, NULL);
    }
  }

  rss_source_t *src = find_source(url);
  if (!src) {
    get_remote_ip(conn, remote, sizeof(remote));
    fprintf(stdout, "Received request: method=%s path=%s remote=%s\n", method, url, remote);
    fflush(stdout);
    return reply(conn,
        MHD_create_response_from_buffer(10, (void *)"Not found\n", MHD_RESPMEM_PERSISTENT),
        MHD_HTTP_NOT_FOUND, NULL);
  }

  // Fetch from upstream
  response_t resp = {0};
  long status = 0;

  if (fetch_upstream(src, &resp, &status) != 0) {
    return reply(conn,
        MHD_create_response_from_buffer(30, (void *)"Error connecting to upstream\n", MHD_RESPMEM_PERSISTENT),
        MHD_HTTP_BAD_GATEWAY, &resp);
  }

  // Handle different status codes

  if (status == 304) {
    return reply(conn,
        MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT),
        MHD_HTTP_NOT_MODIFIED, &resp);
  }

  if (status != 200) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s returned status: %ld\n", src->name + 1, status);
    return reply(conn,
        MHD_create_response_from_buffer(strlen(msg), (void *)msg, MHD_RESPMEM_MUST_COPY),
        status, &resp);
  }

  // Update ETag if present and content actually changed
  if (resp.etag[0] != '\0') {
    size_t len = MIN(strlen(resp.etag), sizeof(src->etag) - 1);
    memcpy(src->etag, resp.etag, len);
    src->etag[len] = '\0';
  }

  // Return successful response with XML content
  struct MHD_Response *response =
      MHD_create_response_from_buffer(resp.len, resp.buf, MHD_RESPMEM_MUST_FREE);
  MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/xml");

  // mode MHD_RESPMEM_MUST_FREE, so resp as been freed by MHD
  return reply(conn, response, MHD_HTTP_OK, NULL);
}

static int s_signo = 0;

// Signal handler to stop the server gracefully
static void signal_handler(int sig) { s_signo = sig; }


// Main function — load sources and blacklist, start server, and wait for termination signal
int main(int argc, char *argv[]) {
  (void) argc; (void) argv;

  // Load RSS sources — exit if none found
  if (sources_load() == 0) {
    fprintf(stderr, "No RSS sources configured, exiting\n");
    return 1;
  }

  // Load IP blacklist
  blacklist_load();

  // Initialize curl
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Set port from environment variable or default to 8889
  const char *port_str = getenv("PORT");
  int port = port_str ? atoi(port_str) : 8889;

  // Catch SIGINT and SIGTERM to stop the server gracefully
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Start microhttpd server
  struct MHD_Daemon *daemon =
      MHD_start_daemon(MHD_USE_AUTO_INTERNAL_THREAD, port, NULL, NULL,
                       request_handler, NULL, MHD_OPTION_END);

  if (!daemon) {
    fprintf(stderr, "Failed to start server on port %d\n", port);
    curl_global_cleanup();
    return 1;
  }

  printf("RSS Proxy listening on port %d\n", port);
  fflush(stdout);

  // Loop until signalled to stop
  while (!s_signo) {
    sleep(1);
  }

  MHD_stop_daemon(daemon);
  curl_global_cleanup();
  sources_free();

  printf("Server stopped\n");
  return 0;
}
