=============
AUTO_TEMPLATE
=============

This document describes the AUTO_TEMPLATE system for customizing how ports are
connected during AUTOINST expansion. Templates provide powerful pattern matching
and signal transformation capabilities.

.. contents:: Table of Contents
   :local:
   :depth: 2


Introduction
============

When slang-autos expands ``/*AUTOINST*/``, it connects each port to a signal
with the same name by default. AUTO_TEMPLATE lets you override this behavior
to:

- Rename signals based on instance names (e.g., ``data`` → ``fifo_0_data``)
- Transform port names (e.g., ``axi_wdata`` → ``m_axi_wdata``)
- Tie off unused ports to constants
- Leave specific ports unconnected
- Compute signal names using math expressions

Templates are especially useful when instantiating multiple copies of a module
where each instance needs different signal names.


Template Syntax
===============

Basic Structure
---------------

A template is defined in a block comment immediately before one or more
module instantiations:

.. code-block:: verilog

   /* module_name AUTO_TEMPLATE ["instance_pattern"]
      port_pattern => signal_expression
      port_pattern => signal_expression
   */
   module_name instance_name (/*AUTOINST*/);

Components:

- **module_name**: The module type this template applies to (required)
- **instance_pattern**: Optional regex to match instance names and extract captures
- **port_pattern**: Port name or regex pattern to match
- **signal_expression**: The signal name or expression to connect

Rule Ordering
-------------

Rules are evaluated in order, and **the first matching rule wins**. This means
more specific patterns should come before general catch-all patterns:

.. code-block:: verilog

   /* fifo AUTO_TEMPLATE
      clk      => sys_clk
      rst_n    => sys_rst_n
      data_.*  => fifo_$0
      .*       => $0
   */

In this example, ``clk`` and ``rst_n`` get specific mappings, ports starting
with ``data_`` get prefixed, and everything else passes through unchanged.


Instance Pattern Matching
=========================

The instance pattern is an optional regex that matches against instance names
and extracts capture groups for use in signal expressions.

Default Behavior
----------------

When no instance pattern is specified, slang-autos automatically extracts the
first number found in the instance name:

.. code-block:: verilog

   /* fifo AUTO_TEMPLATE
      din => fifo_@_din
   */
   fifo u_fifo_0 (/*AUTOINST*/);  // @ = "0", connects to fifo_0_din
   fifo u_fifo_1 (/*AUTOINST*/);  // @ = "1", connects to fifo_1_din

This works with numbers anywhere in the name:

- ``u_fifo_0`` → extracts ``0``
- ``fifo3_inst`` → extracts ``3``
- ``ms2m_adapter`` → extracts ``2``

Custom Instance Patterns
------------------------

For more control, specify a regex pattern in quotes:

.. code-block:: verilog

   /* fifo AUTO_TEMPLATE "u_(.+)_fifo_(\d+)"
      din => %1_data_in[%2]
   */
   fifo u_tx_fifo_0 (/*AUTOINST*/);  // %1="tx", %2="0" → tx_data_in[0]
   fifo u_rx_fifo_3 (/*AUTOINST*/);  // %1="rx", %2="3" → rx_data_in[3]

Instance Capture Variables
--------------------------

- ``%1``, ``%2``, ``%3``, ... - Capture groups from instance pattern
- ``%{1}``, ``%{2}``, ... - Brace variant (use when followed by a digit)
- ``%0`` or ``%{0}`` - Full instance name
- ``@`` - Alias for ``%1`` (verilog-mode compatibility)

The brace variant is necessary when the variable is followed by a digit:

.. code-block:: verilog

   /* mem AUTO_TEMPLATE "bank_(\d+)"
      addr => bank%{1}0_addr
   */
   mem bank_2 (/*AUTOINST*/);  // Connects to bank20_addr (not bank_addr with "20")


Port Pattern Matching
=====================

Port patterns can be literal names or regex patterns that match against port
names and extract capture groups.

Literal Matching
----------------

Simple port names are matched exactly:

.. code-block:: verilog

   /* module AUTO_TEMPLATE
      clk   => sys_clk
      rst_n => sys_rst_n
   */

Regex Patterns
--------------

Use regex for flexible matching:

.. code-block:: verilog

   /* axi_slave AUTO_TEMPLATE
      axi_(.*)      => s_axi_$1
      data_([io])n  => port_$1
   */

Common patterns:

- ``.*`` - Match any port (catch-all)
- ``data_.*`` - Match ports starting with ``data_``
- ``(.*)_valid`` - Match ports ending with ``_valid``, capture prefix
- ``([^_]+)_(.+)`` - Split on first underscore

Port Capture Variables
----------------------

- ``$1``, ``$2``, ``$3``, ... - Capture groups from port pattern
- ``${1}``, ``${2}``, ... - Brace variant
- ``$0`` or ``${0}`` - Full port name (equivalent to ``port.name``)


