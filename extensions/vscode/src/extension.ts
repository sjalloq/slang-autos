// SPDX-License-Identifier: Apache-2.0
import * as vscode from 'vscode';
import * as fs from 'fs';
import * as which from 'which';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    State,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel;
let extensionContext: vscode.ExtensionContext;

export async function activate(context: vscode.ExtensionContext) {
    // Store context for later use (e.g., restart command)
    extensionContext = context;

    // Create output channel for server logs
    outputChannel = vscode.window.createOutputChannel('slang-autos');
    context.subscriptions.push(outputChannel);

    outputChannel.appendLine('slang-autos extension activating...');

    // Register commands FIRST (before any async operations)
    // This ensures commands work on first invocation per Microsoft best practices
    registerCommands(context);

    // Start the language client
    await startLanguageClient(context);

    outputChannel.appendLine('slang-autos extension activated');
}

function registerCommands(context: vscode.ExtensionContext) {
    // Note: We don't register handlers for expandAutos/deleteAutos here.
    // The LanguageClient automatically registers handlers for commands
    // advertised by the server via executeCommandProvider.
    // We just need wrapper commands that prepare the arguments and handle results.

    const expandCmd = vscode.commands.registerCommand('slang-autos.expand', async () => {
        if (!client || client.state !== State.Running) {
            vscode.window.showErrorMessage(
                'slang-autos LSP is not running. Check the slang-autos output for errors.'
            );
            return;
        }

        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            vscode.window.showWarningMessage('No active editor');
            return;
        }

        // Save the document first to ensure server works with latest content
        await editor.document.save();

        const fileUri = editor.document.uri.toString();
        outputChannel.appendLine(`Expanding AUTOs in: ${fileUri}`);

        try {
            // Use workspace/executeCommand via vscode.commands.executeCommand
            // The LanguageClient intercepts this and routes to the LSP server
            const result = await vscode.commands.executeCommand<WorkspaceEditResult>(
                'slang-autos.expandAutos',
                fileUri
            );

            // Apply the workspace edit if we got changes
            if (result && result.changes && Object.keys(result.changes).length > 0) {
                const edit = convertToWorkspaceEdit(result);
                const success = await vscode.workspace.applyEdit(edit);

                if (success) {
                    vscode.window.showInformationMessage('AUTOs expanded successfully');
                } else {
                    vscode.window.showErrorMessage('Failed to apply AUTO expansions');
                }
            } else {
                vscode.window.showInformationMessage('No changes needed');
            }
        } catch (error) {
            outputChannel.appendLine(`Error expanding AUTOs: ${error}`);
            vscode.window.showErrorMessage(`Error expanding AUTOs: ${error}`);
        }
    });

    const deleteCmd = vscode.commands.registerCommand('slang-autos.delete', async () => {
        if (!client || client.state !== State.Running) {
            vscode.window.showErrorMessage('slang-autos LSP is not running');
            return;
        }

        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            vscode.window.showWarningMessage('No active editor');
            return;
        }

        await editor.document.save();
        const fileUri = editor.document.uri.toString();

        try {
            const result = await vscode.commands.executeCommand<WorkspaceEditResult>(
                'slang-autos.deleteAutos',
                fileUri
            );

            if (result && result.changes && Object.keys(result.changes).length > 0) {
                const edit = convertToWorkspaceEdit(result);
                const success = await vscode.workspace.applyEdit(edit);

                if (success) {
                    vscode.window.showInformationMessage('AUTOs deleted successfully');
                } else {
                    vscode.window.showErrorMessage('Failed to delete AUTOs');
                }
            } else {
                vscode.window.showInformationMessage('No AUTO content to delete');
            }
        } catch (error) {
            outputChannel.appendLine(`Error deleting AUTOs: ${error}`);
            vscode.window.showErrorMessage(`Error deleting AUTOs: ${error}`);
        }
    });

    // Add restart command for recovery
    const restartCmd = vscode.commands.registerCommand('slang-autos.restartServer', async () => {
        outputChannel.appendLine('Restarting language server...');
        if (client) {
            await client.stop();
            client = undefined;
        }
        await startLanguageClient(extensionContext);
    });

    context.subscriptions.push(expandCmd, deleteCmd, restartCmd);
    outputChannel.appendLine('Commands registered');
}

