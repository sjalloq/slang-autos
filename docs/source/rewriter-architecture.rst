==========================
AutosRewriter Architecture
==========================

This document describes the internal architecture of slang-autos' rewriter system,
which processes AUTO macros (AUTOINST, AUTOWIRE, AUTOREG) in SystemVerilog files.
Understanding this architecture is essential for debugging issues or adding new
AUTO macro types.

.. contents:: Table of Contents
   :local:
   :depth: 2

Overview and Entry Points
=========================

Purpose
-------

The ``AutosRewriter`` class provides unified processing of all AUTO macros in a
single pass over the syntax tree. It extends slang's ``SyntaxRewriter`` to:

- Find and expand ``/*AUTOINST*/`` markers in module instantiations
- Generate wire declarations for ``/*AUTOWIRE*/`` markers
- Track signal aggregation across multiple instances
- Handle template-based port renaming via ``AUTO_TEMPLATE``

Entry Point
-----------

The main entry point is ``AutosTool::expandFile()`` in ``src/Tool.cpp``:

.. code-block:: cpp

   ExpansionResult AutosTool::expandFile(
       const std::filesystem::path& file,
       bool dry_run);

This function orchestrates the entire transformation pipeline:

.. mermaid::

   flowchart TD
       A[Input .sv File] --> B["AutosTool::expandFile()"]
       B --> C["SyntaxTree::fromText()"]
       C --> D["AutosRewriter::transform()"]
       D --> E["SyntaxPrinter::printFile()"]
       E --> F[Output .sv File]

       subgraph "Slang APIs"
           C
           D
           E
       end

Key steps:

1. **Parse**: Read file and create slang ``SyntaxTree`` from source text
2. **Transform**: ``AutosRewriter`` visits each module and queues modifications
3. **Print**: Convert modified tree back to SystemVerilog text


The Four-Phase Architecture
===========================

``AutosRewriter::handle()`` processes each module in four distinct phases:

.. mermaid::

   flowchart LR
       subgraph Phase1["Phase 1: COLLECT"]
           C1[Scan module members] --> C2[Find AUTOINST nodes]
           C1 --> C3[Find AUTOWIRE/AUTOREG markers]
           C1 --> C4[Track existing declarations]
           C1 --> C5[Identify auto-block boundaries]
       end

       subgraph Phase2["Phase 2: RESOLVE"]
           R1["getModulePorts()"] --> R2["buildConnections()"]
           R2 --> R3[SignalAggregator]
       end

       subgraph Phase3["Phase 3: GENERATE"]
           G1["generateFullInstanceText()"]
           G2["generateAutowireText()"]
       end

       subgraph Phase4["Phase 4: APPLY"]
           A1["replace() AUTOINST nodes"]
           A2["insertBefore() new declarations"]
           A3["remove() old auto-blocks"]
       end

       Phase1 --> Phase2 --> Phase3 --> Phase4

Phase 1: COLLECT
----------------

``collectModuleInfo()`` iterates over all module members once, collecting:

- **AUTOINST locations**: Instances with ``/*AUTOINST*/`` marker
- **AUTOWIRE/AUTOREG markers**: Nodes whose trivia contains these markers
- **Existing declarations**: User-defined wires/logic to avoid duplicates
- **Auto-block boundaries**: ``// Beginning of automatic wires`` to ``// End of automatics``

The result is a ``CollectedInfo`` struct containing all information needed for
subsequent phases.

Phase 2: RESOLVE
----------------

``resolvePortsAndSignals()`` performs port lookups and signal aggregation:

1. For each AUTOINST, call ``getModulePorts()`` to get the instantiated module's ports from the compilation
2. Call ``buildConnections()`` to apply templates and determine signal names
3. Add connections to ``SignalAggregator`` which tracks all signals that need declarations

Phase 3: GENERATE
-----------------

Generate the text for expanded macros:

- ``generateFullInstanceText()``: Creates the complete instance with port connections
- ``generateAutowireText()``: Creates wire/logic declarations for aggregated signals

These functions produce SystemVerilog source text that will be parsed into new
syntax nodes.

Phase 4: APPLY
--------------

Queue syntax tree modifications using slang's ``SyntaxRewriter`` API:

- ``replace(old, new)``: Replace AUTOINST nodes with expanded versions
- ``insertBefore(node, new)``: Insert new declarations before marker nodes
- ``remove(node)``: Remove old auto-block declarations for re-expansion

