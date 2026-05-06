# CortHex LLM proxy

Mac-side FastAPI service that turns natural-language prompts into a 6-patch
"bank" for the Plaits + Swords + Four Play voice via the Claude API.
The `/llm` page on the module POSTs `{session_id, user_message}` to
`/generate`; the proxy holds chat history per session, calls Claude with the
engine table + a `set_patch_bank` tool, and returns `{concept, patches[6]}`.
The iPad page (or this proxy directly, if configured) loads the bank into the
module via `POST /api/patch/bank`.

## First-time setup

```bash
cd tools/llm-proxy
python -m venv .venv && source .venv/bin/activate
pip install -e .

# Copy the env template and fill in your key + module host
cp .env.example .env
$EDITOR .env

# Sanity-run it once in the foreground (Ctrl-C to stop)
uvicorn proxy:app --host 0.0.0.0 --port 8000
```

The proxy auto-loads `.env` on startup via python-dotenv — no more
`export ANTHROPIC_API_KEY=...` on every shell session. `.env` is gitignored.

## Auto-start at login (recommended)

A `launchd` plist is checked into the repo at `com.corthex.proxy.plist`.
Install it once:

```bash
# Copy into LaunchAgents and load it
cp tools/llm-proxy/com.corthex.proxy.plist ~/Library/LaunchAgents/
launchctl load ~/Library/LaunchAgents/com.corthex.proxy.plist
```

The proxy now starts at login and respawns automatically if it crashes.
Logs go to `tools/llm-proxy/proxy.log` (`tail -f` it for live output).

Useful commands:

| Command | What it does |
|---|---|
| `launchctl unload ~/Library/LaunchAgents/com.corthex.proxy.plist` | Stop autostart (also kills the running process). |
| `launchctl kickstart -k gui/$(id -u)/com.corthex.proxy` | Restart now (e.g. after editing `.env` or pulling new code). |
| `tail -f tools/llm-proxy/proxy.log` | Watch live logs. |
| `curl http://localhost:8000/health` | Confirm it's running. |

If you move the repo, edit the absolute paths inside the plist file and
re-run the `launchctl load` step.

## Configuration

All values live in `tools/llm-proxy/.env` (gitignored). See
`.env.example` for the full list with comments.

| Env var                | Default              | Notes                                           |
|------------------------|----------------------|-------------------------------------------------|
| `ANTHROPIC_API_KEY`    | (required)           | Your Anthropic API key.                         |
| `ANTHROPIC_MODEL`      | `claude-opus-4-7`    | Override with `claude-sonnet-4-6` for speed/cost. |
| `ANTHROPIC_MAX_TOKENS` | `4096`               | Max output tokens per response.                 |
| `ANTHROPIC_EFFORT`     | `high`               | `low` / `medium` / `high` / `max`.              |
| `ANTHROPIC_THINKING`   | `adaptive`           | `adaptive` (default) or `off`.                  |
| `MODULE_HOST`          | (unset)              | e.g. `http://CortHex-ESP32.local`. Used only with `APPLY_TO_MODULE=1`. |
| `APPLY_TO_MODULE`      | `0`                  | Set `1` to have the proxy POST banks directly to the module after generation. |

## API

### `POST /generate`

Body:

```json
{ "session_id": "string", "user_message": "string" }
```

Returns:

```json
{
  "assistant_text": "Six pluck-bass variations from clean to gritty.",
  "concept": "Six pluck-bass variations from clean to gritty.",
  "patches": [
    {
      "name": "tight VA",
      "model": 8,
      "timbre":      { "v": 2.5,  "rise_ms": 0,  "fall_ms": 0 },
      "harmonics":   { "v": 0.8,  "rise_ms": 0,  "fall_ms": 0 },
      "swords_freq": { "v": 3.5,  "rise_ms": 5,  "fall_ms": 250 },
      "swords_res":  { "v": 1.0,  "rise_ms": 0,  "fall_ms": 0 },
      "vca":         { "v": 5.0,  "rise_ms": 1,  "fall_ms": 300 },
      "rationale": "..."
    },
    "...×6 total"
  ],
  "applied_to_module": true
}
```

`patches` is `null` if Claude asked a clarifying question instead of
emitting a bank.

### `POST /sessions/{id}/reset`

Wipes chat history for that session.

### `GET /health`

Liveness check; reports current configuration.

## Quick test

```bash
curl -sS -X POST http://localhost:8000/generate \
  -H "Content-Type: application/json" \
  -d '{"session_id": "test", "user_message": "Make me a snappy pluck bass"}' \
  | python3 -m json.tool
```
