#!/usr/bin/env bash
# Cursor MCP allowlist / auto-run fix (Linux).
# If Cursor keeps asking for permission on every MCP call, the old "MCP Tools Protection"
# setting in state.vscdb overrides the UI. Run this script after CLOSING Cursor, then
# restart Cursor.
# See: https://forum.cursor.com/t/mcp-allowlist-doesnt-work-also-cant-be-edited/135594/14

set -euo pipefail

if [ -n "${CURSOR_CONFIG_ROOT:-}" ]; then
	ROOT="$CURSOR_CONFIG_ROOT"
else
	ROOT="${HOME}/.config/Cursor"
fi

STAMP=$(date +%Y%m%d-%H%M%S)
KEY='src.vs.platform.reactivestorage.browser.reactiveStorageServiceImpl.persistentStorage.applicationUser'

if ! command -v sqlite3 >/dev/null 2>&1; then
	echo "sqlite3 is missing. Install it (e.g. apt install sqlite3)." >&2
	exit 1
fi

if [ ! -d "$ROOT" ]; then
	echo "Cursor config not found: $ROOT" >&2
	exit 1
fi

count=0
while IFS= read -r -d '' DB; do
	cp "$DB" "$DB.bak.$STAMP" || true
	sqlite3 "$DB" "PRAGMA busy_timeout=5000; BEGIN;
	UPDATE ItemTable SET value=json_set(value,
	 '$.shouldAutoContinueToolCall', 1,
	 '$.yoloMcpToolsDisabled', 0,
	 '$.isAutoApplyEnabled', 1
	) WHERE key='$KEY' AND json_valid(value);
	UPDATE ItemTable SET value=json_set(value,
	 '$.composerState.useYoloMode', 0,
	 '$.composerState.shouldAutoContinueToolCall', 1,
	 '$.composerState.yoloMcpToolsDisabled', 0,
	 '$.composerState.isAutoApplyEnabled', 1,
	 '$.composerState.modes4[0].autoRun', 1,
	 '$.composerState.modes4[0].fullAutoRun', 1
	) WHERE key='$KEY' AND json_valid(value);
	UPDATE ItemTable SET value=REPLACE(value,'\"mcpEnabled\": false','\"mcpEnabled\": true')
	WHERE key='$KEY' AND value LIKE '%\"mcpEnabled\": false%';
	COMMIT;" 2>/dev/null || true
	sqlite3 -readonly "$DB" "SELECT '$DB',
	json_extract(value,'$.composerState.shouldAutoContinueToolCall'),
	json_extract(value,'$.composerState.yoloMcpToolsDisabled'),
	json_extract(value,'$.composerState.modes4[0].fullAutoRun')
	FROM ItemTable WHERE key='$KEY';" 2>/dev/null || true
	count=$((count + 1))
done < <(find "$ROOT" -type f -name state.vscdb -print0 2>/dev/null)

if [ "$count" -eq 0 ]; then
	echo "No state.vscdb found under: $ROOT" >&2
	exit 1
fi

echo "Done. Backup: $ROOT/**/state.vscdb.bak.$STAMP"
echo "Restart Cursor and try the MCP tools."
