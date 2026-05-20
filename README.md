# rssproxy

A lightweight HTTP proxy for RSS/Atom feeds, written in C. It sits between RSS clients and upstream feed servers, forwarding requests and transparently handling HTTP/2, ETag-based caching, and IP-level access control.

## Features

- **Feed proxying** — maps local URL paths to upstream feed URIs; any number of sources up to 512
- **ETag caching** — tracks ETags per source and sends `If-None-Match` on subsequent requests; returns `304 Not Modified` to clients when content has not changed, avoiding unnecessary data transfer
- **HTTP/2 upstream** — fetches feeds over HTTP/2 (with TLS) when supported by the upstream server
- **IP blacklist** — loads a static blocklist from `blacklist.ip` (plain addresses and CIDR notation); blocked clients receive `403 Forbidden`
- **Auto-blacklisting** — any client sending a path traversal attempt (`..`) is automatically added to the blacklist at runtime and persisted to `blacklist.ip`
- **Portable deployment** — the binary looks for `rssproxy.conf` in its own directory first, then falls back to the compile-time `CONF_DIR`; a single directory with the binary and config file is sufficient to run the proxy
- **systemd integration** — `./configure` generates a ready-to-use `rssproxy.service` unit file

## Requirements

- C99 compiler (GCC or Clang)
- [libcurl](https://curl.se/libcurl/) with HTTP/2 support
- [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)
- `pkg-config`
- Linux (uses `/proc/self/exe` for executable path resolution)

On Debian/Ubuntu:

```sh
sudo apt install build-essential libcurl4-openssl-dev libmicrohttpd-dev pkg-config
```

On Fedora/RHEL:

```sh
sudo dnf install gcc libcurl-devel libmicrohttpd-devel pkgconf
```

## Building and installing

```sh
./configure
make
sudo make install
```

To install under a custom prefix:

```sh
./configure --prefix=/usr
make
sudo make install
```

The `configure` script accepts the following options:

| Option | Description | Default |
|---|---|---|
| `--prefix=DIR` | Install architecture-independent files under DIR | `/usr/local` |
| `--sysconfdir=DIR` | Install configuration files under DIR | `PREFIX/etc` |
| `-h`, `--help` | Display help and exit | |

After `make install` the layout is:

```
PREFIX/bin/rssproxy
SYSCONFDIR/rssproxy/rssproxy.conf
/usr/lib/systemd/system/rssproxy.service   (or /lib/systemd/system/)
```

## Portable (single-directory) deployment

If you prefer to keep the binary and configuration together without installing system-wide, simply copy both files to the same directory and run the binary from there:

```sh
make
mkdir -p /opt/rssproxy
cp bin/rssproxy rssproxy.conf /opt/rssproxy/
cd /opt/rssproxy/
./rssproxy
```

The binary always checks its own directory for `rssproxy.conf` before consulting `CONF_DIR`.

## Configuration

### rssproxy.conf

One source per line, format: `<path> <upstream-url>`

```
# Lines starting with '#' are ignored
/feed1   https://example.com/feed.xml
/feed2   https://other.example.org/rss
```

The `<path>` becomes the local URL path served by the proxy. With the example above, a client would request `http://localhost:8889/feed1`.

### blacklist.ip

Optional file in the working directory. One entry per line — plain IPv4 address or CIDR notation:

```
# Lines starting with '#' are ignored
192.0.2.1
10.0.0.0/8
```

The file is read at startup. Entries added at runtime (auto-blacklisting) are appended to this file automatically.

### Port

The listening port defaults to `8889` and can be overridden with the `PORT` environment variable:

```sh
PORT=9000 rssproxy
```

## Running as a systemd service

After `sudo make install`, enable and start the service:

```sh
sudo systemctl daemon-reload
sudo systemctl enable rssproxy
sudo systemctl start rssproxy
```

Or use the `deploy` make target to reload and restart in one step:

```sh
make deploy
```

## Development build

```sh
make debug
```

Produces an unoptimized binary with debug symbols and verbose logging (`-DDEBUG`).

## License

MIT
