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

Add to your MCP configuration (e.g. `.cursor/mcp.json`):

```json
{
  "mcpServers": {
    "bmcps": {
      "command": "/absolute/path/to/build/bmcps"
    }
  }
}
```

Restart Cursor. The following tools become available in chat:

- **open_browser** – launch Chrome and connect via CDP
- **list_tabs** – list open browser tabs
- **navigate** – navigate the current tab to a URL

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
