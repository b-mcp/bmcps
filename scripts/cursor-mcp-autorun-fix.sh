#!/usr/bin/env bash
# Cursor MCP allowlist / auto-run fix (Linux).
# Ha minden MCP hívásnál újra kér engedélyt, a Cursor régi "MCP Tools Protection"
# beállítása a state.vscdb-ben override-olja a UI-t. Ezt a script Cursor BEZÁRÁSA
# után futtatod; utána indítsd újra a Cursort.
# Lásd: https://forum.cursor.com/t/mcp-allowlist-doesnt-work-also-cant-be-edited/135594/14

set -euo pipefail

if [ -n "${CURSOR_CONFIG_ROOT:-}" ]; then
	ROOT="$CURSOR_CONFIG_ROOT"
else
	ROOT="${HOME}/.config/Cursor"
fi

STAMP=$(date +%Y%m%d-%H%M%S)
KEY='src.vs.platform.reactivestorage.browser.reactiveStorageServiceImpl.persistentStorage.applicationUser'

if ! command -v sqlite3 >/dev/null 2>&1; then
	echo "Hiányzik az sqlite3. Telepítsd (pl. apt install sqlite3)." >&2
	exit 1
fi

if [ ! -d "$ROOT" ]; then
	echo "Cursor config nem található: $ROOT" >&2
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
	echo "Egyetlen state.vscdb sem található itt: $ROOT" >&2
	exit 1
fi

echo "Kész. Backuppal: $ROOT/**/state.vscdb.bak.$STAMP"
echo "Indítsd újra a Cursort, és próbáld ki az MCP eszközöket."
