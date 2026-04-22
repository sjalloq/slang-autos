# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

slang-autos is a C++ implementation of SystemVerilog AUTO macro expansion (similar to Emacs verilog-mode) built on the [slang](https://github.com/MikePopoloski/slang) compiler. It provides a standalone CLI tool and an LSP server for IDE integration.

## Build Commands

```bash
# Configure and build (from repo root)
cmake -B build
cmake --build build -j $(nproc)

# Run all tests
cd build && ctest

# Run a specific test
cd build && ctest -R <test_name>

# Build specific targets
cmake --build build --target slang-autos       # CLI tool
cmake --build build --target slang-autos-lsp   # LSP server
cmake --build build --target slang-autos-tests # Test suite
```

## Architecture

### Core Components (src/, include/slang-autos/)

- **Parser** (`Parser.h/cpp`): Parses AUTO comments from SystemVerilog source using slang's trivia API. Handles `AUTO_TEMPLATE`, `/*AUTOINST*/`, `/*AUTOLOGIC*/`, `/*AUTOPORTS*/`. Legacy Verilog markers (AUTOWIRE, AUTOREG, AUTOINPUT, AUTOOUTPUT, AUTOINOUT) are not supported — use AUTOLOGIC / AUTOPORTS instead (see `Constants.h`).

- **TemplateMatcher** (`TemplateMatcher.h/cpp`): Matches ports against template rules and performs variable substitution. Uses `std::regex` (ECMAScript). Supports port captures (`$1`, `$2`), instance captures (`%1`, `%2`, `@`), built-in variables (`port.name`, `port.width`, `inst.name`), and math functions (`add`, `sub`, `mul`, `div`, `mod`).

- **AutosAnalyzer** (`AutosAnalyzer.h/cpp`): The expansion engine. Walks the slang AST to collect AUTOINST / AUTOLOGIC / AUTOPORTS sites and generates `Replacement` structs. Uses `SignalAggregator` to track net usage across all instances.

- **DotStarExpander** (`DotStarExpander.h/cpp`): Expands SystemVerilog `.*` port wildcards into explicit `.port(port)` connections. Used by the separate `slang-expand` binary.

- **Writer** (`Writer.h/cpp`): Handles text replacement operations. Manages `Replacement` structs that track offset, length, and replacement text.

- **Tool** (`Tool.h/cpp`): Main orchestrator (`AutosTool` class). Coordinates slang compilation, parsing, template matching, expansion, and file writing. Uses slang's Driver for command-line argument parsing.

### Binaries

- **slang-autos** (`src/main.cpp`): CLI for AUTO macro expansion.
- **slang-expand** (`src/main_expand.cpp`): CLI for `.*` dot-star port wildcard expansion.
- **slang-autos-lsp** (`extensions/lsp/main.cpp`): LSP server.

### LSP Extension (extensions/lsp/)

- **AutosServer**: LSP protocol handler. Exposes `slang-autos.expandAutos` and `slang-autos.deleteAutos` via the standard `workspace/executeCommand` mechanism (advertised in `executeCommandProvider`).

### VSCode Extension (extensions/vscode/)

TypeScript extension that connects to the LSP server. Key UI commands (defined in `extensions/vscode/package.json`):
- `slang-autos.expand` (Ctrl+Shift+A / Cmd+Shift+A): Expand AUTOs
- `slang-autos.delete`: Delete AUTO-generated content
- `slang-autos.restartServer`: Restart the language server

The UI commands delegate to the server-side `slang-autos.expandAutos` / `slang-autos.deleteAutos` commands via `workspace/executeCommand`.

## Key Dependencies

- **slang**: SystemVerilog parser/compiler (git submodule in `external/slang`)
- **Catch2**: Test framework (fetched via CMake)
- **reflect-cpp**: JSON serialization for LSP (fetched via CMake)

## Testing

Tests use Catch2 and are organized by component:
- `test_parser.cpp`: AUTO comment parsing
- `test_template_matcher.cpp`: Template rule matching and substitution
- `test_expander.cpp`: Expansion logic
- `test_writer.cpp`: Text replacement
- `test_tool.cpp`: End-to-end tool tests
- `test_integration.cpp`: Full workflow tests

## Development Workflow

### Bug Fixes and New Features

Follow this process for any non-trivial changes:

#### 1. Understand Before Implementing
- **Trace the existing code flow** before proposing changes
- Identify existing mechanisms that might already solve the problem
- Check how similar features are implemented elsewhere in the codebase
- Key data flows to understand:
  - Signal collection: `SignalAggregator::addFromInstance()` tracks all signals
  - Signal filtering: `existing_decls` (user declarations), `existing_ports` (manual ports)
  - Output generation: `generateAutoportsReplacement()`, `generateAutologicDecls()`

#### 2. Write a Failing Test First
- **Always create a test case before implementing a fix**
- Create a fixture in `tests/fixtures/<descriptive_name>/`
- Add the test to `tests/test_integration.cpp`
- Run the test to confirm it fails (reproduces the bug)
- Use simple, generic signal names (e.g., `sig_a`, `data_out`) not domain-specific names

#### 3. Plan the Solution
- Document the bug(s) clearly with examples
- Identify the root cause (not just symptoms)
- Consider where the fix belongs:
  - **Data collection** (aggregator): Should capture all data without filtering
  - **Output generation**: Apply filtering/validation when producing output
- Prefer leveraging existing infrastructure over adding new mechanisms

#### 4. Implement Incrementally
- Make the smallest possible change that fixes the issue
- Avoid sweeping changes across multiple files
- Separation of concerns: don't mix data collection with filtering logic

#### 5. Verify Thoroughly
- **Force full rebuilds** after changes: `touch src/<file>.cpp && cmake --build build`
- Run the specific test: `./tests/slang-autos-tests "Test Name"`
- Run all tests: `cd build && ctest`
- Test with CLI to verify end-to-end: `./slang-autos --dry-run --diff <file>`

### Common Pitfalls

| Pitfall | Solution |
|---------|----------|
| Stale binaries giving inconsistent results | Force rebuild with `touch` on modified files |
| Filtering in data collection breaks other features | Filter at output generation time instead |
| Not understanding existing mechanisms | Trace code flow before implementing |
| Guessing at fixes without tests | Write failing test first, then fix |
| Large changes that are hard to debug | Small, incremental changes with tests |

### Key Design Principles

1. **SignalAggregator collects everything** - It tracks all signals from all instances without filtering. Filtering happens later.

2. **existing_decls vs existing_ports**:
   - `existing_decls`: User-declared variables in module body (excludes AUTOLOGIC-generated)
   - `existing_ports`: Ports declared before `/*AUTOPORTS*/` marker

3. **Idempotency matters** - Running expansion twice should produce identical output. Test with the idempotency test cases.

4. **Use simple test fixtures** - Generic names like `sig_a`, `clk`, `data_out`. Avoid domain-specific names.

## Template Syntax

The templating system uses standard regex syntax with the `=>` operator for rename rules:

```verilog
/* module_name AUTO_TEMPLATE "instance_pattern"
   port_pattern => signal_expression
*/
```

Special values: `_` (unconnected), `'0`, `'1`, `'z` (constants)
