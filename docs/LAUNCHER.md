# Lançador e inicialização protegida

O lançador do projeto detecta a instalação Steam e oferece um caminho explícito
de integração, com saves separados e a DLL carregada antes da inicialização do
Sekiro. Esse caminho foi validado localmente na build 5794815; veja o
[relatório](validation/core-launcher-2026-10-23.md). Ainda não existe multiplayer
jogável. O comando `--launch` continua recusando gameplay.

O [critério de compatibilidade do runtime](validation/native-runtime-2026-10-23.md)
também foi aceito para a build identificada. A distribuição com `.exe` aberto
por dois cliques e pasta de suporte continua sendo a experiência final planejada;
o procedimento de desenvolvimento abaixo ainda exige a entrada pela Steam.

## Instalação identificada

`--find-game` lê `SteamPath` do usuário atual, `libraryfolders.vdf` e o manifesto
do AppID 814380. Seleciona a pasta indicada pelo manifesto, exige instalação
completa e recusa resultados ambíguos. Não escolhe automaticamente uma cópia
vizinha nem altera metadados da Steam. Os parsers têm limites de tamanho,
profundidade e quantidade de campos.

A integração aceita somente Steam 1.06 / produto 1.6.0.0 / build 5794815,
executável AMD64 de 68.005.144 bytes e SHA-256
`637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
Alterar um INI de perfis não amplia essa compatibilidade compilada.

## Configuração de desenvolvimento

O teste exige uma instalação completa **independente**, em outra pasta dentro
do mesmo `steamapps/common`, e a instalação original preservada. Não usa
hardlinks, proxy loader ou substituição da DLL Steam. As duas instalações
mantêm seus arquivos; a DLL do projeto fica ao lado do lançador, fora delas.
O `steam_appid.txt` já preparado na cópia deve conter o AppID correto, 814380.

Copiar [launch.example.ini](../config/launch.example.ini) para
`config/launch.ini`, ao lado do lançador, e preencher os quatro valores:

| Campo | Significado |
|---|---|
| `test_executable` | Caminho absoluto do `sekiro.exe` na cópia independente. |
| `coop_profile_root` | Raiz alternativa de roaming, com `Sekiro/conta` já preparada. |
| `account` | Nome decimal exato da pasta da conta escolhida. |
| `receipt_directory` | Diretório existente e separado para registros locais. |

O INI usa UTF-8, não expande variáveis e não aceita aspas nos valores.
Caminhos devem ser absolutos, locais, sem separador final, aliases ou componentes
ambíguos. A raiz alternativa precisa caber no contrato `MAX_PATH` da API AppData.
Esta versão exige os dois arquivos normais existentes, `S0000.sl2` e
`S0000.sl2.bak`, para protegê-los. Não escolhe contas, importa progresso ou
restaura arquivos automaticamente.

```powershell
.\skcoop_launcher.exe --find-game
.\skcoop_launcher.exe --check-launch .\config\launch.ini
```

`--check-launch` não cria processo, carrega DLL ou grava saves. Confere caminhos
físicos, arquivos privados existentes, disponibilidade do lease e bloqueios dos
originais, sem criar um lease ausente. A inicialização repete as verificações;
uma inspeção bem-sucedida não reserva o perfil para uma execução futura.

O executável original e o de teste são medidos e fixados por handles. As sete
dependências nativas selecionadas, incluindo `steam_api64.dll`, são comparadas
por SHA-256 com a instalação original e mantidas abertas sem permitir alteração.
Os cinco pares `Data*.bdt/bhd` precisam ser arquivos independentes com tamanhos
iguais. Isso **não** equivale à verificação criptográfica completa dos depots
Steam, filmes e áudio. A origem dos arquivos da instalação continua relevante.

## Entrada pela Steam

O teste é iniciado pelas Opções de inicialização do Sekiro na própria Steam:

```text
"caminho absoluto do lançador" --steam-start %command%
```

O comando exato é exibido por `--check-launch`. Uma configuração alternativa
pode ser fornecida entre `--steam-start` e `%command%`. O lançador exige o
contexto Steam do AppID 814380 e valida o comando original contra a instalação
detectada. Argumentos extras são recusados. Não tenta contornar autenticação,
modificar DRM ou lançar silenciosamente a instalação original se falhar.

O host abre os saves normais e a configuração gráfica existente somente para
leitura, com compartilhamento zero, durante toda a execução modificada. Compara
os hashes antes/depois. Cria somente o filho de teste suspenso, fornece o payload
e exige confirmação `READY`, vinculada a versão, PID e nonce. A DLL valida a
identidade, abre o armazenamento privado e instala a transação completa de hooks
antes de liberar TLS e o código inicial do jogo.

Um job Windows encerra somente o filho criado se o host desaparecer. Os bloqueios
dos arquivos originais também são duplicados para esse filho antes do primeiro
resume. Na execução normal, um Sekiro externo ou uma falha de monitoramento
mantém os bloqueios no host até o encerramento manual da instância externa;
ela nunca é terminada pelo lançador. Essa guarda não controla outros programas
se o usuário matar forçadamente o próprio host.

Há limite de inicialização de 60 segundos e de execução de 30 minutos para este
fluxo de teste. A DLL e o controlador ficam válidos até o término do processo;
descarregamento durante o jogo é recusado. Os registros JSONL são locais e
contêm dados privados. Uma saída normal ou `READY` sozinho não aceita um
cenário de gameplay. Contadores só representam as operações medidas; os campos
por operação ainda podem permanecer sem medição.

## Retorno ao jogo normal

Fechar o jogo pelo menu, aguardar o host terminar e deixar as Opções de
inicialização vazias. A Steam volta a executar o jogo original. O teste local
confirmou esse retorno, sem a DLL do projeto, com o personagem normal carregado.
Não há arquivos do mod para remover da instalação original.

O diagnóstico `--diagnose` continua disponível em um processo comum, sem hooks.
A DLL recusa ser carregada em um processo chamado `sekiro.exe` sem o payload
de inicialização. Os testes usam exclusivamente executáveis próprios; nenhum
hash de fixture é admitido no runtime distribuído.
