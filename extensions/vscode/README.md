# slang-autos VSCode Extension

Expands verilog-mode style AUTO macros (AUTOINST, AUTOWIRE, etc.) in SystemVerilog files.

## Requirements

- The `slang-autos-lsp` binary must be built and either:
  - Added to your PATH, or
  - Configured via `slang-autos.serverPath` setting

## Building the LSP Server

From the slang-autos root directory:

```bash
cmake -B build
cmake --build build --target slang-autos-lsp
```

The binary will be at `build/extensions/lsp/slang-autos-lsp`.

## Usage

1. Open a SystemVerilog file containing AUTO macros
2. Press `Ctrl+Shift+A` (or `Cmd+Shift+A` on Mac) to expand AUTOs
3. Or use the command palette: "slang-autos: Expand AUTOs"

## Development

```bash
cd extensions/vscode
npm install
npm run compile
```

To test in VSCode, press F5 to launch the Extension Development Host.

## Configuration

- `slang-autos.serverPath`: Path to slang-autos-lsp executable (if not in PATH)
