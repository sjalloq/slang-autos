//------------------------------------------------------------------------------
// AutosServer.h
// LSP server for slang-autos AUTO expansion
//
// SPDX-FileCopyrightText: Michael Jejeloq
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------

#pragma once

#include "lsp/LspServer.h"
#include <optional>
#include <string>
#include <vector>

namespace autos {

/// Result of an expand/delete operation with diagnostics
struct ExpandResult {
    lsp::WorkspaceEdit edit;
    std::vector<std::string> messages;  // Informational messages
    std::vector<std::string> warnings;  // Warning messages
    std::vector<std::string> errors;    // Error messages
    int autoinst_count = 0;
    int autowire_count = 0;
};

/// LSP server that provides AUTO expansion via workspace/executeCommand.
/// Designed to be triggered from editor keybindings, similar to verilog-mode.
class AutosServer : public lsp::LspServer<AutosServer> {
public:
    AutosServer();

    /// LSP initialize handler - registers commands and returns capabilities
    lsp::InitializeResult getInitialize(const lsp::InitializeParams& params);

    /// LSP initialized notification handler
    void onInitialized(const lsp::InitializedParams& params);

    /// LSP shutdown handler
    std::monostate getShutdown(std::monostate);

    /// Command: Expand all AUTOs in the given file
    /// @param fileUri URI of the file to process (e.g., "file:///path/to/file.sv")
    /// @return ExpandResult with edit, diagnostics, and statistics
    ExpandResult expandAutos(const std::string& fileUri);

    /// Command: Delete all AUTO-generated content in the given file
    /// @param fileUri URI of the file to process
    /// @return ExpandResult with edit and diagnostics
    ExpandResult deleteAutos(const std::string& fileUri);

private:
    std::optional<lsp::WorkspaceFolder> m_workspaceFolder;
};

} // namespace autos
