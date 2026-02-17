# BMCPS – Browser Model Context Protocol Server

MCP server that lets Cursor (or any MCP client) control a browser via stdio JSON-RPC.
Currently supports **Chrome** through the **Chrome DevTools Protocol (CDP)**.

## Dependencies

| Dependency | Purpose | Install (Ubuntu 22.04) |
|---|---|---|
| libwebsockets-dev | CDP WebSocket communication | `sudo apt-get install libwebsockets-dev` |
| nlohmann/json | JSON parsing and serialization | Fetched automatically by CMake (FetchContent) |
| cmake, g++ | Build tools | `sudo apt-get install cmake g++` |

## Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The binary is `build/bmcps`.

## Run tests

```bash
./build/tests/bmcps_test
```

Optional smoke E2E test (requires Chrome and a display):

```bash
cd build && cmake --build . --target bmcps_smoke_test && ./tests/bmcps_smoke_test
```

## Use with Cursor

**Permanent enable:** The project includes a `.cursor/mcp.json`, so the bmcps MCP server is loaded automatically when you open this project. For a global setup, use `~/.cursor/mcp.json` (Cursor loads it on startup).

Add to your MCP configuration (e.g. `.cursor/mcp.json` or `~/.cursor/mcp.json`):

```json
{
  "mcpServers": {
    "bmcps": {
      "command": "/absolute/path/to/build/bmcps"
    }
  }
}
```

Restart Cursor (or reload the window). The following tools become available in chat:

- **open_browser** – launch Chrome and connect via CDP; optional **disable_translate** (boolean, default false) to hide the „Would you like to translate?“ bar
- **close_browser** – close the browser and disconnect (terminates the Chrome process)
- **list_tabs** – list open browser tabs
- **new_tab** – open a new tab (optional URL); it becomes the current target for navigate
- **switch_tab** – switch to tab by 0-based index (use list_tabs to see order)
- **close_tab** – close the current tab; attaches to another tab if one exists
- **navigate** – navigate the current tab to a URL
- **navigate_back** – go back in the current tab’s history
- **navigate_forward** – go forward in the current tab’s history
- **refresh** – reload the current tab
- **get_navigation_history** – get the current tab’s navigation history (URLs and current index; via CDP, not available from in-page JS)
- **capture_screenshot** – capture a screenshot of the currently displayed tab; returns image content so the model can verify the visible UI (e.g. buttons, layout)

## Project structure

```
source/
  mcp/              MCP stdio transport, JSON-RPC dispatch, tool registry
  protocol/         JSON-RPC helpers (nlohmann/json)
  browser/          Browser driver abstraction + CDP implementation
    cdp/            Chrome DevTools Protocol driver
  platform/         OS abstraction (process spawn, file I/O)
    linux/          Linux implementation (current)
    windows/        (placeholder)
    mac/            (placeholder)
  tool_handlers/    MCP tool implementations
tests/              Parameter tests + optional smoke E2E
```
