# Redis Archive Module (with SQLite)

A custom Redis module that supports archiving keys from Redis to SQLite based on key content (JSON timestamps).

## 🔧 Features

- `ARCHIVE.SAVE <key>` – Manually archive a key
- `ARCHIVE.GET <key>` – Fetch a key from archive (SQLite)
- `ARCHIVE.SWEEP <pattern> <timestamp_field> <max_timestamp>` – Auto-archive keys with JSON timestamps

## 🛠️ How It Works

- Keys are stored as JSON strings in Redis, like:
  ```json
  { "name": "Alice", "created": 1712600000 }
  ```

- Archived data is saved in `archive.db` (SQLite) under:
  ```sql
  CREATE TABLE archive (key TEXT PRIMARY KEY, value TEXT);
  ```

## 🚀 Getting Started (Docker)

```bash
# Build image
docker build -t redis-archive .

# Run Redis with module
docker run -it --rm -p 6379:6379 redis-archive
```

## 🧪 Example Usage

```bash
# Connect
redis-cli

# Add test key
> set user:1001 '{"name":"Alice","created":1712600000}'

# Manually archive it
> archive.save user:1001

# Restore (if you add restore command)
> archive.get user:1001

# Sweep all matching keys older than timestamp
> archive.sweep user:* created 1712650000
```

## 📦 File Structure

- `archive.c` – Redis module source
- `cJSON.[ch]` – JSON parsing
- `Makefile` – Build archive.so
- `Dockerfile` – Dockerized Redis with this module
- `entrypoint.sh` – Entrypoint to run Redis with module

## 🧠 Todo Ideas

- Add `archive.restore <key>`
- Store archive timestamps or metadata
- Background sweeping
- API wrapper in Python

---

Built with ❤️ using Redis Modules, SQLite, and C.
