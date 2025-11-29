// SPDX-License-Identifier: Apache-2.0
import * as vscode from 'vscode';
import * as which from 'which';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext) {
    console.log('slang-autos extension activating...');

    // Find the server executable
    const serverPath = await findServerPath();
    if (!serverPath) {
        vscode.window.showErrorMessage(
            'slang-autos-lsp not found. Please set slang-autos.serverPath or add to PATH.'
        );
        return;
    }

    console.log(`Using slang-autos-lsp at: ${serverPath}`);

    // Server options - run the LSP server
    const serverOptions: ServerOptions = {
        run: { command: serverPath },
        debug: { command: serverPath },
    };

    // Client options - which documents to handle
    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'verilog' },
            { scheme: 'file', language: 'systemverilog' },
        ],
    };

    // Create and start the client
    client = new LanguageClient(
        'slang-autos',
        'slang-autos LSP',
        serverOptions,
        clientOptions
    );

    await client.start();
    console.log('slang-autos LSP client started');

    // Register the expand command
    context.subscriptions.push(
        vscode.commands.registerCommand('slang-autos.expandAutos', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) {
                vscode.window.showWarningMessage('No active editor');
                return;
            }

            // Save the document first to ensure we're working with latest content
            await editor.document.save();

            const fileUri = editor.document.uri.toString();
            console.log(`Expanding AUTOs in: ${fileUri}`);

            try {
                // Call the LSP server command
                const result = await client?.sendRequest('workspace/executeCommand', {
                    command: 'slang-autos.expandAutos',
                    arguments: [fileUri],
                });

                // Apply the workspace edit if we got one
                if (result && typeof result === 'object' && 'changes' in result) {
                    const edit = new vscode.WorkspaceEdit();
                    const changes = (result as any).changes;

                    for (const [uri, textEdits] of Object.entries(changes)) {
                        const docUri = vscode.Uri.parse(uri);
                        for (const textEdit of textEdits as any[]) {
                            const range = new vscode.Range(
                                new vscode.Position(textEdit.range.start.line, textEdit.range.start.character),
                                new vscode.Position(textEdit.range.end.line, textEdit.range.end.character)
                            );
                            edit.replace(docUri, range, textEdit.newText);
                        }
                    }

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
                console.error('Error expanding AUTOs:', error);
                vscode.window.showErrorMessage(`Error expanding AUTOs: ${error}`);
            }
        })
    );

    // Register the delete command (placeholder for now)
    context.subscriptions.push(
        vscode.commands.registerCommand('slang-autos.deleteAutos', async () => {
            vscode.window.showInformationMessage('Delete AUTOs not yet implemented');
        })
    );

    console.log('slang-autos extension activated');
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
