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
    registerInitialized();
    registerShutdown();
    registerExit();
}

lsp::InitializeResult AutosServer::getInitialize(const lsp::InitializeParams& params) {
    // Register the workspace/executeCommand handler
    // This allows the client to call commands via vscode.commands.executeCommand()
    registerWorkspaceExecuteCommand();

    // Register our commands - these will be advertised in executeCommandProvider
    // and automatically routed by LanguageClient when called via executeCommand
    registerCommand<std::string, ExpandResult,
                    &AutosServer::expandAutos>("slang-autos.expandAutos");
    registerCommand<std::string, ExpandResult,
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

ExpandResult AutosServer::expandAutos(const std::string& fileUri) {
    ExpandResult result;

    // Convert URI to file path
    URI fileUriObj(fileUri);
    std::filesystem::path filePath(fileUriObj.getPath());

    std::cerr << "Expanding AUTOs in: " << filePath << "\n";

    // Read original content to get line count for full-file replacement
    std::ifstream ifs(filePath);
    if (!ifs) {
        std::string msg = "Failed to open file: " + filePath.string();
        std::cerr << msg << "\n";
        result.errors.push_back(msg);
        return result;
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
        std::string msg = "Failed to load file for compilation. Check that all referenced modules are available.";
        std::cerr << msg << "\n";
        result.errors.push_back(msg);
        return result;
    }

    // Expand with dry_run=true so we don't write to disk
    auto expansionResult = tool.expandFile(filePath, true);

    // Collect diagnostics from the tool
    const auto& diagnostics = tool.diagnostics();
    for (const auto& diag : diagnostics.diagnostics()) {
        std::string msg = diag.message;
        if (!diag.file_path.empty()) {
            msg = diag.file_path;
            if (diag.line_number > 0) {
                msg += ":" + std::to_string(diag.line_number);
            }
            msg += ": " + diag.message;
        }

        if (diag.level == slang_autos::DiagnosticLevel::Error) {
            result.errors.push_back(msg);
        } else {
            result.warnings.push_back(msg);
        }
    }

    // Store counts
    result.autoinst_count = expansionResult.autoinst_count;
    result.autologic_count = expansionResult.autologic_count;

    if (!expansionResult.hasChanges()) {
        std::cerr << "No changes needed\n";
        if (result.errors.empty() && result.warnings.empty()) {
            result.messages.push_back("No AUTO macros found in file.");
        }
        return result;
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
        .newText = expansionResult.modified_content,
    };

    // Add to workspace edit
    result.edit.changes = std::unordered_map<std::string, std::vector<lsp::TextEdit>>{};
    result.edit.changes->emplace(fileUri, std::vector<lsp::TextEdit>{textEdit});

    // Add success message
    std::stringstream ss;
    ss << "Expanded " << expansionResult.autoinst_count << " AUTOINST";
    if (expansionResult.autologic_count > 0) {
        ss << ", " << expansionResult.autologic_count << " AUTOLOGIC";
    }
    result.messages.push_back(ss.str());

    std::cerr << ss.str() << "\n";

    return result;
}

ExpandResult AutosServer::deleteAutos(const std::string& fileUri) {
    ExpandResult result;

    // TODO: Implement delete - strip AUTO-generated content
    // For now, just return empty result with message
    std::cerr << "deleteAutos not yet implemented for: " << fileUri << "\n";
    result.messages.push_back("Delete AUTOs is not yet implemented.");

    return result;
}

} // namespace autos
