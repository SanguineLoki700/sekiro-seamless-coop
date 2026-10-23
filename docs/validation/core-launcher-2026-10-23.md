# Validação local do core-launcher — 2026-10-23

Resultado: **core-launcher aceito**, um participante e uma máquina Windows 11.
Implementação testada: `d48d46911f78c497a7bb4b050bdabed626a892f9`.
SanguineLoki700 executou e confirmou as ações no jogo; Codex verificou arquivos,
processos, DLL carregada e registros. A aceitação soma somente os 3 pontos desse
critério. Compatibilidade geral do runtime, atores e multiplayer continuam
separados. O isolamento de saves já aceito não recebe novos pontos.

## Identidade e configuração

- Sekiro Steam 1.06 / produto 1.6.0.0 / build 5794815 / AppID 814380.
- Executável AMD64, 68.005.144 bytes; SHA-256
  `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
- Instalação original descoberta pelo registro e manifestos Steam. Cópia
  completa independente em outra pasta do mesmo `steamapps/common`.
- Perfil `.co2` separado, da mesma conta, previamente validado no
  [ciclo de saves](native-save-isolation-2026-10-22.md). Originais `.sl2` e backup
  protegidos por handles exclusivos durante toda a execução modificada.
- Launcher e DLL compilados no repositório, copiados para uma pasta local fora
  das instalações. Bytes copiados conferidos com os artefatos testados.
- DLL testada SHA-256
  `7a5bacfb4513df04583015b4f621cb217b4b3cf5d476bb2dabf2556932bd5bf2`.

## Cenário e resultados observados

1. Com jogo fechado, `--find-game` identificou a instalação original e o hash
   esperado; `--check-launch` aprovou a configuração real sem criar processo.
2. O participante configurou a opção do novo lançador na Steam. Foi conferido
   somente o campo do AppID selecionado antes de solicitar a abertura pela Steam.
3. O lançador criou a cópia suspensa e recebeu `READY`, fase 3, erro fatal 0,
   máscara 1 da transação de hooks de saves. Após dez segundos o mesmo processo
   estava vivo, com janela respondendo. A enumeração dos módulos confirmou
   `skcoop_runtime.dll` da pasta local, dentro do processo da cópia independente.
4. O participante escolheu Continuar, confirmou o personagem carregado e fechou
   pelo menu. O host recebeu saída 0, sem timeout, processo estrangeiro ou falha
   de monitoramento. Os hashes completos dos dois arquivos originais e da
   configuração gráfica permaneceram iguais aos medidos antes desse teste.
5. O participante apagou a opção. O campo vazio foi conferido. A Steam abriu
   o executável da instalação original; nenhum módulo `skcoop*` estava presente.
   O participante confirmou Continuar e fechou normalmente. A saída foi
   conferida no registro de processos da Steam.

O registro da execução modificada recebeu snapshot no encerramento: 141
operações redirecionadas, cinco consultas AppData redirecionadas, 28 consultas
de diretório, nenhum erro de cópia e um `PROCESS_DETACH`. O contador agregado
`denied` foi 4 e inclui falhas ordinárias de API; ele não permite classificar
isoladamente essas quatro operações. Não se afirma ausência total de falhas
de I/O nem rastreamento completo por operação.

O recibo privado de referência é
`game-test-69ca1169de308673aa65696d62d593cc.jsonl`. Preparação, observação do
processo e medições permanecem em `local/core-launcher/`; saves, caminhos de
conta e logs brutos não são publicados. Após abrir normalmente, os saves solo
podem mudar legitimamente; nenhum hash antigo foi restaurado.

## Testes independentes

MSVC x64 Release compilou sem avisos na execução registrada. CTest: **13/13**
conjuntos aprovados. Python: **26/26** testes do ledger aprovados.

| Verificação nova | Resultado e alcance |
|---|---|
| Descoberta Steam | 12 casos: bibliotecas modernas/legadas, UTF-8, ambiguidades, instalação incompleta, caminho inválido e limites do parser. |
| Configuração de launch | 11 grupos: quatro campos exatos, conta canônica, Unicode, limites, entradas inválidas e leitura sem escrita. |
| Vida do filho | 2 cenários: fechamento do job e término abrupto do host encerram seu filho; fixture independente permanece viva. |
| Guarda do runtime | 13 casos: diagnóstico comum, recusa sem payload no executável próprio renomeado, payloads inválidos e identidade incompatível. As 11 recusas por injeção exigem `0xc0000142` e ausência do marcador TLS. |

Os testes de bootstrap precoce e de saves existentes também passaram. Fixtures
controladas não substituem o teste real acima, nem admitem versões extras do
jogo. Nenhum binário de Sekiro é necessário no CI público.

## Limitações e estado final

Este é um caminho explícito de integração pela Steam, com instalação de teste
preparada e proteção externa dos originais. Não é ainda uma distribuição
multiplayer pronta para instalação. Não foram aceitos Steam Cloud, outras
contas, versões, slots ou mods. A conferência dos grandes arquivos de dados é
de independência física e tamanho, sem validar todos os hashes dos depots.

O Ghidra não foi necessário para esta integração de código já conhecido. O jogo
e o lançador terminaram; a opção Steam ficou vazia. Nenhum binário do projeto
foi instalado na pasta original do Sekiro. Perfil e cópia de teste foram
preservados para a próxima etapa.
