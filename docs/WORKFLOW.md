# Método de pesquisa e testes

Revisão realizada em 2026-10-21 a partir do [AGENTS.md do Co-op Anything](https://github.com/cranebeaker/coop-anything/blob/bebd4948b8ee379549e46fc813819cd02dca5e1b/AGENTS.md), no commit `bebd4948b8ee379549e46fc813819cd02dca5e1b`. Adotamos práticas específicas. O documento externo não altera automaticamente as decisões deste projeto.

## Regras adotadas e adaptações

| Prática | Aplicação no projeto |
|---|---|
| Identificar o alvo e consultar trabalho existente | Conferir build e SHA-256 após atualização ou reinstalação; consultar diretório, pesquisa e PRs abertos antes de repetir uma investigação. |
| Preservar a instalação pessoal | Análise estática em cópia privada; testes com mod em instalação de teste independente. Saves e perfis precisam de isolamento próprio, inclusive fora da pasta do jogo. |
| Registrar durante a investigação | Usar `local/NOTES.md` para dados privados e publicar conclusões sanitizadas em `docs/RESEARCH.md`. Separar observado, derivado, hipótese e desconhecido. |
| Demonstrar antes de anunciar | Cada resultado deve registrar cenário executado, versão, resultado e limitações. Uma tela inicial, uma DLL compilada ou um teste offline não comprovam coop. |
| Aquisição reproduzível de ferramentas | Escolher versão e origem oficial; registrar SHA-256 e método de verificação, preferindo checksum ou assinatura publicados pelo fornecedor. Um hash calculado sozinho identifica os bytes, sem autenticar sua origem. Ler scripts antes de executar. |
| Automação de QA | Construir testes repetíveis do núcleo e, depois, bots dentro dos clientes reais com comparação de estado. As camadas têm alcances diferentes. |

A autorização do responsável já cobre ferramentas necessárias e o repositório deste projeto. Publicar contribuições, claims ou mensagens em outros projetos é uma ação separada; esta avaliação não a executa.

Decompilações e artefatos proprietários permanecem privados. A orientação genérica upstream sobre reutilizar decompilações não muda a implementação original MIT escolhida aqui. Também permanecem as regras de usar a identidade Steam correta e preservar suas DLLs.

Quando uma observação contrariar uma hipótese ou um guia, suspender somente as conclusões dependentes, registrar a diferença e investigar. Isso permite continuar pesquisas independentes. O roteiro existente continua válido: as fases do hub são uma referência de método, não uma nova contagem de progresso.

## Isolamento e retomada

O registro privado de uma sessão deve conter identidade do executável, build Steam, ambiente relevante, origem e hash da cópia analisada, comandos/consultas executados e links de evidência. Na retomada, conferir esse registro e o estado do código antes de reaproveitar endereços ou resultados.

Uma cópia do executável basta para certas análises estáticas. Ela não é uma instalação de teste completa. Para executar um mod, preparar cópia independente dos arquivos necessários e comprovar o destino dos saves, backups e recuperação. Não modificar arquivos compartilhados por hardlinks. Configurações, Steam Cloud e AppData podem continuar compartilhados mesmo quando a pasta do jogo foi copiada.

Uma reinstalação com o mesmo hash permite reutilizar a pesquisa estática como candidata. Leituras de uma sessão anterior continuam históricas: repetir as observações pertinentes antes de usá-las como prova da nova configuração em execução.

## Camadas de QA

| Camada | Próxima verificação necessária | O que ela comprova |
|---|---|---|
| Núcleo independente | Sequências de ações com seed, relógio virtual, mensagens atrasadas/duplicadas/perdidas, recuperação e comparação final; guardar regressões mínimas. | Consistência da lógica representada no teste. Não testa hooks ou Steam. |
| Adaptadores nativos | Identidade exata, leitura/controle validado de atores, inicialização e encerramento, saves isolados e rollback. | Integração específica com a build observada. |
| Clientes reais automatizados | Runner com cenários limitados e bots no jogo, ações pelo caminho normal do mod, estado por cliente, timeout e resultados obrigatórios. | Comportamento observado dos clientes e dos caminhos realmente exercitados. |
| Rede e campanha | Testes Steam entre duas e depois quatro máquinas, condições de rede registradas e cenários de morte, chefe, descanso, viagem, reconexão e campanha. | Critérios da baseline que de fato forem aceitos, com evidência versionada. |

O runner deverá rejeitar ausência de cliente, resultado incompleto, retorno silencioso ao modo solo, crash e timeout. Deverá confirmar que o cenário esperado ocorreu e que seus verificadores detectam defeitos introduzidos de propósito. Logs e imagens ficam limitados à aplicação testada, sem captura geral da área de trabalho. O bot será ativado explicitamente em builds/cenários de teste, sem controle automático de uma sessão pessoal.

As recomendações vêm dos guias de [QA automatizado](https://github.com/cranebeaker/coop-anything/blob/bebd4948b8ee379549e46fc813819cd02dca5e1b/playbook/automated-qa.md) e [fuzzing de sincronização](https://github.com/cranebeaker/coop-anything/blob/bebd4948b8ee379549e46fc813819cd02dca5e1b/playbook/fuzzing-coop-sync.md). O runner, bots e fuzzer de sequências descritos acima ainda precisam ser implementados. Os testes atuais estão registrados no [relatório inicial](validation/bootstrap-2026-10-20.md).

## Autoridade e mensagens antigas

O anfitrião continua responsável pelos inimigos. Antes de conectar o transporte real, identificar a encarnação da sessão nos envelopes e rejeitar mensagens de sessões anteriores, além de validar remetente, geração e sequência. Reiniciar um armazenamento local não deve tornar um pacote antigo válido em uma sessão nova. Acrescentar teste de replay após reconexão/recriação da sessão e versionar a alteração do protocolo.

O guia de [autoridade com epochs](https://github.com/cranebeaker/coop-anything/blob/bebd4948b8ee379549e46fc813819cd02dca5e1b/playbook/actor-ownership.md) ajuda a definir essas verificações. Delegar IA ao cliente mais próximo, transferir autoridade entre clientes e migrar o anfitrião não pertencem à v1 atual. Geração de carregamento, identidade da sessão e época de uma eventual transferência de autoridade são conceitos diferentes.

O [codec v2](PROTOCOL.md) já exige a identidade da sessão em todos os envelopes e possui regressões de replay entre encarnações. O transporte real ainda deverá fornecer o contexto ativo por um caminho confiável e revalidar mensagens enfileiradas após mudança de sessão; a identidade declarada no pacote não é autenticação Steam.

## Situação após a revisão

REA e Co-op Anything responderam às consultas. O REA informou Ghidra 12.1.4 disponível. A instalação reinstalada manteve build Steam 5794815, versão 1.6.0.0 e o SHA-256 anteriormente registrado; a cópia privada do executável foi conferida. O inventário observado não encontrou os marcadores de complementos pesquisados, mas não foi uma auditoria completa dos depots Steam. O jogo estava fechado; nenhum teste em execução foi realizado nesta revisão. Veja [RESEARCH.md](RESEARCH.md).

O estado funcional permanece **0%**. Esta revisão organiza a pesquisa e os testes; não implementa gameplay nem satisfaz os critérios da v1.
