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

// Extension name prefix for all user-facing messages
const EXT_NAME = 'slang-autos';

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel;
let extensionContext: vscode.ExtensionContext;

// Logging helpers
function getVerbose(): boolean {
    const config = vscode.workspace.getConfiguration('slang-autos');
    return config.get<boolean>('verbose') || process.env.SLANG_AUTOS_VERBOSE === '1';
}

function log(message: string): void {
    outputChannel.appendLine(`[${EXT_NAME}] ${message}`);
}

function logVerbose(message: string): void {
    if (getVerbose()) {
        outputChannel.appendLine(`[${EXT_NAME}] [DEBUG] ${message}`);
    }
}

// User-facing message helpers (always include extension name)
function showInfo(message: string): Thenable<string | undefined> {
    return vscode.window.showInformationMessage(`${EXT_NAME}: ${message}`);
}

function showWarning(message: string): Thenable<string | undefined> {
    return vscode.window.showWarningMessage(`${EXT_NAME}: ${message}`);
}

function showError(message: string, ...items: string[]): Thenable<string | undefined> {
    return vscode.window.showErrorMessage(`${EXT_NAME}: ${message}`, ...items);
}

export async function activate(context: vscode.ExtensionContext) {
    // Store context for later use (e.g., restart command)
    extensionContext = context;

    // Create output channel for server logs
    outputChannel = vscode.window.createOutputChannel('slang-autos');
    context.subscriptions.push(outputChannel);

    log('Extension activating...');
    logVerbose(`Verbose logging enabled (setting: ${vscode.workspace.getConfiguration('slang-autos').get('verbose')}, env: ${process.env.SLANG_AUTOS_VERBOSE})`);

    // Register commands FIRST (before any async operations)
    // This ensures commands work on first invocation per Microsoft best practices
    registerCommands(context);

    // Start the language client
    await startLanguageClient(context);

    log('Extension activated');
}

function registerCommands(context: vscode.ExtensionContext) {
    // Note: We don't register handlers for expandAutos/deleteAutos here.
    // The LanguageClient automatically registers handlers for commands
    // advertised by the server via executeCommandProvider.
    // We just need wrapper commands that prepare the arguments and handle results.

    const expandCmd = vscode.commands.registerCommand('slang-autos.expand', async () => {
        if (!client || client.state !== State.Running) {
            showError('LSP server is not running. Check the Output panel (slang-autos) for details.');
            return;
        }

        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            showWarning('No active editor. Open a SystemVerilog file first.');
            return;
        }

        // Save the document first to ensure server works with latest content
        await editor.document.save();

        const fileUri = editor.document.uri.toString();
        log(`Expanding AUTOs in: ${fileUri}`);
        logVerbose(`File URI: ${fileUri}`);

        try {
            // Use workspace/executeCommand via vscode.commands.executeCommand
            // The LanguageClient intercepts this and routes to the LSP server
            logVerbose('Sending slang-autos.expandAutos command to LSP server...');
            const result = await vscode.commands.executeCommand<ExpandResult>(
                'slang-autos.expandAutos',
                fileUri
            );
            logVerbose(`Server response: ${JSON.stringify(result)}`);

            // Handle errors first
            if (result?.errors && result.errors.length > 0) {
                for (const err of result.errors) {
                    log(`Error: ${err}`);
                }
                outputChannel.show(true);  // Show output panel (preserve editor focus)
                showError(result.errors.join('\n'));
                return;
            }

            // Show warnings in output (but don't block)
            if (result?.warnings && result.warnings.length > 0) {
                for (const warn of result.warnings) {
                    log(`Warning: ${warn}`);
                }
                outputChannel.show(true);  // Show output panel (preserve editor focus)
                // Also show first warning to user
                showWarning(result.warnings[0] + (result.warnings.length > 1 ? ` (+${result.warnings.length - 1} more)` : ''));
            }

            // Apply the workspace edit if we got changes
            if (result?.edit?.changes && Object.keys(result.edit.changes).length > 0) {
                const edit = convertToWorkspaceEdit(result.edit);
                logVerbose(`Applying ${Object.keys(result.edit.changes).length} file edit(s)...`);
                const success = await vscode.workspace.applyEdit(edit);

                if (success) {
                    // Show the success message from server, or a default
                    const msg = result.messages?.length > 0
                        ? result.messages.join('. ')
                        : 'AUTOs expanded successfully.';
                    showInfo(msg);
                } else {
                    showError('Failed to apply AUTO expansions to the document.');
                }
            } else {
                // No changes - show messages or default
                const msg = result?.messages?.length > 0
                    ? result.messages.join('. ')
                    : 'No AUTOs found or no changes needed.';
                showInfo(msg);
            }
        } catch (error) {
            log(`Error expanding AUTOs: ${error}`);
            logVerbose(`Stack trace: ${error instanceof Error ? error.stack : 'N/A'}`);
            showError(`Failed to expand AUTOs: ${error}`);
        }
    });

    const deleteCmd = vscode.commands.registerCommand('slang-autos.delete', async () => {
        if (!client || client.state !== State.Running) {
            showError('LSP server is not running. Check the Output panel (slang-autos) for details.');
            return;
        }

        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            showWarning('No active editor. Open a SystemVerilog file first.');
            return;
        }

        await editor.document.save();
        const fileUri = editor.document.uri.toString();
        log(`Deleting AUTOs in: ${fileUri}`);

        try {
            logVerbose('Sending slang-autos.deleteAutos command to LSP server...');
            const result = await vscode.commands.executeCommand<ExpandResult>(
                'slang-autos.deleteAutos',
                fileUri
            );
            logVerbose(`Server response: ${JSON.stringify(result)}`);

            // Handle errors first
            if (result?.errors && result.errors.length > 0) {
                for (const err of result.errors) {
                    log(`Error: ${err}`);
                }
                outputChannel.show(true);  // Show output panel (preserve editor focus)
                showError(result.errors.join('\n'));
                return;
            }

            // Show warnings in output
            if (result?.warnings && result.warnings.length > 0) {
                for (const warn of result.warnings) {
                    log(`Warning: ${warn}`);
                }
                outputChannel.show(true);  // Show output panel (preserve editor focus)
                showWarning(result.warnings[0] + (result.warnings.length > 1 ? ` (+${result.warnings.length - 1} more)` : ''));
            }

            if (result?.edit?.changes && Object.keys(result.edit.changes).length > 0) {
                const edit = convertToWorkspaceEdit(result.edit);
                const success = await vscode.workspace.applyEdit(edit);

                if (success) {
                    const msg = result.messages?.length > 0
                        ? result.messages.join('. ')
                        : 'AUTO-generated content deleted successfully.';
                    showInfo(msg);
                } else {
                    showError('Failed to delete AUTO-generated content.');
                }
            } else {
                const msg = result?.messages?.length > 0
                    ? result.messages.join('. ')
                    : 'No AUTO-generated content found to delete.';
                showInfo(msg);
            }
        } catch (error) {
            log(`Error deleting AUTOs: ${error}`);
            logVerbose(`Stack trace: ${error instanceof Error ? error.stack : 'N/A'}`);
            showError(`Failed to delete AUTOs: ${error}`);
        }
    });

    // Add restart command for recovery
    const restartCmd = vscode.commands.registerCommand('slang-autos.restartServer', async () => {
        log('Restarting language server...');
        if (client) {
            await client.stop();
            client = undefined;
        }
        await startLanguageClient(extensionContext);
        showInfo('Language server restarted.');
    });

    context.subscriptions.push(expandCmd, deleteCmd, restartCmd);
    logVerbose('Commands registered: expand, delete, restartServer');
}

