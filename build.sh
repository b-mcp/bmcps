#!/usr/bin/env bash
set -e

# -----------------------------------------------------------------------------
# Ensure build dependencies are installed (cmake, pkg-config, libwebsockets-dev).
# If any is missing, install via sudo apt so the build can run on a clean system.
# -----------------------------------------------------------------------------
REQUIRED_PACKAGES=""
command -v cmake >/dev/null 2>&1 || REQUIRED_PACKAGES="${REQUIRED_PACKAGES} cmake"
command -v pkg-config >/dev/null 2>&1 || REQUIRED_PACKAGES="${REQUIRED_PACKAGES} pkg-config"
if ! pkg-config --exists libwebsockets 2>/dev/null; then
	REQUIRED_PACKAGES="${REQUIRED_PACKAGES} libwebsockets-dev"
fi
if [ -n "${REQUIRED_PACKAGES}" ]; then
	echo "The following build dependencies are missing and will be installed with sudo apt-get:${REQUIRED_PACKAGES}"
	echo "Running: sudo apt-get update && sudo apt-get install -y${REQUIRED_PACKAGES}"
	sudo apt-get update && sudo apt-get install -y ${REQUIRED_PACKAGES}
fi

# Create build directory and build the project
mkdir -p build

# Build the project
(cd build && cmake .. && make -j"$(nproc)")

# -----------------------------------------------------------------------------
# If Cursor MCP config exists under $HOME/.cursor/mcp.json, add or update the
# "Browser MCP tool" entry to point at the built binary. If the file or folder
# is missing, we do not fail; we only print that the config was not found and
# echo the full expected JSON so the user can add it manually.
# -----------------------------------------------------------------------------
SCRIPT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BMCPS_BIN="${SCRIPT_ROOT}/build/bmcps"
MCP_JSON="${HOME}/.cursor/mcp.json"

if [ ! -f "$BMCPS_BIN" ]; then
	echo "Build binary not found at $BMCPS_BIN" >&2
	exit 1
fi

if [ ! -f "$MCP_JSON" ]; then
	echo "Build finished. Cursor mcp json not found: $MCP_JSON"
	echo "To use BMCPS in Cursor, add the following to your MCP config (e.g. create $MCP_JSON):"
	echo "{\"mcpServers\":{\"Browser MCP tool\":{\"command\":\"${BMCPS_BIN}\"}}}"
	exit 0
fi

echo "Build finished. Updating Cursor mcpServers block with Browser MCP tool."
export BMCPS_BIN
export MCP_JSON
python3 -c '
import json
import os

path = os.environ.get("BMCPS_BIN", "")
mcp_path = os.environ.get("MCP_JSON", "")
if not path or not mcp_path:
    raise SystemExit("BMCPS_BIN or MCP_JSON not set")

try:
    with open(mcp_path, "r") as f:
        data = f.read().strip() or "{}"
        config = json.loads(data)
except (FileNotFoundError, json.JSONDecodeError):
    config = {}

if "mcpServers" not in config or not isinstance(config["mcpServers"], dict):
    config["mcpServers"] = {}
config["mcpServers"]["Browser MCP tool"] = {"command": path}
with open(mcp_path, "w") as f:
    json.dump(config, f, indent=2)
'
echo "Cursor mcpServers block updated."