async function startLanguageClient(context: vscode.ExtensionContext) {
    try {
        const serverPath = await findServerPath();
        if (!serverPath) {
            vscode.window.showErrorMessage(
                'slang-autos-lsp not found. Please set slang-autos.serverPath in settings or add to PATH.'
            );
            return;
        }

        // Validate server exists and is accessible
        if (!fs.existsSync(serverPath)) {
            vscode.window.showErrorMessage(
                `slang-autos-lsp not found at configured path: ${serverPath}`
            );
            return;
        }

        outputChannel.appendLine(`Using slang-autos-lsp at: ${serverPath}`);

        const serverOptions: ServerOptions = {
            run: { command: serverPath },
            debug: { command: serverPath },
        };

        const clientOptions: LanguageClientOptions = {
            documentSelector: [
                { scheme: 'file', language: 'verilog' },
                { scheme: 'file', language: 'systemverilog' },
            ],
            outputChannel: outputChannel,
            traceOutputChannel: outputChannel,
        };

        client = new LanguageClient(
            'slang-autos',
            'slang-autos LSP',
            serverOptions,
            clientOptions
        );

        // Monitor server state for crash recovery (Microsoft best practice)
        context.subscriptions.push(
            client.onDidChangeState(({ oldState, newState }) => {
                outputChannel.appendLine(`LSP state: ${State[oldState]} -> ${State[newState]}`);

                if (oldState === State.Running && newState === State.Stopped) {
                    // Server crashed - offer restart
                    vscode.window.showErrorMessage(
                        'slang-autos LSP has stopped unexpectedly.',
                        'Restart'
                    ).then(selection => {
                        if (selection === 'Restart') {
                            vscode.commands.executeCommand('slang-autos.restartServer');
                        }
                    });
                }
            })
        );

        // Add client to subscriptions for proper cleanup
        context.subscriptions.push(client);

        await client.start();

        // Log server info after successful start
        const serverInfo = client.initializeResult?.serverInfo;
        if (serverInfo) {
            outputChannel.appendLine(`Connected to ${serverInfo.name} v${serverInfo.version}`);
        }

        outputChannel.appendLine('slang-autos LSP client started');
    } catch (error) {
        outputChannel.appendLine(`Failed to start LSP client: ${error}`);
        vscode.window.showErrorMessage(`Failed to start slang-autos LSP: ${error}`);
    }
}

export async function deactivate(): Promise<void> {
    if (client) {
        await client.stop();
        client = undefined;
    }
}

async function findServerPath(): Promise<string | undefined> {
    // First check the configuration
    const config = vscode.workspace.getConfiguration('slang-autos');
    const configPath = config.get<string>('serverPath');

    if (configPath && configPath.length > 0) {
        return configPath;
    }

    // Otherwise search PATH
    try {
        return await which.default('slang-autos-lsp');
    } catch {
        return undefined;
    }
}

// Type definitions for LSP responses
interface TextEditResult {
    range: {
        start: { line: number; character: number };
        end: { line: number; character: number };
    };
    newText: string;
}

interface WorkspaceEditResult {
    changes?: { [uri: string]: TextEditResult[] };
}

function convertToWorkspaceEdit(result: WorkspaceEditResult): vscode.WorkspaceEdit {
    const edit = new vscode.WorkspaceEdit();

    if (result.changes) {
        for (const [uri, textEdits] of Object.entries(result.changes)) {
            const docUri = vscode.Uri.parse(uri);
            for (const textEdit of textEdits) {
                const range = new vscode.Range(
                    new vscode.Position(textEdit.range.start.line, textEdit.range.start.character),
                    new vscode.Position(textEdit.range.end.line, textEdit.range.end.character)
                );
                edit.replace(docUri, range, textEdit.newText);
            }
        }
    }

    return edit;
}
