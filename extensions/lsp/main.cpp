//------------------------------------------------------------------------------
// main.cpp
// Entry point for slang-autos LSP server
//
// SPDX-FileCopyrightText: Michael Jejeloq
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------

#include "AutosServer.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // Check for --help
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "slang-autos-lsp - LSP server for AUTO macro expansion\n\n"
                      << "Usage: slang-autos-lsp\n\n"
                      << "The server communicates via JSON-RPC over stdin/stdout.\n"
                      << "It provides the following commands via workspace/executeCommand:\n\n"
                      << "  slang-autos.expandAutos <fileUri>  - Expand all AUTOs in file\n"
                      << "  slang-autos.deleteAutos <fileUri>  - Delete AUTO-generated content\n\n"
                      << "Typically invoked by an editor extension, not directly.\n";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "slang-autos-lsp 0.1.0\n";
            return 0;
        }
    }

    autos::AutosServer server;
    server.run();

    return 0;
}
