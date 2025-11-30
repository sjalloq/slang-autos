==========================
VSCode Extension Architecture
==========================

This document describes the architecture of the slang-autos VSCode extension,
following Microsoft's recommended patterns for Language Server Protocol (LSP)
integration.

.. contents:: Table of Contents
   :local:
   :depth: 2


Overview
========

The slang-autos VSCode extension provides editor integration for expanding
verilog-mode style AUTO macros. It follows a client-server architecture where:

- **Extension (Client)**: TypeScript code running in VSCode's Extension Host
- **Language Server**: C++ executable communicating via LSP over stdin/stdout

This separation allows the heavy lifting (parsing SystemVerilog, expanding AUTOs)
to happen in a separate process, keeping the editor responsive.


Architecture Diagram
====================

.. code-block:: text

   ┌─────────────────────────────────────────────────────────────────┐
   │                         VSCode                                  │
   │  ┌───────────────────────────────────────────────────────────┐  │
   │  │                    Extension Host                         │  │
   │  │  ┌─────────────────────────────────────────────────────┐  │  │
   │  │  │              slang-autos Extension                  │  │  │
   │  │  │                                                     │  │  │
   │  │  │  User Action (Ctrl+Shift+A)                         │  │  │
   │  │  │         │                                           │  │  │
   │  │  │         ▼                                           │  │  │
   │  │  │  slang-autos.expand (UI command)                    │  │  │
   │  │  │         │                                           │  │  │
   │  │  │         ▼                                           │  │  │
   │  │  │  vscode.commands.executeCommand(                    │  │  │
   │  │  │      'slang-autos.expandAutos', uri)                │  │  │
   │  │  │         │                                           │  │  │
   │  │  │         ▼                                           │  │  │
   │  │  │  LanguageClient intercepts                          │  │  │
   │  │  │  (command in executeCommandProvider)                │  │  │
   │  │  │         │                                           │  │  │
   │  │  └─────────│─────────────────────────────────────────┘  │  │
   │  └────────────│────────────────────────────────────────────┘  │
   └───────────────│───────────────────────────────────────────────┘
                   │ JSON-RPC over stdin/stdout
                   │ workspace/executeCommand
                   ▼
   ┌───────────────────────────────────────────────────────────────┐
   │                  slang-autos-lsp (C++)                        │
   │                                                               │
   │  workspace/executeCommand handler                             │
   │         │                                                     │
   │         ▼                                                     │
   │  expandAutos(uri) ──► Parse file with slang                   │
   │         │             Expand AUTO macros                      │
   │         │             Generate WorkspaceEdit                  │
   │         ▼                                                     │
   │  Return WorkspaceEdit                                         │
   └───────────────────────────────────────────────────────────────┘
                   │
                   ▼
   ┌───────────────────────────────────────────────────────────────┐
   │  Extension applies WorkspaceEdit to document                  │
   └───────────────────────────────────────────────────────────────┘


Command Flow Pattern
====================

The extension uses Microsoft's recommended ``workspace/executeCommand`` pattern
for triggering server-side operations. This involves two layers of commands:

UI Commands (User-Facing)
-------------------------

These are registered in ``package.json`` and appear in the command palette:

.. code-block:: json

   {
     "commands": [
       {
         "command": "slang-autos.expand",
         "title": "Expand AUTOs",
         "category": "slang-autos"
       }
     ]
   }

The extension registers handlers for these commands that:

1. Validate preconditions (LSP running, file open)
2. Save the document
3. Call the LSP command via ``vscode.commands.executeCommand()``
4. Apply the returned ``WorkspaceEdit``

LSP Commands (Server-Side)
--------------------------

These are advertised by the server in its ``executeCommandProvider`` capability:

.. code-block:: json

   {
     "capabilities": {
       "executeCommandProvider": {
         "commands": ["slang-autos.expandAutos", "slang-autos.deleteAutos"]
       }
     }
   }

When the client calls ``vscode.commands.executeCommand('slang-autos.expandAutos', uri)``,
the ``LanguageClient`` library automatically:

1. Recognizes the command is in ``executeCommandProvider``
2. Sends a ``workspace/executeCommand`` request to the server
3. Returns the server's response to the caller


Why This Pattern?
=================

Microsoft recommends this pattern for several reasons:

Automatic Routing
-----------------

The ``vscode-languageclient`` library automatically handles routing when you
call ``vscode.commands.executeCommand()`` for commands advertised by the server.
You don't need to manually construct JSON-RPC messages.

Command Discovery
-----------------

Commands in ``executeCommandProvider`` are discoverable. Other extensions can
call your commands, and they appear in LSP logs as standard command executions.

Consistency
-----------

This matches how other LSP features work (hover, completion, etc.) - the client
makes a request, the server processes it, and returns a result.


Alternative: Custom Methods
===========================

LSP also allows custom request methods (e.g., ``slang-autos/expand``). While
valid, this approach:

- Requires explicit ``client.sendRequest()`` calls
- Bypasses VSCode's command routing
- Is better suited for internal implementation details

The ``workspace/executeCommand`` pattern is preferred when:

