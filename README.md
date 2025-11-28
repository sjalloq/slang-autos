# slang-autos

A C++ implementation of SystemVerilog AUTO macro expansion built on the [slang](https://github.com/MikePopoloski/slang) compiler.

## Features

- **AUTOINST** - Automatic module port instantiation
- **AUTOWIRE** - Automatic wire declaration generation
- **AUTO_TEMPLATE** - Template-based port mapping with regex support

## Building

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.20+

### Build Steps

```bash
# Clone with submodules
git clone --recursive https://github.com/your/slang-autos.git
cd slang-autos

# Or if already cloned:
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake ..
cmake --build .

# Run tests
ctest
```

## Usage

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

```verilog
/* module_name AUTO_TEMPLATE "instance_pattern"
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
```

### Substitution Variables

- `$1`, `$2`, ... - Capture groups from port pattern
- `%1`, `%2`, ... - Capture groups from instance pattern
- `port.name`, `port.width`, `port.range` - Port properties
- `inst.name` - Instance name

### Special Values

- `_` - Unconnected port
- `'0`, `'1`, `'z` - Constant values

## License

MIT License
