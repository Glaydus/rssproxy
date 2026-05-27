#ifndef BLACKLIST_H
#define BLACKLIST_H

#include <arpa/inet.h>

// Load (or reload) the IPv4 blacklist from ./blacklist.ip.
// Accepts plain addresses and CIDR notation (e.g. 192.168.1.0/24).
// Returns the number of entries loaded, or -1 if the file cannot be opened.
int blacklist_load(void);

// Return 1 if the IPv4 address `a` matches any blacklist entry, 0 otherwise.
int blacklist_check(struct in_addr a);

// Add an IPv4 address to the blacklist and persist it to BLACKLIST_FILE.
// ip_str is the dotted-decimal string representation of `a` (used for logging
// and writing to the file).  Returns 0 on success, -1 if the blacklist is full
// or the address is already listed.
int blacklist_add_ip(struct in_addr a, const char *ip_str);

#endif // BLACKLIST_H
