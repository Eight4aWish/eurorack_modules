# CortHex LLM proxy

Mac-side FastAPI service that turns natural-language prompts into Plaits-voice
patches via the Claude API. The iPad page POSTs `{session_id, user_message}` to
`/generate`; the proxy holds chat history per session, calls Claude with the
engine table + a `set_patch` tool, and returns `{assistant_text, patch}`. The
iPad applies the patch to the module's `/api/patch` endpoint.

Optionally, the proxy can forward patches to the module itself for curl-only
testing (set `MODULE_HOST` and `APPLY_TO_MODULE=1`).

## Setup

```bash
cd tools/llm-proxy
python -m venv .venv && source .venv/bin/activate
pip install -e .
export ANTHROPIC_API_KEY=sk-ant-...
uvicorn proxy:app --host 0.0.0.0 --port 8000
```

## Configuration

| Env var                | Default              | Notes                                           |
|------------------------|----------------------|-------------------------------------------------|
| `ANTHROPIC_API_KEY`    | (required)           | Your Anthropic API key.                         |
| `ANTHROPIC_MODEL`      | `claude-opus-4-7`    | Override with `claude-sonnet-4-6` for speed/cost. |
| `ANTHROPIC_MAX_TOKENS` | `4096`               | Max output tokens per response.                 |
| `ANTHROPIC_EFFORT`     | `high`               | `low` / `medium` / `high` / `max`.              |
| `ANTHROPIC_THINKING`   | `adaptive`           | `adaptive` (default) or `off`.                  |
| `MODULE_HOST`          | (unset)              | e.g. `http://CortHex-ESP32.local`. Used only with `APPLY_TO_MODULE=1`. |
| `APPLY_TO_MODULE`      | `0`                  | Set `1` to have the proxy POST patches directly to the module after generation. |

## API

### `POST /generate`

Body:

```json
{ "session_id": "string", "user_message": "string" }
```

Returns:

```json
{
  "assistant_text": "Pluck bass — Pair VA with quick filter snap.",
  "patch": {
    "model": 8,
    "timbre":      { "v": 2.5,  "rise_ms": 0,  "fall_ms": 0 },
    "harmonics":   { "v": 0.8,  "rise_ms": 0,  "fall_ms": 0 },
    "swords_freq": { "v": 3.5,  "rise_ms": 5,  "fall_ms": 250 },
    "swords_res":  { "v": 1.0,  "rise_ms": 0,  "fall_ms": 0 },
    "vca":         { "v": 5.0,  "rise_ms": 1,  "fall_ms": 300 },
    "rationale": "..."
  },
  "applied_to_module": false
}
```

`patch` is `null` if Claude asked a clarifying question instead of patching.

### `POST /sessions/{id}/reset`

Wipes chat history for that session.

### `GET /health`

Liveness check; reports current configuration.

## Quick test

```bash
curl -sS -X POST http://localhost:8000/generate \
  -H "Content-Type: application/json" \
  -d '{"session_id": "test", "user_message": "Make me a snappy pluck bass"}'
```

To have the proxy apply the patch to the module directly:

```bash
export MODULE_HOST=http://CortHex-ESP32.local
export APPLY_TO_MODULE=1
uvicorn proxy:app --host 0.0.0.0 --port 8000
```

## Auto-start on Mac (optional)

Drop a `~/Library/LaunchAgents/com.corthex.proxy.plist` with the right
`WorkingDirectory` and `ProgramArguments` to run uvicorn at login.