- The operation should be user-triggerable
- Other extensions might want to call it
- You want standard LSP tooling to recognize the command


Extension Implementation
========================

package.json
------------

.. code-block:: json

   {
     "activationEvents": [
       "onLanguage:verilog",
       "onLanguage:systemverilog"
     ],
     "contributes": {
       "commands": [
         {
           "command": "slang-autos.expand",
           "title": "Expand AUTOs",
           "category": "slang-autos"
         }
       ],
       "menus": {
         "commandPalette": [
           {
             "command": "slang-autos.expand",
             "when": "editorLangId == verilog || editorLangId == systemverilog"
           }
         ]
       },
       "keybindings": [
         {
           "command": "slang-autos.expand",
           "key": "ctrl+shift+a",
           "when": "editorTextFocus && editorLangId == systemverilog"
         }
       ]
     }
   }

extension.ts
------------

.. code-block:: typescript

   import * as vscode from 'vscode';
   import { LanguageClient, State } from 'vscode-languageclient/node';

   let client: LanguageClient;

   export async function activate(context: vscode.ExtensionContext) {
       // Register UI command FIRST (before async LSP startup)
       const expandCmd = vscode.commands.registerCommand('slang-autos.expand', async () => {
           if (!client || client.state !== State.Running) {
               vscode.window.showErrorMessage('LSP not running');
               return;
           }

           const editor = vscode.window.activeTextEditor;
           if (!editor) return;

           await editor.document.save();
           const uri = editor.document.uri.toString();

           // Call LSP command via vscode.commands.executeCommand
           // LanguageClient routes this to workspace/executeCommand
           const result = await vscode.commands.executeCommand<WorkspaceEdit>(
               'slang-autos.expandAutos',
               uri
           );

           // Apply the returned edit
           if (result?.changes) {
               await vscode.workspace.applyEdit(convertEdit(result));
           }
       });

       context.subscriptions.push(expandCmd);

       // Start language client
       client = new LanguageClient('slang-autos', 'slang-autos LSP', serverOptions, clientOptions);
       await client.start();
   }


Server Implementation
=====================

The C++ server registers commands via ``registerCommand``:

AutosServer.cpp
---------------

.. code-block:: cpp

   lsp::InitializeResult AutosServer::getInitialize(const lsp::InitializeParams& params) {
       // Register workspace/executeCommand handler
       registerWorkspaceExecuteCommand();

       // Register commands - advertised in executeCommandProvider
       registerCommand<std::string, lsp::WorkspaceEdit,
                       &AutosServer::expandAutos>("slang-autos.expandAutos");
       registerCommand<std::string, lsp::WorkspaceEdit,
                       &AutosServer::deleteAutos>("slang-autos.deleteAutos");

       return lsp::InitializeResult{
           .capabilities = lsp::ServerCapabilities{
               .executeCommandProvider = lsp::ExecuteCommandOptions{
                   .commands = getCommandList(),  // Returns registered command names
               },
           },
           .serverInfo = lsp::ServerInfo{
               .name = "slang-autos-lsp",
               .version = "0.1.0",
           },
       };
   }

   lsp::WorkspaceEdit AutosServer::expandAutos(const std::string& fileUri) {
       // Parse file, expand AUTOs, return WorkspaceEdit
       // ...
   }


LSP Lifecycle
=============

The server implements the full LSP lifecycle:

1. **initialize** - Exchange capabilities, register handlers
2. **initialized** - Server ready for requests
3. **workspace/executeCommand** - Handle command requests
4. **shutdown** - Prepare for exit
5. **exit** - Terminate process

.. code-block:: cpp

   AutosServer::AutosServer() {
       registerInitialize();
       registerInitialized();
       registerShutdown();
       registerExit();
   }


Error Handling
==============

The extension monitors server state for crash recovery:

.. code-block:: typescript

   client.onDidChangeState(({ oldState, newState }) => {
       if (oldState === State.Running && newState === State.Stopped) {
           vscode.window.showErrorMessage(
               'slang-autos LSP has stopped unexpectedly.',
               'Restart'
           ).then(selection => {
               if (selection === 'Restart') {
                   vscode.commands.executeCommand('slang-autos.restartServer');
               }
           });
       }
   });


Testing
=======

The LSP server can be tested directly via stdin/stdout:

.. code-block:: bash

   cat << 'EOF' | ./slang-autos-lsp
   Content-Length: 75

   {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}
   Content-Length: 52

   {"jsonrpc":"2.0","method":"initialized","params":{}}
   Content-Length: 149

   {"jsonrpc":"2.0","id":2,"method":"workspace/executeCommand","params":{"command":"slang-autos.expandAutos","arguments":["file:///path/to/file.sv"]}}
   EOF


References
==========

- `VSCode Extension API <https://code.visualstudio.com/api>`_
- `Language Server Protocol <https://microsoft.github.io/language-server-protocol/>`_
- `vscode-languageclient <https://www.npmjs.com/package/vscode-languageclient>`_
- `VSCode Language Server Extension Guide <https://code.visualstudio.com/api/language-extensions/language-server-extension-guide>`_
