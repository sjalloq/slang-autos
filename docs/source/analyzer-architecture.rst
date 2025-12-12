========================
AutosAnalyzer Architecture
========================

This document describes the internal architecture of slang-autos' analyzer system,
which processes AUTO macros (AUTOINST, AUTOLOGIC, AUTOPORTS) in SystemVerilog files.
Understanding this architecture is essential for debugging issues or adding new
AUTO macro types.

.. contents:: Table of Contents
   :local:
   :depth: 2

Overview and Entry Points
=========================

Purpose
-------

The ``AutosAnalyzer`` class analyzes SystemVerilog modules and generates text
replacements for AUTO macros. Key design principles:

- **AST for analysis only** - All modifications are done via text replacement
- **Perfect whitespace preservation** - Original formatting is never disturbed
- **All positions from AST** - Token locations and trivia offsets, never regex

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
       C --> D["AutosAnalyzer::analyze()"]
       D --> E["SourceWriter::applyReplacements()"]
       E --> F[Output .sv File]

       subgraph "Analysis (read-only)"
           C
           D
       end

       subgraph "Text Replacement"
           E
       end

Key steps:

1. **Parse**: Read file and create slang ``SyntaxTree`` from source text
2. **Analyze**: ``AutosAnalyzer`` collects position information and generates replacements
3. **Apply**: ``SourceWriter`` applies text replacements to original source


The Three-Phase Architecture
============================

``AutosAnalyzer::analyze()`` processes each module in three distinct phases:

.. mermaid::

   flowchart LR
       subgraph Phase1["Phase 1: COLLECT"]
           C1[Scan module members] --> C2[Find AUTOINST markers]
           C1 --> C3[Find AUTOLOGIC/AUTOPORTS markers]
           C1 --> C4[Track existing declarations]
           C1 --> C5[Record byte positions from AST]
       end

       subgraph Phase2["Phase 2: RESOLVE"]
           R1["getModulePorts()"] --> R2["buildConnections()"]
           R2 --> R3[SignalAggregator]
       end

       subgraph Phase3["Phase 3: GENERATE"]
           G1["Generate replacement text"]
           G2["Create Replacement structs"]
       end

       Phase1 --> Phase2 --> Phase3

Phase 1: COLLECT
----------------

``collectModuleInfo()`` iterates over all module members once, collecting:

- **AUTOINST locations**: Instances with ``/*AUTOINST*/`` marker, including:
  - Marker end position (byte offset after ``*/``)
  - Close paren position (byte offset of ``)``)
  - Manual ports (ports before the marker)
- **AUTOLOGIC markers**: Position for inserting declarations
- **AUTOPORTS markers**: Position in port list
- **Existing declarations**: User-defined wires/logic to avoid duplicates
- **Auto-block boundaries**: ``// Beginning of automatic logic`` to ``// End of automatics``

All positions come from AST token locations and trivia offsets - never from
searching the source text.

Phase 2: RESOLVE
----------------

``resolvePortsAndSignals()`` performs port lookups and signal aggregation:

1. For each AUTOINST, call ``getModulePorts()`` to get the instantiated module's ports
2. Call ``buildConnections()`` to apply templates and determine signal names
3. Add connections to ``SignalAggregator`` which tracks signals needing declarations

Phase 3: GENERATE
-----------------

Generate ``Replacement`` structs containing:

- ``start``: Byte offset where replacement begins
- ``end``: Byte offset where replacement ends
- ``new_text``: The replacement text
- ``description``: Human-readable description

For AUTOINST, the replacement spans from marker end to close paren.
For AUTOLOGIC, it either inserts after marker or replaces existing block.
For AUTOPORTS, it replaces from marker end to close paren.


Text-Based Replacement
======================

Unlike traditional AST rewriters, ``AutosAnalyzer`` never modifies the syntax tree.
Instead, it generates ``Replacement`` structs that are applied to the original source.

Replacement Struct
------------------

.. code-block:: cpp

   struct Replacement {
       size_t start;           // Byte offset in original source
       size_t end;             // Byte offset (exclusive)
       std::string new_text;   // Text to insert
       std::string description; // For debugging
   };

Applying Replacements
---------------------

``SourceWriter::applyReplacements()`` applies replacements from highest offset first:

.. code-block:: cpp

   AutosAnalyzer analyzer(compilation, templates, options);
   analyzer.analyze(tree, source_content);

   auto& replacements = analyzer.getReplacements();
   std::string output = writer.applyReplacements(source_content, replacements);

This approach guarantees:

- **Perfect whitespace preservation** - Only specified byte ranges are modified
- **No formatting changes** - Original indentation and comments are preserved
- **Idempotency** - Re-running on expanded output produces identical results


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

   /*AUTOLOGIC*/
   sub u_0 (/*AUTOINST*/);

In slang's AST:

- ``/*AUTOLOGIC*/`` is leading trivia on the ``sub`` token
- ``/*AUTOINST*/`` is part of the instance's port connection area

Trivia Position Calculation
---------------------------

To find marker positions in trivia, we calculate offsets:

.. code-block:: cpp

   std::optional<std::pair<size_t, size_t>>
   AutosAnalyzer::findMarkerInTrivia(Token tok, std::string_view marker) const {
       // Token location is where the actual token text starts
       size_t token_loc = tok.location().offset();

       // Calculate total trivia length
       size_t total_trivia_len = 0;
       for (const auto& trivia : tok.trivia()) {
           total_trivia_len += trivia.getRawText().length();
       }

       // Trivia starts before the token
       size_t trivia_offset = token_loc - total_trivia_len;

       // Walk through trivia to find our marker
       for (const auto& trivia : tok.trivia()) {
           auto raw = trivia.getRawText();
           auto pos = raw.find(marker);
           if (pos != std::string_view::npos) {
               size_t start = trivia_offset + pos;
               size_t end = start + marker.length();
               return std::make_pair(start, end);
           }
           trivia_offset += raw.length();
       }
       return std::nullopt;
   }

This is necessary because ``trivia.location()`` is private in slang.


Key Data Structures
===================

CollectedInfo
-------------

Populated by ``collectModuleInfo()``, contains everything found in Phase 1:

.. code-block:: cpp

   struct CollectedInfo {
       // AUTOINST instances to expand
       std::vector<AutoInstInfo> autoinsts;

       // AUTOLOGIC marker and block info
       AutoLogicInfo autologic;
       bool has_autologic = false;

       // AUTOPORTS marker and port list bounds
       AutoPortsInfo autoports;
       bool has_autoports = false;

       // User declarations to avoid duplicating
       std::set<std::string> existing_decls;
   };

AutoInstInfo
------------

Per-instance information for AUTOINST expansion:

.. code-block:: cpp

   struct AutoInstInfo {
       const MemberSyntax* node;           // The HierarchyInstantiation node
       std::string module_type;            // e.g., "sub"
       std::string instance_name;          // e.g., "u_0"
       std::set<std::string> manual_ports; // Manually connected ports
       const AutoTemplate* templ;          // Template rules (nullable)

       // Byte positions from AST
       size_t marker_end = 0;      // Position after /*AUTOINST*/
       size_t close_paren_pos = 0; // Position of )
   };

AutoLogicInfo
-------------

Information about AUTOLOGIC marker and existing block:

.. code-block:: cpp

   struct AutoLogicInfo {
       size_t marker_end = 0;          // Position after /*AUTOLOGIC*/
       bool has_existing_block = false;
       size_t block_start = 0;         // Start of "// Beginning..."
       size_t block_end = 0;           // End of "// End of automatics"
   };

SignalAggregator
----------------

Tracks signals across all instances (defined in ``SignalAggregator.h``):

.. code-block:: cpp

   class SignalAggregator {
   public:
       void addFromInstance(const std::string& instance_name,
                           const std::vector<PortConnection>& connections,
                           const std::vector<PortInfo>& ports);

       std::vector<NetInfo> getInternalNets() const;
       std::vector<NetInfo> getExternalInputNets() const;
       std::vector<NetInfo> getExternalOutputNets() const;
   };


Adding New AUTO Macro Types
===========================

To add a new AUTO macro (e.g., ``AUTOARG``):

Step 1: Update Collection Structs
---------------------------------

Add fields to track the new marker in ``AutosAnalyzer.h``:

.. code-block:: cpp

   struct AutoArgInfo {
       size_t marker_end = 0;
       // ... other position info
   };

   struct CollectedInfo {
       // ... existing fields ...
       AutoArgInfo autoarg;
       bool has_autoarg = false;
   };

Step 2: Detect in collectModuleInfo()
-------------------------------------

Add marker detection in ``AutosAnalyzer.cpp``:

.. code-block:: cpp

   // In collectModuleInfo():
   if (auto pos = findMarkerInTrivia(tok, markers::AUTOARG)) {
       info.has_autoarg = true;
       info.autoarg.marker_end = pos->second;
   }

Step 3: Add Generation Function
-------------------------------

Create ``generateAutoargReplacement()`` following existing patterns:

.. code-block:: cpp

   void AutosAnalyzer::generateAutoargReplacement(const CollectedInfo& info) {
       std::ostringstream oss;
       // Generate replacement text

       replacements_.push_back({
           info.autoarg.start_pos,
           info.autoarg.end_pos,
           oss.str(),
           "AUTOARG"
       });
   }

Step 4: Call from generateReplacements()
----------------------------------------

Add the generation call:

.. code-block:: cpp

   if (info.has_autoarg) {
       generateAutoargReplacement(info);
   }

Step 5: Add Tests
-----------------

Add test cases in ``tests/`` following existing patterns.


Source File Reference
=====================

Key files for the analyzer system:

- ``include/slang-autos/AutosAnalyzer.h``: Class declaration and data structures
- ``src/AutosAnalyzer.cpp``: Main analyzer implementation
- ``src/Tool.cpp``: Entry point and file processing
- ``include/slang-autos/SignalAggregator.h``: Signal aggregation types
- ``src/SignalAggregator.cpp``: Signal aggregation implementation
- ``include/slang-autos/Writer.h``: Replacement struct and SourceWriter
- ``src/Writer.cpp``: Text replacement application