async function startLanguageClient(context: vscode.ExtensionContext) {
    try {
        const serverPath = await findServerPath();
        if (!serverPath) {
            showError(
                'LSP server (slang-autos-lsp) not found. Set slang-autos.serverPath in settings or add to PATH.'
            );
            return;
        }

        // Validate server exists and is accessible
        if (!fs.existsSync(serverPath)) {
            showError(`LSP server not found at: ${serverPath}`);
            return;
        }

        log(`Using LSP server: ${serverPath}`);
        logVerbose(`Server path resolved from: ${vscode.workspace.getConfiguration('slang-autos').get('serverPath') ? 'settings' : 'PATH'}`);

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
                logVerbose(`LSP state change: ${State[oldState]} -> ${State[newState]}`);

                if (oldState === State.Running && newState === State.Stopped) {
                    // Server crashed - offer restart
                    showError(
                        'LSP server stopped unexpectedly.',
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

        logVerbose('Starting LSP client...');
        await client.start();

        // Log server info after successful start
        const serverInfo = client.initializeResult?.serverInfo;
        if (serverInfo) {
            log(`Connected to ${serverInfo.name} v${serverInfo.version}`);
        }

        logVerbose('LSP client started successfully');
    } catch (error) {
        log(`Failed to start LSP client: ${error}`);
        logVerbose(`Stack trace: ${error instanceof Error ? error.stack : 'N/A'}`);
        showError(`Failed to start LSP server: ${error}`);
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
        logVerbose(`Server path from settings: ${configPath}`);
        return configPath;
    }

    // Otherwise search PATH
    try {
        const pathResult = await which.default('slang-autos-lsp');
        logVerbose(`Server path from PATH: ${pathResult}`);
        return pathResult;
    } catch {
        logVerbose('Server not found in PATH');
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

// Richer response from server with diagnostics
interface ExpandResult {
    edit: WorkspaceEditResult;
    messages: string[];   // Informational messages
    warnings: string[];   // Warning messages
    errors: string[];     // Error messages
    autoinst_count: number;
    autowire_count: number;
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
