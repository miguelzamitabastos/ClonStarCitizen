#!/usr/bin/env bash
# PreToolUse hook (matcher: Bash) — bloquea `git commit` si el diff en stage
# contiene patrones de claves/secretos habituales.
set -euo pipefail

input="$(cat)"
command="$(printf '%s' "$input" | jq -r '.tool_input.command // empty')"

if ! printf '%s' "$command" | grep -qE '\bgit[[:space:]]+commit\b'; then
  echo '{"continue": true}'
  exit 0
fi

# AWS access key, bloque de clave privada, api_key/secret = "...", GitHub PAT, OpenAI/Anthropic-style sk-...
PATTERNS='(AKIA[0-9A-Z]{16})|(-----BEGIN [A-Z ]*PRIVATE KEY-----)|([Aa][Pp][Ii][_-]?[Kk][Ee][Yy]["'"'"']?[[:space:]]*[:=][[:space:]]*["'"'"'][A-Za-z0-9_\-]{16,})|([Ss][Ee][Cc][Rr][Ee][Tt]["'"'"']?[[:space:]]*[:=][[:space:]]*["'"'"'][A-Za-z0-9_\-]{16,})|(ghp_[A-Za-z0-9]{36})|(sk-[A-Za-z0-9]{20,})'

hits="$(git diff --cached | grep -EnoI "$PATTERNS" || true)"

if [ -n "$hits" ]; then
  preview="$(printf '%s' "$hits" | head -3 | tr '\n' ' ' | sed 's/"/\\"/g')"
  jq -n --arg reason "Patrón de secreto detectado en el diff en stage: $preview — revisa antes de commitear." '{
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: "deny",
      permissionDecisionReason: $reason
    }
  }'
  exit 0
fi

echo '{"continue": true}'