Substitution Variables
======================

Signal expressions can include various substitution variables that are replaced
during expansion.

Instance Captures
-----------------

From the instance pattern regex:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Variable
     - Description
   * - ``%1``, ``%2``, ...
     - Capture groups from instance pattern
   * - ``%{1}``, ``%{2}``, ...
     - Brace variant (use when followed by digit)
   * - ``%0``, ``%{0}``
     - Full instance name
   * - ``@``
     - Alias for ``%1`` (verilog-mode compatible)

Port Captures
-------------

From the port pattern regex:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Variable
     - Description
   * - ``$1``, ``$2``, ...
     - Capture groups from port pattern
   * - ``${1}``, ``${2}``, ...
     - Brace variant
   * - ``$0``, ``${0}``
     - Full port name

Port Properties
---------------

Access port metadata:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Variable
     - Description
   * - ``port.name``
     - Port name (same as ``$0``)
   * - ``port.width``
     - Bit width as integer (e.g., ``8``)
   * - ``port.range``
     - Range string (e.g., ``[7:0]``)
   * - ``port.direction``
     - Direction string: ``"input"``, ``"output"``, or ``"inout"``
   * - ``port.input``
     - ``1`` if input, ``0`` otherwise
   * - ``port.output``
     - ``1`` if output, ``0`` otherwise
   * - ``port.inout``
     - ``1`` if inout, ``0`` otherwise

Instance Properties
-------------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Variable
     - Description
   * - ``inst.name``
     - Full instance name


Special Values
==============

These special values have specific meanings in signal expressions:

Unconnected Ports
-----------------

Use ``_`` (underscore) to leave a port unconnected:

.. code-block:: verilog

   /* debug_module AUTO_TEMPLATE
      debug_.*  => _
   */

This generates an empty connection: ``.debug_port()``

Constant Values
---------------

Tie ports to constant values:

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Value
     - Description
   * - ``'0``
     - Logic zero (unsized)
   * - ``'1``
     - Logic one (unsized)
   * - ``'z``
     - High impedance / tri-state

Example:

.. code-block:: verilog

   /* unused_module AUTO_TEMPLATE
      enable => '1
      data   => '0
   */

.. warning::

   Assigning constants to **output** ports generates a warning, as this is
   usually unintentional. Use ternary expressions to handle bidirectional
   cases.


Ternary Expressions
===================

Ternary expressions allow conditional signal assignment based on port properties.

Syntax
------

.. code-block:: text

   condition ? true_value : false_value

The condition must evaluate to ``"0"`` or ``"1"`` after variable substitution.
This is typically used with the port direction booleans.

Direction-Based Assignment
--------------------------

Connect different signals based on port direction:

.. code-block:: verilog

   /* bidir_module AUTO_TEMPLATE
      data => port.input ? data_in : data_out
   */

Tie-Off Patterns
----------------

A common pattern is to tie inputs to a constant while leaving outputs unconnected:

.. code-block:: verilog

   /* unused_module AUTO_TEMPLATE
      .* => port.input ? '0 : _
   */

Or vice versa - only connect outputs:

.. code-block:: verilog

   /* monitor AUTO_TEMPLATE
      .* => port.output ? mon_$0 : _
   */

Nested Ternaries
----------------

For more complex logic, ternaries can be nested:

.. code-block:: verilog

   /* complex AUTO_TEMPLATE
      .* => port.input ? input_bus : port.output ? output_bus : bidir_bus
   */


Math Functions
==============

Math functions allow computing signal names or indices from numeric values.

Available Functions
-------------------

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Function
     - Description
   * - ``add(a, b)``
     - Addition: a + b
   * - ``sub(a, b)``
     - Subtraction: a - b
   * - ``mul(a, b)``
     - Multiplication: a * b
   * - ``div(a, b)``
     - Integer division: a / b
   * - ``mod(a, b)``
     - Modulo: a % b

Arguments must be integers after variable substitution. Negative results are
supported.

.. note::

   Division or modulo by zero returns ``0`` and generates a warning.

Basic Usage
-----------

Offset instance numbers:

.. code-block:: verilog

   /* stage AUTO_TEMPLATE "stage_(\d+)"
      prev_data => stage_sub(@, 1)_data
      next_data => stage_add(@, 1)_data
   */
   stage stage_5 (/*AUTOINST*/);
   // prev_data → stage_4_data
   // next_data → stage_6_data

Nested Functions
----------------

Functions can be nested for complex calculations:

.. code-block:: verilog

   /* ring AUTO_TEMPLATE "node_(\d+)"
      next => node_mod(add(@, 1), 4)_port
   */
   ring node_0 (/*AUTOINST*/);  // next → node_1_port
   ring node_3 (/*AUTOINST*/);  // next → node_0_port (wraps around)