These modifications are applied atomically when ``transform()`` returns.


Using slang's SyntaxRewriter
============================

The ``AutosRewriter`` class extends ``slang::syntax::SyntaxRewriter<AutosRewriter>``.
This section explains the key patterns for working with slang's rewriter.

Visitor Pattern
---------------

Slang's rewriter uses the visitor pattern. Define a ``handle()`` method for each
syntax node type you want to process:

.. code-block:: cpp

   class AutosRewriter : public SyntaxRewriter<AutosRewriter> {
   public:
       void handle(const ModuleDeclarationSyntax& module);
   };

The rewriter automatically traverses the syntax tree and calls your handlers.

Queuing Modifications
---------------------

Within handlers, use these methods to queue changes:

.. code-block:: cpp

   // Replace a node with new content
   replace(oldNode, newNode);

   // Insert before a node
   insertBefore(existingNode, newNode);

   // Insert after a node
   insertAfter(existingNode, newNode);

   // Remove a node
   remove(node);

**Important**: Modifications are queued, not applied immediately. They're applied
atomically when ``transform()`` completes.

Creating New Syntax Nodes
-------------------------

To create new syntax nodes, parse SystemVerilog text:

.. code-block:: cpp

   // Wrap generated text in a module for parsing
   std::string wrapper = "module _wrapper_;\n" + generated_text + "\nendmodule\n";
   auto& parsed = parse(wrapper);

   // Extract the parsed members
   auto& mod = parsed.as<ModuleDeclarationSyntax>();
   for (auto* member : mod.members) {
       insertBefore(targetNode, *member);
   }

The ``parse()`` method (inherited from ``SyntaxRewriter``) creates syntax nodes
that can be used with ``replace()``, ``insertBefore()``, etc.

Applying the Transform
----------------------

Call ``transform()`` with the original syntax tree to apply all queued modifications:

.. code-block:: cpp

   AutosRewriter rewriter(compilation, templates, options);
   auto newTree = rewriter.transform(originalTree);

   // Convert back to text
   std::string output = SyntaxPrinter::printFile(*newTree);


.. _trivia-model:

Slang's Trivia Model
====================

Understanding slang's trivia model is **essential** for working with this codebase.

Leading Trivia Model
--------------------

Slang uses a **leading trivia** model: comments and whitespace attach to the
**next** token, not the preceding one.

Consider this source code:

.. code-block:: systemverilog

   /*AUTOWIRE*/
   sub u_0 (/*AUTOINST*/);

In slang's AST, this becomes:

.. mermaid::

   flowchart TD
       subgraph Source["Source Code View"]
           S1["Line 1: /*AUTOWIRE*/"]
           S2["Line 2: sub u_0 (/*AUTOINST*/)"]
       end

       subgraph AST["Slang AST View"]
           N1["HierarchyInstantiation"]
           T1["Leading Trivia:\n  - BlockComment /*AUTOWIRE*/\n  - EndOfLine"]
           T2["Content includes /*AUTOINST*/"]
       end

       S1 -.->|"becomes leading trivia on"| N1
       T1 --> N1
       T2 --> N1

The ``/*AUTOWIRE*/`` comment is **not** a standalone node. It's trivia attached
to the ``sub u_0`` instantiation.

Why This Matters
----------------

**1. Marker Detection**

To find nodes with specific comments in their trivia:

.. code-block:: cpp

   bool hasMarkerInTrivia(const SyntaxNode& node, std::string_view marker) {
       if (auto tok = node.getFirstToken()) {
           for (const auto& trivia : tok.trivia()) {
               if (trivia.getRawText().find(marker) != std::string_view::npos) {
                   return true;
               }
           }
       }
       return false;
   }

**2. Insert Operations**

``insertBefore(node)`` inserts content **before the node's trivia**:

.. code-block:: text

   Before insertBefore(sub_u0):
       /*AUTOWIRE*/        <- trivia on sub_u0
       sub u_0 (...)

   After insertBefore(sub_u0, new_decl):
       logic foo;          <- inserted content
       /*AUTOWIRE*/        <- trivia still on sub_u0
       sub u_0 (...)

**3. Preserving Trivia on Replace**

``replace(old, new, preserveTrivia)`` has a critical parameter:

- ``preserveTrivia=false`` (default): Discards old node's trivia
- ``preserveTrivia=true``: Copies old node's trivia to new node

Use ``preserveTrivia=true`` when you want to keep comments that precede a node:

.. code-block:: cpp

   // Keep AUTO_TEMPLATE comments when replacing instances
   replace(*inst.node, *newNode, /* preserveTrivia */ true);


Key Data Structures
===================

CollectedInfo
-------------

Populated by ``collectModuleInfo()``, contains everything found in Phase 1:

.. code-block:: cpp

   struct CollectedInfo {
       // AUTOINST instances to expand
       std::vector<AutoInstInfo> autoinsts;

       // Marker positions (the node whose trivia contains the marker)
       const MemberSyntax* autowire_marker = nullptr;
       const MemberSyntax* autoreg_marker = nullptr;

       // Existing auto-blocks (for re-expansion)
       std::vector<const MemberSyntax*> autowire_block;
       const MemberSyntax* autowire_block_end = nullptr;

       // User declarations to avoid duplicating
       std::set<std::string> existing_decls;
   };

AutoInstInfo
------------

Per-instance information for AUTOINST expansion:

.. code-block:: cpp

   struct AutoInstInfo {
       const MemberSyntax* node;      // The HierarchyInstantiation node
       std::string module_type;        // e.g., "sub"
       std::string instance_name;      // e.g., "u_0"
       std::set<std::string> manual_ports;  // Manually connected ports
       const AutoTemplate* templ;      // Template rules (nullable)
   };

SignalAggregator
----------------

Tracks signals across all instances (defined in ``Expander.h``):

.. code-block:: cpp

   class SignalAggregator {
   public:
       void addFromInstance(const std::string& instance_name,
                           const std::vector<PortConnection>& connections,
                           const std::vector<PortInfo>& ports);

       std::vector<NetInfo> getInstanceDrivenNets() const;
       std::vector<NetInfo> getModuleOutputNets() const;
   };

- ``getInstanceDrivenNets()``: Signals driven by instance outputs (for AUTOWIRE)
- ``getModuleOutputNets()``: Module outputs not driven by instances (for AUTOREG)


Adding New AUTO Macro Types
===========================

To add a new AUTO macro (e.g., ``AUTOARG``):

Step 1: Update CollectedInfo
----------------------------

Add fields to track the new marker in ``AutosRewriter.h``:

.. code-block:: cpp

   struct CollectedInfo {
       // ... existing fields ...
       const MemberSyntax* autoarg_marker = nullptr;
       std::vector<const MemberSyntax*> autoarg_block;
   };

Step 2: Detect in collectModuleInfo()
-------------------------------------

Add marker detection in ``AutosRewriter.cpp``:

.. code-block:: cpp

   // In collectModuleInfo():
   if (hasMarkerInTrivia(*member, "/*AUTOARG*/")) {
       info.autoarg_marker = member;
   }

   if (hasMarker(*member, "// Beginning of automatic args")) {
       in_autoarg_block = true;
   }

Step 3: Add Generation Function
-------------------------------

Create ``generateAutoargText()`` following the pattern of ``generateAutowireText()``:

.. code-block:: cpp

   std::string AutosRewriter::generateAutoargText(...) {
       std::ostringstream oss;
       // Generate SystemVerilog text
       return oss.str();
   }

Step 4: Add Queue Function
--------------------------

Create ``queueAutoargExpansion()`` following the pattern of ``queueAutowireExpansion()``:

.. code-block:: cpp

   void AutosRewriter::queueAutoargExpansion(const CollectedInfo& info) {
       // Remove old block if re-expanding
       for (auto* node : info.autoarg_block) {
           remove(*node);
       }

       // Generate new text
       std::string text = generateAutoargText(...);

       // Parse and insert
       std::string wrapper = "module _wrapper_;\n" + text + "\nendmodule\n";
       auto& parsed = parse(wrapper);
       // ... extract and insert members ...
   }

Step 5: Call from handle()
--------------------------

Add the expansion call in ``handle()``:

.. code-block:: cpp

   if (info.autoarg_marker) {
       queueAutoargExpansion(info);
   }

Step 6: Add Tests
-----------------

Add test cases in ``tests/`` following existing patterns.


Source File Reference
=====================

Key files for the rewriter system:

- ``include/slang-autos/AutosRewriter.h``: Class declaration and data structures
- ``src/AutosRewriter.cpp``: Main rewriter implementation
- ``src/Tool.cpp``: Entry point and file processing
- ``include/slang-autos/Expander.h``: SignalAggregator and related types
- ``src/Expander.cpp``: Signal aggregation implementation
