# Integração nativa de saves

O adaptador Windows conecta a política de caminhos ao `CoOpSaveStore` e devolve handles reais do sistema ao jogo. Na build Steam identificada, os testes locais confirmaram criação, carregamento, gravação e recuperação de saves `.co2` em uma instalação independente. O [relatório de aceitação](validation/native-save-isolation-2026-10-22.md) registra os cenários e seus limites.

O lançador público permanece diagnóstico. O bootstrap público de testes aceita somente seu executável-fixture e sua marca exportada; o teste de Sekiro usou um iniciador privado, com identidade exata, perfil preparado e proteção adicional dos arquivos solo. Ainda falta integrar esse fluxo ao pacote público. O runtime diagnóstico e `game_profiles.ini` mantêm suas capacidades de gameplay desativadas.

## Vinculação e caminhos

`NativeIoBinding` copia a configuração, o contexto do processo e uma conta. O chamador estabelece opt-in, PID e hash medido antes da instalação. O adaptador verifica o PID atual, a política e a correspondência das raízes do controller. Esses parâmetros não autenticam uma conta Steam.

`decide_native_save_path` reconhece `S0000.sl2[.bak]` na origem e no perfil realocado, além de `S0000.co2[.bak]` já no destino. Conserva conta e tipo primário/backup. Nomes canônicos de save fora das duas árvores, caminhos candidatos relativos ou ambíguos, outra conta e pares mistos de cópia são recusados. Uma recusa não tenta o caminho solo.

`SHGetFolderPathW(CSIDL_APPDATA)` passa a devolver a raiz privada preparada, inclusive para outras chamadas desse processo que usem essa API. As três pastas exatas — raiz, `Sekiro` e conta — possuem handles fixados. Consultas de atributos e `CreateDirectoryW` reconhecem essas pastas existentes. O adaptador não cria uma hierarquia por descoberta automática.

## I/O e handles

- `CreateFileW` e `CreateFileA` redirecionam arquivos selecionados por meio de `open_native`. O jogo conserva seus próprios `ReadFile`, `WriteFile`, seek, flush e codec; um bloco de escrita não é tratado como save completo.
- O contrato dos arquivos aceita acesso zero ou `GENERIC_READ`/`GENERIC_WRITE`, compartilhamento read/write sem delete, atributos normais e as cinco disposições Win32. A combinação de backup-semantics com acesso zero também é aceita. Security/template não nulos, I/O overlapped, delete-on-close e demais opções não implementadas falham antes do acesso.
- As pastas exatas aceitam somente abertura de metadados: acesso zero, `OPEN_EXISTING`, atributos normais com backup-semantics, security/template nulos e sem share-delete. O handle devolvido aponta à pasta privada. A abertura verifica reparse point, caminho canônico, volume e ID contra o handle fixado. Acesso de dados ou escrita de diretório permanece recusado.
- `GetFileAttributesW`/`ExW`, `DeleteFileW` e `CopyFileW` usam o mesmo binding. A cópia fixa as identidades dos arquivos e utiliza a operação Win32 real sob bypass interno restrito. Uma cópia malsucedida pode ter alterado um destino existente; `destination_may_be_modified` registra essa possibilidade. Ela não é um commit atômico com rollback.
- Find/move/replace/CopyEx e variantes ANSI não implementadas recusam caminhos selecionados. Arquivos não relacionados seguem a API original, preservando seu contrato e o erro de entrada antes do encaminhamento.

Os controllers e pins devem sobreviver a todos os handles devolvidos. Operações nativas não esperam pelo mutex do store: contenção resulta em sharing violation. Isso evita uma espera específica em callbacks do loader, sem demonstrar segurança de qualquer operação arbitrária sob loader lock.

## Inicialização e dependência

As 27 interceptações entram em uma transação Detours. A instalação é feita no processo primordial, antes dos callbacks TLS e inicializadores da aplicação. Somente a thread atual é alistada. Não há suporte comprovado a anexar os hooks a um jogo já em andamento ou descarregar a DLL com chamadas concorrentes.

Detours 4.0.1 é incluído com licença MIT, commit upstream e manifesto dos bytes originais dos Git blobs em `third_party/detours`. O manifesto identifica os arquivos incorporados; ele não constitui assinatura independente do publicador.

## Alcance

Os testes-fixture verificam início antes de TLS/global/main, perfil novo e semeado, offsets distintos, preservação de bytes, cópia/remoção/recuperação, abertura ANSI, consultas de pasta e recusas de APIs/contas/caminhos. São separados dos testes humanos em Sekiro.

Esta fronteira cobre as APIs implementadas e os caminhos exercitados. Não é um sandbox geral de filesystem nem uma interceptação de todas as chamadas NT, variáveis de ambiente ou APIs de pastas conhecidas. Steam Cloud, interrupção de energia, execução hostil, outros mods, novas builds e campanha multiplayer exigem verificações próprias. Os testes do jogo mantiveram também os dois arquivos solo bloqueados por um controller externo até o encerramento da cópia testada.
