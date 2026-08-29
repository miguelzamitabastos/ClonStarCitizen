#!/usr/bin/env bash
# PreToolUse hook (matcher: Bash) — bloquea merge/push a main/master si no hay
# un registro reciente de tests en verde.
#
# Convención: el rol `tester` (o el proceso de CI) escribe ".claude/.last-test-run"
# con el contenido "passed" o "failed" cada vez que corre la suite de tests.
set -euo pipefail

input="$(cat)"
command="$(printf '%s' "$input" | jq -r '.tool_input.command // empty')"

if ! printf '%s' "$command" | grep -qE '\bgit[[:space:]]+(merge|push)\b.*\b(main|master)\b'; then
  echo '{"continue": true}'
  exit 0
fi

MARKER=".claude/.last-test-run"
MAX_AGE_SECONDS=3600

if [ ! -f "$MARKER" ]; then
  jq -n --arg marker "$MARKER" '{
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: "deny",
      permissionDecisionReason: ("No hay registro de tests recientes (" + $marker + " no existe). Corre la suite de tests del proyecto antes de mergear/pushear a main.")
    }
  }'
  exit 0
fi

status="$(cat "$MARKER" 2>/dev/null || echo unknown)"
mtime="$(stat -f %m "$MARKER" 2>/dev/null || stat -c %Y "$MARKER" 2>/dev/null || echo 0)"
age=$(( $(date +%s) - mtime ))

if [ "$status" != "passed" ] || [ "$age" -gt "$MAX_AGE_SECONDS" ]; then
  jq -n --arg status "$status" --arg age "$age" '{
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: "deny",
      permissionDecisionReason: ("Último resultado de tests: " + $status + " (hace " + $age + "s). Vuelve a correr los tests antes de mergear/pushear a main.")
    }
  }'
  exit 0
fi

echo '{"continue": true}'
