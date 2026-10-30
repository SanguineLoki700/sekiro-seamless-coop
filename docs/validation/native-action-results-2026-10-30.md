# Estados efetivos do controlador — 30/10/2026

Alvo: Sekiro Steam build 5794815 / 1.06, AMD64, SHA-256 `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`. Um PC, instalação independente e perfil co-op separado. Progresso permanece **10%**.

## Observado

A execução anterior do adaptador C++ foi interrompida na fase neutra por deslocamento durante a entrada nativa no mapa. Esse resultado não passou. A coleta somente de leitura encontrou a transição da entrada para repouso agachado; carregar o modelo, sozinho, não comprova estabilidade física.

A reflexão e o RTTI do artefato selecionado forneceram os campos e tipos candidatos do grafo, máquinas de estado e descritores de estado. Uma observação de execução validou esses tipos e o vínculo ao comportamento do convidado. Não foram assumidos layouts de outra versão do middleware.

Em seguida, duas sequências privadas e limitadas forneceram somente níveis virtuais de botões ao controlador nativo exclusivo do convidado. O jogador principal ficou independente; não foram emitidos eventos HKB manualmente, chamadas manuais de funções do jogo, escritas no grafo ou correções de posição. Um observador separado, somente de leitura, acompanhou os resultados efetivos:

- Ataque do chão, sua soltura e recuperação para repouso.
- Entrada na defesa, defesa sustentada e saída para repouso.
- Na sequência mais longa, o estado de defesa sustentada chegou a **2,529 segundos** antes da saída, sob um comando mantido por 3 segundos.
- Filhos antigos de ataque e defesa continuaram ativos durante algumas transições. A categoria canônica da ação separou esses resíduos de uma ação atual.

Evidências REA: `c08ab4e844ffe8ed31e4566a8d0c7b83fd74871d2b122c95165ef310a6f6be06` (entrada/repouso), `29f00abad70959d4c91f853e88ee81b47a929806b66d6c2a5d12477974d6fc3b` (primeira sequência), `6ee2045497303c893e8d1b7d2cda25885920af7ec1b35ea2eb8483dd6217a082` (sequência longa). Os registros integrais, metadados, endereços e scripts ficam privados. A saída do jogo foi normal, código 0, sem timeout; saves pessoais e backup foram reconferidos independentemente e ficaram intactos, assim como a configuração gráfica.

## Implementação resultante

O candidato acrescenta um leitor de resultados somente de leitura, ligado ao grafo próprio do ator e limitado por tipos, tamanhos e orçamento. A categoria canônica governa a classificação; entrada de defesa não conta como sustentação, resíduos de mistura não contam como ações atuais e uma categoria desconhecida não é repouso. A preparação espera repouso real antes de armar comandos. A prova C++ passa a ser estrita e exige recuperação explícita após ataque e defesa.

Testes simulados cobrem mistura de estados, entrada, sustentação, recuperação, invalidação de cache, ambiguidade e dados malformados. O cenário rejeita ausência de recuperação e entrada tardia na defesa, sem ampliar os limites de frescor ou consumo. Esses testes verificam a implementação e o verificador; não substituem a execução no jogo.

## Limites

A pesquisa comprova estados nativos de ação e recuperação, não contato, bloqueio eficaz, dano, postura, parry ou combate compartilhado. Não valida retroativamente a sequência C++ anterior. A execução estrita do novo candidato ainda precisa passar antes de concluir a prova do controlador. Comandos pelo TCP existente, dois jogos reais e quatro máquinas continuam pendentes. Não há novos pontos, release pública ou aceitação de combate nesta entrega.
