# slang-autos

A C++ implementation of SystemVerilog AUTO macro expansion built on the [slang](https://github.com/MikePopoloski/slang) compiler.

## Why slang-autos?

`slang-autos` is a modern take on the venerable [`verilog-mode`](https://veripool.org/verilog-mode/), an Emacs major mode that offers syntax highlighting, indentation and macro expansion.  This project aims to replicate the macro expansion functionality but with the following benefits:

- Full SystemVerilog parsing (macros, params, ifdefs evaluated correctly)
- Modern C++ (no Emacs/Lisp dependency)
- A templating engine built on top of `std::regex`
- Standalone CLI tool as well as an LSP

## Building

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.20+

### Build Steps

```bash
# Clone with submodules
git clone --recursive https://github.com/sjalloq/slang-autos.git
cd slang-autos

# Build
cmake -B build
cmake --build build -j `nproc`

# Run tests
cd build && ctest
```

## Usage

Pass the files you want to expand as positional arguments to `slang-autos`.  In order to avoid `slang` elaborating the full design, each positional argument that is a Verilog/SystemVerilog file is treated as the top level of the design and elaborated independently.

```bash
# Basic usage
slang-autos design.sv

# With library paths
slang-autos design.sv -y lib/ +libext+.v+.sv +incdir+include/

# File list
slang-autos design.sv -f design.f

# Dry run (show change summary without modifying)
slang-autos design.sv --dry-run

# Show diff
slang-autos design.sv --diff

# Strict mode (error on missing modules)
slang-autos design.sv --strict
```

## Template Syntax

The templating system uses standard regex syntax instead of Emacs Lisp's double-escaped patterns.  This hopefully makes it easier writing rename rules. 

The `AUTO_TEMPLATE` and `AUTOINST` names are taken from `verilog-mode`, with `AUTOLOGIC` replacing the separate `AUTOWIRE`/`AUTOREG` macros, and `AUTOPORTS` added for ANSI-style port declarations. The format should be familiar to anyone who has used `verilog-mode` in the past. The main difference is that each rename rule is specified using the `=>` operator. The left hand side defines the source port name and the right hand side is the regex expansion.

The following snippet gives an example of the template syntax:

```verilog
/* module_name AUTO_TEMPLATE ["instance_pattern"]
   port_pattern => signal_expression
*/

// Example:
/* fifo AUTO_TEMPLATE "u_fifo_(\d+)"
   din => fifo_%1_din
   dout => fifo_%1_dout
*/
fifo u_fifo_0 (
    .clk(clk),
    /*AUTOINST*/
);

fifo u_fifo_1 (
   .clk(clk),
   /*AUTOINST*/
);
```

gets expanded to:

```verilog
/* fifo AUTO_TEMPLATE "u_fifo_(\d+)"
   din => fifo_%1_din
   dout => fifo_%1_dout
*/
fifo u_fifo_0 (
    .clk(clk),
    /*AUTOINST*/
    .din  (fifo_0_din),
    .dout (fifo_0_dout)
);

fifo u_fifo_1 (
   .clk(clk),
   /*AUTOINST*/
   .din  (fifo_1_din),
   .dout (fifo_1_dout)
);
```

See the main [documentation](https://sjalloq.github.io/slang-autos/) for many more examples.

### Substitution Variables

- `$1`, `$2`, ... - Capture groups from port pattern
- `%1`, `%2`, ... - Capture groups from instance pattern
- `@` - Alias to `%1` for `verilog-mode` compatibility
- `port.name`, `port.width`, `port.range` - Port properties
- `port.input`, `port.output`, `port.inout` - Port booleans
- `inst.name` - Instance name

### Special Values

- `_` - Unconnected port
- `'0`, `'1`, `'z` - Constant values

### Arithmetic Functions

- `mul(a,b)` - Multiplication
- `div(a,b)` - Divide
- `add(a,b)` - Addition
- `sub(a,b)` - Subtraction
- `mod(a,b)` - Modulus

## Configuration

`slang-autos` can be configured through three layers, with later layers overriding earlier ones:

1. **Config file** (`.slang-autos.toml` or `.slang-autos`) — project-level defaults
2. **Inline comments** (`// slang-autos-KEY: VALUE`) — per-file overrides
3. **CLI flags** — highest priority

Library paths (`libdirs`, `libext`, `incdirs`) are **additive** across all layers. All other options use **override** semantics.

### Config File

The config file is searched for in order:
1. Current working directory
2. Git repository root

Both `.slang-autos.toml` and `.slang-autos` are accepted (TOML format either way). If both exist in the same directory, `.slang-autos.toml` takes priority.

```toml
[library]
libdir = ["rtl/", "lib/"]            # -y equivalents
libext = [".v", ".sv"]               # +libext+ equivalents
incdir = ["include/"]                # +incdir+ equivalents

[formatting]
indent    = 2                        # Number of spaces (or "tab")
alignment = true                     # Align port names in columns
grouping  = "direction"              # Port ordering: "alphabetical", "direction", "declaration"

# Direction comments on AUTOINST port connections
# Can be: true (defaults: <- -> <->), false, or custom tokens
direction_comments = true
# direction_comments = "IN OUT INOUT"  # custom tokens

[behavior]
strictness      = "lenient"          # "strict" errors on missing modules
verbosity       = 1                  # 0=quiet, 1=normal, 2=verbose
single_unit     = true               # Treat all files as single compilation unit
resolved_ranges = false              # Use resolved integer widths instead of original syntax
```

### Inline Config

Per-file overrides using single-line comments. Environment variables (`$VAR` or `${VAR}`) are expanded in values.

```verilog
// slang-autos-libdir: rtl/ lib/
// slang-autos-libext: .v .sv
// slang-autos-incdir: include/
// slang-autos-indent: 4
// slang-autos-alignment: true
// slang-autos-grouping: direction
// slang-autos-strictness: strict
// slang-autos-verbosity: 2
// slang-autos-single-unit: false
// slang-autos-resolved-ranges: true
// slang-autos-direction-comments: true
// slang-autos-direction-comments: IN OUT INOUT
```

The `grouping` option controls port ordering in `AUTOPORTS` expansion:
- `alphabetical` / `alpha` — sort ports alphabetically
- `direction` / `bydirection` — group by input/output/inout (default)
- `declaration` / `bydeclaration` — preserve declaration order from the module

## License

MIT License