Array Indexing
--------------

Compute array indices from instance numbers:

.. code-block:: verilog

   /* slice AUTO_TEMPLATE "slice_(\d+)"
      data => bus_data[add(mul(@, 8), 7):mul(@, 8)]
   */
   slice slice_0 (/*AUTOINST*/);  // data → bus_data[7:0]
   slice slice_1 (/*AUTOINST*/);  // data → bus_data[15:8]
   slice slice_2 (/*AUTOINST*/);  // data → bus_data[23:16]


Practical Examples
==================

Basic Instance Numbering
------------------------

Multiple FIFOs with numbered signals:

.. code-block:: verilog

   /* fifo AUTO_TEMPLATE "u_fifo_(\d+)"
      wr_data  => fifo_@_wr_data
      rd_data  => fifo_@_rd_data
      wr_en    => fifo_@_push
      rd_en    => fifo_@_pop
      full     => fifo_@_full
      empty    => fifo_@_empty
   */
   fifo u_fifo_0 (/*AUTOINST*/);
   fifo u_fifo_1 (/*AUTOINST*/);
   fifo u_fifo_2 (/*AUTOINST*/);

Port Prefix Transformation
--------------------------

Add or change signal prefixes:

.. code-block:: verilog

   /* axi_master AUTO_TEMPLATE
      m_axi_(.*)  => axi_$1
      clk         => axi_clk
      rst_n       => axi_rst_n
   */

AXI Interface Remapping
-----------------------

Remap AXI signals between naming conventions:

.. code-block:: verilog

   /* axi_slave AUTO_TEMPLATE
      s_axi_aw(.*)  => axi_aw$1
      s_axi_w(.*)   => axi_w$1
      s_axi_b(.*)   => axi_b$1
      s_axi_ar(.*)  => axi_ar$1
      s_axi_r(.*)   => axi_r$1
      s_axi_(.*)    => axi_$1
   */

Multiple Capture Groups
-----------------------

Extract multiple parts from port names:

.. code-block:: verilog

   /* crossbar AUTO_TEMPLATE "xbar_(\d+)"
      port_(\d+)_(.*)  => node_%1_p$1_$2
   */
   crossbar xbar_0 (/*AUTOINST*/);
   // port_0_valid → node_0_p0_valid
   // port_1_data  → node_0_p1_data

Unused Module Tie-Off
---------------------

Completely tie off an unused module:

.. code-block:: verilog

   /* debug_controller AUTO_TEMPLATE
      .* => port.input ? '0 : _
   */
   debug_controller u_debug (/*AUTOINST*/);

Ring Buffer Connections
-----------------------

Connect modules in a ring topology:

.. code-block:: verilog

   /* ring_node AUTO_TEMPLATE "node_(\d+)"
      to_next   => link_mod(add(@, 1), 8)
      from_prev => link_@
   */
   ring_node node_0 (/*AUTOINST*/);  // to_next→link_1, from_prev→link_0
   ring_node node_7 (/*AUTOINST*/);  // to_next→link_0, from_prev→link_7


Diagnostics and Troubleshooting
===============================

Common Warnings
---------------

**Unresolved variable**

.. code-block:: text

   Warning: Unresolved substitution variable '$2' in template

This occurs when referencing a capture group that doesn't exist. Check that
your regex has enough capture groups.

**Invalid regex pattern**

.. code-block:: text

   Warning: Invalid regex pattern in template

The regex syntax is invalid. slang-autos uses RE2 regex syntax, which differs
slightly from Perl-compatible regex.

**Constant assigned to output**

.. code-block:: text

   Warning: Assigning constant to output port 'data_out'

You're assigning ``'0``, ``'1``, or ``'z`` to an output port. This is usually
a mistake. Use a ternary expression if you need direction-aware behavior.

Common Mistakes
---------------

**Forgetting braces around variables followed by digits**

.. code-block:: verilog

   /* Wrong - %10 is interpreted as capture group 10 */
   mem_addr => bank%10_addr

   /* Correct - use braces */
   mem_addr => bank%{1}0_addr

**Order of rules matters**

.. code-block:: verilog

   /* Wrong - catch-all first, specific rules never match */
   /* module AUTO_TEMPLATE
      .*  => sig_$0
      clk => sys_clk
   */

   /* Correct - specific rules first */
   /* module AUTO_TEMPLATE
      clk => sys_clk
      .*  => sig_$0
   */

**Regex special characters need escaping**

.. code-block:: verilog

   /* Wrong - dot matches any character */
   data.in => signal

   /* Correct - escape the dot for literal match */
   data\.in => signal

   /* Or use it intentionally as wildcard */
   data.* => prefix_$0
