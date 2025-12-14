# slang-autos TODO

## Features to Implement

### AUTOINST: Automatic bit-slicing for width mismatches

**Problem:**
When multiple instances connect same-named ports with different widths to the same signal, AUTOLOGIC correctly declares the max width, but AUTOINST generates simple name connections that cause width mismatch errors.

**Example scenario:**
```systemverilog
// sub1 has: input [7:0] data
// sub2 has: input [15:0] data
// sub3 has: input [3:0] data
```

**Current (broken) output:**
```systemverilog
/*AUTOLOGIC*/
logic [15:0] data;  // Correct: max width

sub1 u_sub1 (.data(data), ...);  // ERROR: 16-bit to 8-bit
sub2 u_sub2 (.data(data), ...);  // OK: 16-bit match
sub3 u_sub3 (.data(data), ...);  // ERROR: 16-bit to 4-bit
```

**Expected output:**
```systemverilog
/*AUTOLOGIC*/
logic [15:0] data;

sub1 u_sub1 (.data(data[7:0]), ...);   // Explicit slice
sub2 u_sub2 (.data(data[15:0]), ...);  // Full width (or just data)
sub3 u_sub3 (.data(data[3:0]), ...);   // Explicit slice
```

**Implementation approach:**
1. In `AutosAnalyzer::generatePortConnections()` or `buildConnections()`
2. Look up the aggregated signal width from `SignalAggregator` for this signal name
3. Compare to the port width for this specific instance's port
4. If widths differ, emit `.port(signal[port_width-1:0])`
5. If widths match, emit `.port(signal)`

**Note:** verilog-mode supported this via template syntax `.data(my_data[])` where empty brackets meant "insert port width here". We can do this automatically since we have full width information from slang's AST.

---

## QA Review Progress

Based on review of `slang-autos-qa-gaps.md` (2025-12-15)

### Completed

| Item | Description | Status |
|------|-------------|--------|
| 1.1.3 | Parameterized port widths - preserve original syntax | Fixed |
| 3.2 | Multi-dimensional packed arrays | Fixed |
| - | Token iteration rewrite in CompilationUtils.cpp | Fixed |
| - | `resolved_ranges` option added | Fixed |
| - | Integration tests for macro preservation | Added |

### In Progress / To Investigate

| Section | Item | Priority | Notes |
|---------|------|----------|-------|
| 1.1.1 | Module with no ports | Low | Degenerate case, likely just warning |
| 1.1.2 | Module with only inout ports | Low | Quick sanity test |
| 1.1.4 | Nested instantiation in generate | Medium | Real-world pattern, needs testing |
| 1.2.1 | Conflicting widths across instances | **High** | See "Automatic bit-slicing" above |
| 1.2.2 | Signal name = SV keyword | Low | Slang catches this, verify we surface error |
| 1.2.3 | User comments in AUTOLOGIC block | None | Documented behavior, comments get replaced |

### Not Yet Reviewed

| Section | Items |
|---------|-------|
| 1.3 | AUTOPORTS edge cases (interface ports, default values) |
| 1.4 | Template edge cases (precedence, escaping, unicode, empty pattern) |
| 1.5 | Error handling (syntax errors, circular instantiation, long lines) |
| 1.6 | Configuration edge cases |
| 1.7 | Line endings and whitespace |
| 2.x | Potential bugs (manual port boundary, template lookup, etc.) |
| 3.x | Slang API edge cases (MultiPortSymbol, LSP line counting) |
| 4.x | DRY refactoring opportunities |
| 5.x | Feature suggestions (--check mode, --diff mode) |
| 6.x | Documentation gaps |

### Dismissed

| Item | Reason |
|------|--------|
| 1.1.5 | Instance with parameter overrides - not a bug; if user hardcodes override but wants syntax preservation, that's user error (produces compile error on undefined param) |
| 1.2.2 | SV keyword as signal name - invalid SV, slang rejects it upstream |

---

## Next Steps

1. Implement automatic bit-slicing for width mismatches (HIGH)
2. Continue QA review from section 1.3 onwards
3. Add test cases for medium-priority items
4. Update documentation
