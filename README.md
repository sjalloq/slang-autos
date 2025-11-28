# slang-autos

A C++ implementation of SystemVerilog AUTO macro expansion built on the [slang](https://github.com/MikePopoloski/slang) compiler.

## Features

- **AUTOINST** - Automatic module port instantiation
- **AUTOWIRE** - Automatic wire declaration generation
- **AUTOPORTS** - Automatic ANSI style port declarations
- **AUTO_TEMPLATE** - Template-based port mapping with regex support née Lisp

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

The tool uses `slang` to resolve all modules and evaluate all macros/params.  This means,
unlike `verilog-mode`, ports that are ifdef'd out don't appear in the instantiation.

```bash
# Basic usage
slang-autos design.sv

# With library paths
slang-autos design.sv -y lib/ +libext+.v+.sv +incdir+include/

# File list
slang-autos -f design.f

# Dry run (show changes without modifying)
slang-autos design.sv --dry-run

# Show diff
slang-autos design.sv --diff

# Strict mode (error on missing modules)
slang-autos design.sv --strict
```

## Template Syntax

For better or worse, the ``AUTO_TEMPLATE`` name is taken from ``verilog-mode`` where the
inspiration for this project came from.  This might change.

Rather than using Lisp, ``slang-autos`` uses RE2 regex patterns with the ``=>`` operator
implying a mapping between port name and net name.

The following snippet shows an example of the syntax:

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


**Note:** I've followed the `verilog-mode` default of capturing a simple `(\d+)` for the
instance pattern if none is given.  The `@` operator is used as an alias for `%1`.  See
below.

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

### Further Regex Examples

```verilog
/* module_name AUTO_TEMPLATE
   ignored_signals_.* => port.input ? '0 : _
   axim_(.+) => m_axi_$1
   sig_([0-9]+)_(.*) => top_sig_$2[$1]
*/
```

## License

MIT License
