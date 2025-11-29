//------------------------------------------------------------------------------
// AutosServer.cpp
// LSP server implementation for slang-autos AUTO expansion
//
// SPDX-FileCopyrightText: Michael Jejeloq
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------

#include "AutosServer.h"
#include "lsp/URI.h"
#include "slang-autos/Tool.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace autos {

AutosServer::AutosServer() {
    registerInitialize();
    registerShutdown();
}

lsp::InitializeResult AutosServer::getInitialize(const lsp::InitializeParams& params) {
    // Register the executeCommand handler
    registerWorkspaceExecuteCommand();

    // Register our custom commands
    registerCommand<std::string, lsp::WorkspaceEdit,
                    &AutosServer::expandAutos>("slang-autos.expandAutos");
    registerCommand<std::string, lsp::WorkspaceEdit,
                    &AutosServer::deleteAutos>("slang-autos.deleteAutos");

    // Store workspace folder if provided
    if (params.workspaceFolders.has_value() && !params.workspaceFolders->empty()) {
        m_workspaceFolder = params.workspaceFolders->at(0);
    } else if (params.rootUri.has_value()) {
        m_workspaceFolder = lsp::WorkspaceFolder{
            .uri = params.rootUri.value(),
            .name = "root",
        };
    }

    std::cerr << "slang-autos LSP initialized";
    if (m_workspaceFolder) {
        std::cerr << " at " << m_workspaceFolder->uri.getPath();
    }
    std::cerr << "\n";

    // Return capabilities
    return lsp::InitializeResult{
        .capabilities = lsp::ServerCapabilities{
            .executeCommandProvider = lsp::ExecuteCommandOptions{
                .commands = getCommandList(),
            },
        },
        .serverInfo = lsp::ServerInfo{
            .name = "slang-autos-lsp",
            .version = "0.1.0",
        },
    };
}

void AutosServer::onInitialized(const lsp::InitializedParams&) {
    std::cerr << "slang-autos LSP ready\n";
}

std::monostate AutosServer::getShutdown(std::monostate) {
    std::cerr << "slang-autos LSP shutting down\n";
    return std::monostate{};
}

lsp::WorkspaceEdit AutosServer::expandAutos(const std::string& fileUri) {
    lsp::WorkspaceEdit edit;

    // Convert URI to file path
    URI fileUriObj(fileUri);
    std::filesystem::path filePath(fileUriObj.getPath());

    std::cerr << "Expanding AUTOs in: " << filePath << "\n";

    // Read original content to get line count for full-file replacement
    std::ifstream ifs(filePath);
    if (!ifs) {
        std::cerr << "Failed to open file: " << filePath << "\n";
        return edit;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string originalContent = buffer.str();
    ifs.close();

    // Count lines in original content
    size_t lineCount = 0;
    for (char c : originalContent) {
        if (c == '\n') lineCount++;
    }
    // Add 1 if file doesn't end with newline
    if (!originalContent.empty() && originalContent.back() != '\n') {
        lineCount++;
    }

    // Create tool and expand
    slang_autos::AutosTool tool;

    // TODO: Need to get compile arguments from somewhere
    // For now, just use the single file - this won't work for modules
    // that need other files to be compiled
    std::vector<std::string> args = {filePath.string()};

    if (!tool.loadWithArgs(args)) {
        std::cerr << "Failed to load file for compilation\n";
        // Return empty edit on failure
        return edit;
    }

    // Expand with dry_run=true so we don't write to disk
    auto result = tool.expandFile(filePath, true);

    if (!result.hasChanges()) {
        std::cerr << "No changes needed\n";
        return edit;
    }

    // Create a TextEdit that replaces the entire file content
    lsp::TextEdit textEdit{
        .range = lsp::Range{
            .start = lsp::Position{.line = 0, .character = 0},
            .end = lsp::Position{
                .line = static_cast<unsigned int>(lineCount),
                .character = 0
            },
        },
        .newText = result.modified_content,
    };

    // Add to workspace edit
    edit.changes = std::unordered_map<std::string, std::vector<lsp::TextEdit>>{};
    edit.changes->emplace(fileUri, std::vector<lsp::TextEdit>{textEdit});

    std::cerr << "Expanded " << result.autoinst_count << " AUTOINST, "
              << result.autowire_count << " AUTOWIRE\n";

    return edit;
}

lsp::WorkspaceEdit AutosServer::deleteAutos(const std::string& fileUri) {
    lsp::WorkspaceEdit edit;

    // TODO: Implement delete - strip AUTO-generated content
    // For now, just return empty edit
    std::cerr << "deleteAutos not yet implemented for: " << fileUri << "\n";

    return edit;
}

} // namespace autos
