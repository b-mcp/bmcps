# BMCPS – Browser Model Context Protocol Server

MCP server that lets Cursor (or any MCP client) control a browser. You can open and close the browser, manage tabs, navigate to URLs, capture screenshots, list and fill form fields, click elements, read console messages, resize the window, and scroll—all from the chat.

## Links

- **[Model Context Protocol (MCP)](https://modelcontextprotocol.io/)** – protocol for connecting AI assistants to tools and data.
- **[Cursor](https://cursor.com/)** – code editor that can use MCP servers; configure BMCPS as an MCP server to control a browser from Cursor chat.

## Supported platforms

Currently **Linux** (Chrome). For other platforms (Windows, macOS), see [Developer documentation](documentation/developer.md) to contribute or port.

## Build and requirements

**Linux (e.g. Ubuntu/Debian):**

- Install: `libwebsockets-dev`, `cmake`, `g++`  
  Example: `sudo apt-get install libwebsockets-dev cmake g++`
- nlohmann/json is fetched automatically by CMake (FetchContent).

**Build:**

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Binary: `build/bmcps`.

## How to use

1. Add BMCPS to your MCP configuration (e.g. `.cursor/mcp.json` or `~/.cursor/mcp.json`):

```json
{
  "mcpServers": {
    "bmcps": {
      "command": "/absolute/path/to/build/bmcps"
    }
  }
}
```

2. Set **command** to the **absolute path** of the `bmcps` binary (e.g. `/home/you/project/build/bmcps`). There is no system-wide install step; use the path of the binary you built.
3. Restart Cursor (or reload the window). The tools then appear in chat.

## What it can do (tool sample)

Open and close the browser, manage tabs, navigate to a URL, capture a screenshot, list and fill form fields, click buttons and links, read console messages, resize the window, scroll the page or a scrollable element, hover and double-click or right-click elements, drag and drop (by selector or coordinates for canvas), run JavaScript in the page, send keys and wait for elements or navigation, manage cookies and dialogs, upload files, work with frames and storage, read or set clipboard, inspect network requests, set geolocation or user agent, and check element visibility or bounding box.

For the full list of tools and parameters, see [Developer documentation](documentation/developer.md).

## Tests

Run unit tests:

```bash
./build/tests/bmcps_test
```

Optional smoke E2E test (requires Chrome and a display):

```bash
cd build && cmake --build . --target bmcps_smoke_test && ./tests/bmcps_smoke_test
```
