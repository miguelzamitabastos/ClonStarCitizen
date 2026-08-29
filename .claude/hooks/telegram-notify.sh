#!/usr/bin/env bash
# Stop hook — notifica por Telegram (móvil + reloj vía mirroring) cuando
# Claude termina de responder, con la cuota de plan en caché (ver
# _framework/office-notify-quota.json). Usa _framework/office-notify.env
# (token + chat_id) de la oficina. No bloquea nunca: cualquier fallo se
# traga en silencio, esto es notificación, no seguridad.
set -uo pipefail

ENV_FILE="${OFFICE_NOTIFY_ENV:-$HOME/proyects/ClaudeWorkstation/_framework/office-notify.env}"
QUOTA_FILE="${OFFICE_NOTIFY_QUOTA_FILE:-$HOME/proyects/ClaudeWorkstation/_framework/office-notify-quota.json}"
finish() { echo '{"continue": true}'; exit 0; }

[ -f "$ENV_FILE" ] || finish
set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a
[ -n "${TELEGRAM_BOT_TOKEN:-}" ] || finish
[ -n "${TELEGRAM_CHAT_ID:-}" ] || finish
command -v jq >/dev/null 2>&1 || finish

input="$(cat)"

cwd="$(printf '%s' "$input" | jq -r '.cwd // empty')"
project="$(basename "${cwd:-$(pwd)}")"

raw="$(printf '%s' "$input" | jq -r '.last_assistant_message // empty')"
[ -n "$raw" ] || raw="(sin resumen de texto)"

# La norma de citar memorias envuelve la frase en <cc-memory filenames="...">...</cc-memory>
# -- pensado para que la UI de chat lo renderice como una insignia, no para texto plano.
# Telegram no interpreta la etiqueta: la muestra literal y, peor, el recorte a MAXLEN cuenta
# la etiqueta entera, comiéndose la mayor parte del hueco antes de llegar al texto real.
# Quitamos solo las etiquetas, dejando la frase citada tal cual (confirmado 2026-08-19).
raw="$(printf '%s' "$raw" | sed -E 's/<cc-memory[^>]*>//g; s#</cc-memory>##g')"

# Norma de verificación manual del framework (ver framework-universal-oficina-claude.md,
# sección 11): si la respuesta final incluye una sección "### 🔎 VERIFICACIÓN MANUAL",
# se separa del resumen y se manda como mensaje propio, sin recortar a 220 caracteres,
# para que la lista de qué debe comprobar Miguel llegue completa al móvil/reloj.
MARKER='### 🔎 VERIFICACIÓN MANUAL'
manual_block=""
before_marker="$raw"
if printf '%s' "$raw" | grep -qF "$MARKER"; then
  before_marker="$(printf '%s' "$raw" | awk -v m="$MARKER" 'index($0,m){exit} {print}')"
  manual_block="$(printf '%s' "$raw" | awk -v m="$MARKER" 'index($0,m){f=1} f{print}')"
fi

summary="$(printf '%s' "$before_marker" | tr '\n' ' ' | tr -s ' ')"
[ -n "$summary" ] || summary="(sin resumen de texto)"

MAXLEN=220
short="$summary"
if [ "${#summary}" -gt "$MAXLEN" ]; then
  short="$(printf '%s' "$summary" | cut -c1-"$MAXLEN")…"
fi

text="✅ ${project}
${short}"

send_telegram() {
  curl -s -m 10 "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage" \
    --data-urlencode "chat_id=${TELEGRAM_CHAT_ID}" \
    --data-urlencode "text=$1" \
    >/dev/null 2>&1
}

# Mensaje 1: resumen de la tarea — se manda ya, corto, para que no se corte en el reloj.
send_telegram "$text"

# Mensaje 1b (aparte, solo si hay sección de verificación manual): sin recortar
# (dentro del límite de Telegram de 4096 caracteres UTF-8, con margen).
if [ -n "$manual_block" ]; then
  manual_text="⚠️ ${project} — verificación manual pendiente
${manual_block}"
  if [ "${#manual_text}" -gt 3900 ]; then
    manual_text="$(printf '%s' "$manual_text" | cut -c1-3900)…"
  fi
  send_telegram "$manual_text"

  # Además de Telegram, deja constancia dentro del propio proyecto: así no
  # depende solo del scroll del chat para saber qué tenía pendiente de probar
  # y dónde (URL/comando/archivo), lo consulta directamente en el repo.
  verify_dir="${cwd:-$(pwd)}/.claude"
  if [ -d "$verify_dir" ]; then
    {
      echo "# Verificación manual pendiente"
      echo
      echo "_Generado $(date -u +%Y-%m-%dT%H:%M:%SZ) por telegram-notify.sh (Stop hook) — se sobrescribe en cada sesión que genere una nueva verificación._"
      echo
      printf '%s\n' "$manual_block"
    } > "${verify_dir}/VERIFICACION-PENDIENTE.md" 2>/dev/null
  fi
fi

# --- Mensaje 2 (aparte): cuota de plan (caché, se refresca de forma oportunista, no en vivo) ---
bar_for_pct() {
  local pct="$1" filled empty b=""
  [ "$pct" -lt 0 ] 2>/dev/null && pct=0
  [ "$pct" -gt 100 ] 2>/dev/null && pct=100
  filled=$(( pct / 10 ))
  empty=$(( 10 - filled ))
  for ((i = 0; i < filled; i++)); do b+="▓"; done
  for ((i = 0; i < empty; i++)); do b+="░"; done
  printf '%s' "$b"
}

if [ -f "$QUOTA_FILE" ]; then
  session_pct="$(jq -r '.session_pct // empty' "$QUOTA_FILE" 2>/dev/null)"
  weekly_pct="$(jq -r '.weekly_pct // empty' "$QUOTA_FILE" 2>/dev/null)"
  session_resets="$(jq -r '.session_resets // empty' "$QUOTA_FILE" 2>/dev/null)"
  weekly_resets="$(jq -r '.weekly_resets // empty' "$QUOTA_FILE" 2>/dev/null)"
  checked_at="$(jq -r '.checked_at // empty' "$QUOTA_FILE" 2>/dev/null)"

  if [ -n "$session_pct" ] && [ -n "$weekly_pct" ]; then
    checked_epoch="$(date -j -f "%Y-%m-%dT%H:%M:%SZ" "$checked_at" +%s 2>/dev/null || date -d "$checked_at" +%s 2>/dev/null || echo 0)"
    now_epoch="$(date +%s)"
    age_min=$(( (now_epoch - checked_epoch) / 60 ))
    [ "$age_min" -lt 0 ] 2>/dev/null && age_min=0
    if [ "$age_min" -lt 60 ]; then
      age_str="hace ${age_min} min"
    else
      age_str="hace $(( age_min / 60 )) h"
    fi

    quota_text="📊 Sesión: $(bar_for_pct "$session_pct") ${session_pct}% (resetea ${session_resets})
📊 Semana: $(bar_for_pct "$weekly_pct") ${weekly_pct}% (resetea ${weekly_resets})
🕐 actualizada ${age_str}"

    send_telegram "$quota_text"
  fi
fi

finish
