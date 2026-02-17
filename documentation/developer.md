# BMCPS – Developer documentation

## Project fundamentals

BMCPS = **Browser Model Context Protocol Server**. It is an MCP (Model Context Protocol) server that controls a browser; currently Chrome is supported. The stack is C++17, producing a single executable (`bmcps`) that speaks stdio JSON-RPC and is used by Cursor or other MCP clients.

Short architecture:

- **MCP transport** – stdio, JSON-RPC dispatch
- **Tool registry** – MCP tools are registered and invoked by name
- **Browser driver abstraction** – interface to control a browser (Chrome implementation)
- **Platform** – OS abstraction (process spawn, file I/O); Linux is implemented; Windows and macOS are placeholders

## Why C++

- Performance and a single binary with no external runtime (no Node/Python dependency).
- WebSocket (libwebsockets) and JSON (nlohmann/json) fit the needs of the browser communication layer.
- Future platforms (e.g. Windows, macOS) can be added with the same codebase.

## How to build

- **Build system:** CMake; use a separate build directory (e.g. `build/`).
- **Dependencies:** libwebsockets-dev, cmake, g++. On Ubuntu/Debian: `sudo apt-get install libwebsockets-dev cmake g++`. nlohmann/json is fetched automatically via CMake FetchContent.
- **Commands:**

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Binary: `build/bmcps`.

**Tests:**

```bash
./build/tests/bmcps_test
```

Optional smoke E2E (requires Chrome and a display):

```bash
cd build && cmake --build . --target bmcps_smoke_test && ./tests/bmcps_smoke_test
```

## How we develop

- **Source layout:** Application code lives under `source/`. Main entry is `main.cpp`; MCP layer in `mcp/`, protocol helpers in `protocol/`, browser driver in `browser/`, platform in `platform/`, tool implementations in `tool_handlers/`, utilities in `utils/`.
- **Adding a new tool:** Implement in `tool_handlers/tool_<name>.cpp`, register it in `tool_handlers.cpp`, and add the unit to CMakeLists.txt.
- **ABI and driver:** Shared types for the browser layer are in `browser_driver_abi.hpp`. The concrete browser driver lives under `browser/`.
- **Conventions:** No OOP/classes (project rule); prefer callback and error-first style. File names are kebab-case. Avoid abbreviations in names except the documented exceptions (e.g. req, res, err, i, j, k in the allowed contexts).

## Commit and PR standards

- Commit messages should be concise; use the repository’s usual language (e.g. English or Hungarian as per repo convention).
- Run `git pull` before committing to avoid unnecessary merge conflicts.
- Prefer submitting a **pull request** instead of pushing directly to the main branch when the project policy requires it.
- Before committing, verify that the change still builds and tests pass.

## Code style and conventions

- **Abbreviations:** Avoid them in identifiers; use full, meaningful names. Documented exceptions include: loop indices (i, j, k), middleware parameters (req, res), error parameters (err), and similar cases specified in project rules.
- **File size:** Keep files manageable; split when complexity or length grows too high (project rule: aim for a few hundred lines per file, avoid very long monoliths).
- **Error logging:** When logging errors, include: (1) a unique, human-readable message describing what went wrong and how to avoid it, (2) the error object or exception, (3) the triggering context (e.g. parameters) in a structured form (e.g. JSON) so the situation can be reproduced.
- For more detail, see the project’s `.cursor` rules and any other documented coding standards.

## Full tool list

Tools exposed to the MCP client (e.g. Cursor). Use the selectors and parameters below when integrating or testing.

| Tool | Description | Main parameters |
|------|-------------|-----------------|
| **open_browser** | Launch the browser and connect. | **disable_translate** (boolean, default true) – hide the “translate this page?” bar. |
| **close_browser** | Close the browser and disconnect (terminates the browser process). | — |
| **list_tabs** | List open browser tabs (target IDs, titles, URLs, types). | — |
| **new_tab** | Open a new tab; optional URL. The new tab becomes the current target for navigate and other actions. | **url** (optional). |
| **switch_tab** | Switch to a tab by 0-based index (use list_tabs for order). | **index** (integer). |
| **close_tab** | Close the current tab; attaches to another tab if one exists. | — |
| **navigate** | Navigate the current tab to a URL. | **url** (required). |
| **navigate_back** | Go back in the current tab’s history. | — |
| **navigate_forward** | Go forward in the current tab’s history. | — |
| **refresh** | Reload the current tab. | — |
| **get_navigation_history** | Get the current tab’s navigation history (URLs and current index). | — |
| **capture_screenshot** | Capture a screenshot of the currently displayed tab; returns image content for the model to inspect (e.g. buttons, layout). | — |
| **get_console_messages** | Get console messages (console.log, console.error, etc.) from the current tab. | **time_scope** (object: type `none` \| `last_duration` \| `range` \| `from_onwards` \| `until`; for last_duration use value+unit; for range use from_ms+to_ms; for from_onwards use from_ms; for until use to_ms). **count_scope**: max_entries (default 500), order (newest_first \| oldest_first). **level_scope**: type min_level with level, or only with levels array. Response starts with `[bmcps-console] returned=N total_matching=M truncated=true|false`, then time sync and log lines. UTF-8 sanitized; FIFO buffer to limit memory. |
| **list_interactive_elements** | List form fields and clickable elements (inputs, textareas, buttons, links, option and role=option for dropdowns). Returns selector, role, label, placeholder, type, visible text. For combobox/listbox, open the dropdown first then call again for options. Use returned selectors with fill_field and click_element. | — |
| **fill_field** | Fill an input or textarea by selector (from list_interactive_elements). | **selector**, **value**. Optional **clear_first** (default true). |
| **click_element** | Click an element by selector (e.g. from list_interactive_elements). | **selector**. |
| **click_at_coordinates** | Click at viewport coordinates (x, y in CSS pixels). Use for canvas or when no DOM selector is available. | **x**, **y**. |
| **scroll** | Scroll the page or a scrollable element. | **scroll_scope**: type `page` (delta_x, delta_y for window) or type `element` (selector + delta_x, delta_y for container). |
| **resize_browser** | Resize the browser window. | **preset** (vga, xga, hd, fullhd, 2k, 4k) or **width** and **height** in pixels. Default open size is 1024×768. |

## Project structure

```
source/
  mcp/              MCP stdio transport, JSON-RPC dispatch, tool registry
  protocol/         JSON-RPC helpers (nlohmann/json)
  browser/          Browser driver abstraction + implementation
  platform/         OS abstraction (process spawn, file I/O)
    linux/          Linux implementation (current)
    windows/        (placeholder)
    mac/            (placeholder)
  tool_handlers/    MCP tool implementations
  utils/            Utilities (e.g. UTF-8 sanitize, debug log)
  main.cpp          Application entry
tests/              Unit/parameter tests + optional smoke E2E
```
