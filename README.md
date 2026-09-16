# URL Shortener — C++20 + SQLite

A self-contained URL shortener built from the ground up with C++20, SQLite, and a lightweight HTTP architecture.

## Why SQLite?

SQLite is used instead of PostgreSQL because it fits this project well:

- zero database server to install or configure
- database is a single local file
- ACID transactions and a unique constraint provide safe URL/code creation
- WAL mode allows concurrent readers while SQLite serializes writes safely
- easy local development and deployment
- the application still uses a connection pool, so the database layer remains separated from the service layer

For a multi-instance production deployment with several application servers writing to the same database, a client/server database such as MySQL or PostgreSQL would be a better fit. This version intentionally optimizes for a self-contained URL-shortener project.

## Features

- 7-character cryptographically secure short codes
- SQLite persistence
- collision-safe short-code insertion
- idempotent creation for duplicate long URLs
- optional expiration and `410 Gone`
- atomic database click counting
- per-IP request limiting: 100 requests / 60-second sliding window
- bounded thread-safe access-expiring LRU-style cache: 10,000 entries / 10 minutes
- SQLite connection pooling
- parameterized SQL statements
- automatic database/schema initialization
- environment-based configuration
- clean, consistent HTTP API for URL shortening and redirection

The test suite covers:

- short-code generation
- cache behavior
- JSON parsing and malformed JSON
- rate limiting
- concurrent rate limiting
- SQLite schema initialization
- URL creation
- duplicate URL idempotency
- short-code collision handling
- URL lookup
- expiration storage
- concurrent click counting

## Run

The database is created automatically on first start.

```bash
export DB_PATH="$PWD/urlshortener.db"
export PORT=7070
export BASE_URL="http://localhost:7070"
./build/url_shortener
```

Configuration:

| Variable | Default | Description |
|---|---|---|
| `DB_PATH` | `urlshortener.db` | SQLite database file |
| `DB_POOL_SIZE` | `10` | Number of SQLite connections |
| `PORT` | `7070` | HTTP server port |
| `BASE_URL` | `http://localhost:7070` | Base URL returned by the API |

## API

### Create a short URL

```bash
curl -i -X POST http://localhost:7070/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://example.com"}'
```

With expiration:

```bash
curl -i -X POST http://localhost:7070/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://example.com","expiresInSecond":3600}'
```

Response:

```json
{"shortUrl":"http://localhost:7070/Ab12Cd3"}
```

### Redirect

```bash
curl -i http://localhost:7070/Ab12Cd3
```

A valid, non-expired short code returns `302 Found` with the original URL in the `Location` header.

### Error behavior

- `400` — malformed request, missing URL, invalid URL, or invalid expiration
- `404` — short code does not exist
- `410` — short code exists but has expired
- `429` — rate limit exceeded
- `405` — unsupported HTTP method
- `500` — unexpected server/database error

## Database schema

The schema is in `schema.sql`, but the application also initializes it automatically.

The important constraints are:

```sql
short_code TEXT NOT NULL UNIQUE
long_url   TEXT NOT NULL UNIQUE
```

These database-level constraints are important because application-side checks alone are not sufficient when multiple requests arrive concurrently.

## Architecture

```text
HTTP client
    |
    v
HttpServer
    |
    +--> RateLimiter
    |
    v
UrlService
    |
    v
UrlRepository
    |
    +--> AccessTtlCache
    |
    v
Database connection pool
    |
    v
SQLite (WAL)
```

## Security/concurrency notes

- SQL values are bound parameters rather than concatenated into SQL strings.
- Short codes use OpenSSL `RAND_bytes`.
- Click counting is performed atomically inside SQLite.
- Cache and rate-limiter state are protected with mutexes.
- SQLite connections use `FULLMUTEX`, WAL mode, and a busy timeout.
- The application intentionally accepts only `http://` and `https://` URLs.
