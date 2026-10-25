#!/usr/bin/env bash
# Smoke-test a running hass-bg-gateway or tools/mock_gateway.py.
#
# Usage:
#   ./tools/test_gateway.sh [wav]
#   GATEWAY=http://192.168.1.10:8125 BGV_API_KEY=secret ./tools/test_gateway.sh
set -euo pipefail

WAV="${1:-/tmp/bg-voice-test.wav}"
GATEWAY="${GATEWAY:-http://127.0.0.1:18125}"
KEY="${BGV_API_KEY:-}"
OUT="${OUT:-/tmp/bg-voice-response.json}"

if [[ ! -f "$WAV" ]]; then
  echo "missing WAV: $WAV (run: make test-wav)" >&2
  exit 1
fi

HEADERS=()
if [[ -n "$KEY" ]]; then
  HEADERS=(-H "X-API-Key: $KEY")
fi

echo "POST $GATEWAY/v1/voice ($WAV)"
curl -sS -D /tmp/bg-voice-headers.txt -o "$OUT" \
  -X POST "$GATEWAY/v1/voice" \
  "${HEADERS[@]}" \
  -F "audio=@${WAV};type=audio/wav"

echo "--- response headers ---"
cat /tmp/bg-voice-headers.txt
echo "--- response body ---"
python3 -m json.tool "$OUT"
