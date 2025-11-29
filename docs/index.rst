.. slang-autos documentation master file, created by
   sphinx-quickstart on Sat Nov 29 16:49:19 2025.

=========================
slang-autos Documentation
=========================

slang-autos is a tool for expanding verilog-mode style AUTO macros in
SystemVerilog files, built on the `slang <https://github.com/MikePopoloski/slang>`_
SystemVerilog compiler frontend.

Supported AUTO macros:

- ``/*AUTOINST*/`` - Automatic port connections
- ``/*AUTOWIRE*/`` - Automatic wire declarations
- ``/*AUTOREG*/`` - Automatic register declarations
- ``/*AUTOPORTS*/`` - Automatic port declarations
- ``AUTO_TEMPLATE`` - Template-based port renaming

Getting Started
===============

See the `README <https://github.com/your-repo/slang-autos#readme>`_ for
installation instructions and basic usage.

Developer Documentation
=======================

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   rewriter-architecture


Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
