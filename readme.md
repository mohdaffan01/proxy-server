# HTTP Proxy Server with LRU Caching

A multi-threaded HTTP proxy server in C with LRU cache for improved performance.

## Project Files

- `proxy_server_with_cache.c` - Main implementation (authored)
- `proxy_parse.c`, `proxy_parse.h` - HTTP parsing utilities (from GitHub)

## Features

- Multi-threaded (up to 400 concurrent clients)
- LRU caching (200 MB cache, 10 MB max per element)
- HTTP/1.0 and HTTP/1.1 support
- Thread-safe cache operations

## Compilation

```bash
gcc -o proxy_server proxy_server_with_cache.c proxy_parse.c -lpthread
```

## Usage

```bash
./proxy_server <port_number>
```

Example:
```bash
./proxy_server 8080
```

## How It Works

1. Client connects to proxy
2. Proxy checks cache (hit = instant return)
3. On miss: fetches from remote server, sends to client, caches response
4. LRU eviction when cache is full

## Configuration

Edit these constants in source code:

```c
#define MAX_BYTES 4096              // Max request/response size
#define MAX_CLIENTS 400             // Max concurrent clients
#define MAX_SIZE 200*(1<<20)        // Cache size (200 MB)
#define MAX_ELEMENT_SIZE 10*(1<<20) // Max element size (10 MB)
```

## Browser Setup

**Firefox**: Settings → Network Settings → Manual proxy configuration
- HTTP Proxy: `localhost`, Port: `8080`

## Testing

```bash
curl -x http://localhost:8080 http://example.com
```

## Supported

- HTTP GET requests only
- Error codes: 400, 403, 404, 500, 501, 505

## Limitations

- No HTTPS support
- GET requests only
- No persistent cache (resets on restart)

## Author

MOHD AFFAN