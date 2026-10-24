# Criação de personagens: tabela, atualização e tipos

Pesquisa de **24/10/2026**, uma pessoa e uma máquina, na mesma identidade Steam
1.06/build 5794815 do [primeiro registro](ACTOR_RESEARCH.md). SHA-256 do jogo:
`637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.

**Resultado:** a tabela do criador de debug foi observada em execução, seu
caminho de atualização foi exercitado pelo próprio jogo, e a análise encontrou
um caminho distinto para construir `PlayerIns` em registros adicionais.
Nenhuma função de criação foi invocada pela pesquisa. Não há segundo personagem
criado nem controle remoto comprovado. `actors-spawn` continua em andamento,
com zero pontos; o total da v1 permanece **10%**.

## Nova sonda reproduzível

Fonte testada: `137c21556ce8800879ac66c86b47a0aa18b6e1e6`.

```text
skcoop_probe --exe <executável da instalação de teste aprovada> --creator
```

O modo confirma `WorldChrManImp`, `CSDebugChrCreator`, ajuste zero, vtable exata
e os dois destinos virtuais selecionados. Lê estado/pedido e um prefixo limitado
de quatro pares de ponteiros. Aceita esse prefixo dentro do módulo ou fora dele
sob limites de endereço/página; a classificação `heap` não comprova propriedade
da alocação. Ponteiros não nulos de callback precisam de página executável e
legível na imagem; labels precisam de página legível na imagem. Strings não
são lidas.

Links, tipos, estados, tabela e destinos são relidos antes do resultado; mudanças
ou leituras incompletas causam recusa. O processo é revalidado quanto à vida útil
e encerramento. Os limites adicionais continuam em **512 bytes de heap e
4.096 bytes de módulo**, sem endereços do heap na saída. Identidade incompatível
é recusada antes de consultar processos. Os modos padrão, `--rtti` e `--actors`
permanecem separados.

No teste protegido, a sonda passou com **80 bytes de heap e 880 do módulo**.
Estado e pedido eram ambos zero. A tabela estava no módulo em RVA `0x3d88050`:

| Índice observado | Callback RVA | Label RVA / identificação separada |
|---|---|---|
| 0 | `0xc0ff70` | `0x2a94af8`, literal `CSDebugChrCreator::STEP_Idle` |
| 1 | `0xc0ff80` | `0x2a94b38`, literal `CSDebugChrCreator::STEP_Load` |
| 2 | `0xc10010` | `0x2a94b80`, literal `CSDebugChrCreator::STEP_LoadWait` |
| 3 | Nulo | `0x3d880b0`, significado desconhecido |

Os três nomes foram lidos separadamente da cópia privada exata. O quarto par
não estabelece um quarto estado invocável nem a extensão completa da tabela.
Uma busca anterior das partes presentes no arquivo não encontrara pares
qualificantes; a leitura em execução resolveu a associação, sem transformar
a busca estática negativa em prova de ausência.

## Atualização observada no jogo

O código selecionado em `0xa509f0` acessa o criador em `WorldChrMan+0x3148` e
chama seu slot virtual `+0x10`. O destino `0xc0f790` encaminha ao slot `+0x48`,
cujo consumidor é `0xc0f7a0`.

Uma observação REA 6.7.0 / Frida 17.23.2 de oito segundos registrou **480 entradas
em cada uma dessas duas rotinas**, em um mesmo thread observado. As quatro
amostras iniciais do consumidor tinham endereço de retorno `0xa50a31`, coerente com a
chamada encontrada. Foram emitidos oito eventos de entrada; contagens finais
incluem as demais entradas. O término por duração foi registrado em 8.039 ms.

As âncoras de 16 bytes de ambas as entradas foram conferidas antes da instalação
dos listeners. Isso valida essas âncoras, não o hash integral de cada função.
`Interceptor.attach` altera temporariamente a instrumentação do processo;
esta parte não é uma observação sem escrita de memória. Não houve substituição
de comportamento, alteração de argumentos/retornos, leitura de argumentos ou
conteúdo do heap nesse cenário, nem chamada de função do jogo pelo observador.

O resultado teve zero erros de callback, listeners destacados, host com saída0
e jogo vivo após a coleta. O fechamento separado da sessão REA não registrou
erros e confirmou sua liberação. Evidência:
`ev_d7a96e8b6bb6b1b05c951c0d77df33195a79e4661d34e5d94196a0f02900fd27`.
O thread observado nesta sessão não é um identificador fixo para sessões futuras.

## Ramo de debug e ramo de jogador adicional

O código analisado do fluxo de debug prepara recursos em `0xc0ff80`, espera a conclusão
em `0xc10010` e alcança `0xa492e0` / `0xa075d0` / `0xa05550`. O construtor
`0xa11f60` grava a vtable `0x2a27f28`, identificada independentemente como
`EnemyIns`. A alocação desse ramo é `0x2090` bytes. Isso não comprova um avatar
de jogador remoto, mesmo que use um modelo conhecido.

Também encontramos construção de `PlayerIns`: o construtor `0xa217a0` grava a
vtable `0x2a2b338`. O wrapper `0xa05a80` aloca `0x2290` bytes para ele. Esses
vínculos vêm de stores explícitos nas instruções e de RTTI/COL independentes,
sem inferência por proximidade de endereços ou tamanho.

| Caminho analisado | Observação relevante | Estado |
|---|---|---|
| `0xa058a0` | Constrói `PlayerIns` e grava `WorldChrMan+0x88`. | Caminho do primary; não habilitado para criar um convidado. |
| `0xa07960` | Percorre a coleção em passos de `0x38`, começando pelo índice1; procura registro nulo e chama `0xa05a80`. | Candidato para registro adicional; ainda não invocado. |
| `0xa49460` | Encaminha `WorldChrMan+0x40` ao caminho anterior e pós-processa o resultado. | Wrapper candidato; contrato e execução ainda pendentes. |

O laço que começa em1 é uma evidência de seleção de registros adicionais. Não
prova que todo o restante da construção preserve o jogador local, carregue o
modelo ou produza um personagem visível. Não trocamos construtores, tamanhos,
ponteiros ou o primary em execução.

O helper `0x7f8480` escolhe um manipulador pelo campo `config+4`. Os seguintes
vínculos foram corroborados por stores de vtable nos construtores e RTTI:

| Valor observado no código | Construtor RVA | Tipo vinculado |
|---|---|---|
| 0 | `0x7e3220` | `ChrManipulator` |
| 1 | `0x7ed3a0` | `PadManipulator` |
| 2 | `0x7ebab0` | `NetworkManipulator` |
| 3 | `0x7f4350` | `ReplayManipulator` |
| 5 | `0x7e9f10` | `NetAIManipulator` |

São manipuladores, não a família distinta `ChrCtrl`/`PlayerCtrl`. Os nomes não
estabelecem controles funcionais ou uma sessão de rede. Nenhum desses valores
foi escolhido e enviado a um construtor pela pesquisa.

## Parâmetros de carregamento ainda sem prova

Uma segunda observação, limitada a 55 segundos, procurou chamadas de
`0xa493d0` e `0x7f8730` para conferir os argumentos normais. Ela terminou por
duração com **zero eventos**, sem erros e com cleanup confirmado. Portanto é
**inconclusiva** para os parâmetros de criação. Não demonstra ausência das
rotinas, uma criação bem-sucedida ou valores padrão utilizáveis. Evidência:
`ev_de8e55752b1f3fb14857e6d88db983f2f482c81f40ac85c028874d201cf326b4`.

Não inferimos que uma ação manual ocorreu dentro da janela a partir da simples
passagem de tempo. Os próximos testes precisam atingir o caminho real de
construção e registrar seus argumentos antes de invocar o wrapper adicional.
O responsável confirmou posteriormente a volta à tela inicial e a recarga;
essa confirmação não atesta o horário da ação dentro da janela de captura.

## Proveniência, verificações e próximo gate

As coletas usaram a instalação independente, o lançador/runtime já aceitos e
o perfil de teste `.co2`, com proteção externa dos originais. Não houve nova
DLL instalada. Leituras selecionadas revalidaram caminho, hash, módulo, PE,
vida do processo e páginas; bytes e releituras ficaram sob orçamento de4 MiB
por coleta. Todos os artefatos proprietários e recibos brutos permanecem privados.

Ao final, o responsável confirmou saída pelo menu e retirada da opção da Steam.
O host registrou saída0, sem timeout, com saves originais e configuração gráfica
inalterados. A ausência do jogo/lançador, o campo vazio da Steam e os hashes
originais foram conferidos separadamente. Não houve restauração de saves nem
sessão de observação deixada ativa.

REA/Ghidra 12.1.4 analisaram carriers privados de trechos retidos. O carrier de
código atual tem SHA-256
`7653581d177ff4d4613f29f59a56a05256db4d869af2accff14a22bedfa58c7d`.
Não inclui uma imagem completa, `.rdata`, imports ou unwind original. Os tipos
e assinaturas da decompilação são inferidos; exports criados pelo analista não
validam ABI. As entradas de unwind também podem ser fragmentos. O carrier nunca
foi executado ou carregado como DLL.

Evidências de análise do alvo atual:

- `ev_fcc02e77bf735db11991679198b162b572f58979ce04f535b39edff4ff772b3f`: wrapper adicional.
- `ev_93a15a9af6f72622b14a43d8ba5300ce4a8f8e43bee318b0ce756dc9cea7d7fd`: seleção a partir do índice1.
- `ev_9a57e0bb831dac5b99bdec6cb0f654f4ae81aa09f70866bebe1f2d8b4a8f631f`: construtor `PlayerIns`.

MSVC Release, **13/13 conjuntos CTest**, **26/26 testes Python** e conferência
do ledger passaram. As regressões novas exercitam limites do prefixo, overflow,
permissões executáveis/legíveis e recusa de CLI/identidade incompatível.

O próximo gate é confirmar parâmetros, recursos, configuração do manipulador
e ciclo de remoção para uma tentativa local limitada de `PlayerIns` adicional,
executada no caminho de atualização observado e com comparação do primary
antes/depois. A aceitação de `actors-spawn` continua exigindo duas instalações,
duas máquinas e um ator remoto visível/controlável. Esta pesquisa não a aceita.
