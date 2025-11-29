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

For better or worse, the `AUTO_TEMPLATE`, `AUTOWIRE` and `AUTOREG` names are taken from `verilog-mode` along with the addition of `AUTOPORTS` for ANSI-style port declarations.  The format should be familiar to anyone who has used `verilog-mode` in the past.  The main difference is that each rename rule is specified using the `=>` operator.  The left hands side defines the source port name and the right hand side is the regex expansion.

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

## License

MIT License
