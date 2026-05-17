import {
  createConnection,
  TextDocuments,
  ProposedFeatures,
  CompletionItemKind,
  TextDocumentSyncKind,
  InitializeParams
} from 'vscode-languageserver/node';
import { TextDocument } from 'vscode-languageserver-textdocument';

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

connection.onInitialize((params: InitializeParams) => {
  connection.console.log('Coda LSP: onInitialize called');
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      completionProvider: { triggerCharacters: [' ', '.', ':'] }
    }
  };
});

const KEYWORDS = ["if","while","for","return","enum","union","struct","fn","module","include","int","int8","int16","int32","int64","uint","uint8","uint16","uint32","uint64","char","string","none","type","null","mut"];

connection.onCompletion((params) => {
  connection.console.log('Coda LSP: onCompletion called at position ' + JSON.stringify(params.position));
  return KEYWORDS.map((k) => ({
    label: k,
    kind: CompletionItemKind.Keyword
  }));
});

connection.onCompletionResolve((item) => {
  item.detail = 'Coda keyword';
  item.documentation = `Docs for ${item.label}`;
  return item;
});

documents.onDidOpen((e) => connection.console.log('Coda LSP: document opened: ' + e.document.uri));
documents.listen(connection);
connection.listen();
