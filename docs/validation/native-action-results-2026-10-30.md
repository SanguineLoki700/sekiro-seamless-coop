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

## Rodada estrita e revisão da prova de parada

O candidato `b77c6b903f65900a1838b3992403e8878b72723a` comprovou o novo binding de resultados em execução: classificou a entrada agachada e o repouso, aguardou estabilização e rejeitou o controle negativo. Consumiu 62 de 65 comandos e registrou 0,951 m de avanço antes de parar com `movement_not_released`, ainda antes do ataque. Uma criação e um posicionamento inicial; zero escritas manuais no grafo, comandos legados, eventos manuais ou alterações de entrada do principal. Saída normal, código 0; saves pessoais e backup reconferidos e intactos. Esse teste **não passou**.

O histórico imutável do próprio candidato registra comandos neutros consumidos em toda a liberação. O deslocamento tridimensional residual foi aproximadamente 0,300 m desde o primeiro neutro até o repouso. A categoria permaneceu agachada; a flag efetiva de repouso surgiu aproximadamente 1,2 segundo após o neutro e coincidiu com a posição estacionária nas amostras posteriores. O prazo antigo de 500 ms terminou enquanto essa transição ainda estava ativa. O registro demonstra a separação entre soltar a entrada e terminar a parada física; não autoriza tratar qualquer movimento residual como correto.

A revisão 2 mantém consumo/frescor/propriedade e substitui aquele critério por duas condições explícitas: até 0,4 m de deslocamento residual e 2 segundos para a transição; depois repouso nativo com 250 ms contínuos de posição estável dentro de 0,05 m, em três dimensões. Os limites são da prova local controlada, não parâmetros gerais de combate. O orçamento ativo passa a 12 segundos; o limite total de 4 m continua. Deslocamentos durante lacunas de consumo permanecem no máximo acumulado. Não há correção de posição. Novos testes simulados aceitam uma parada gradual limitada e rejeitam excesso de deslocamento, ausência de repouso, deriva horizontal/vertical após repouso e janelas interrompidas. A revisão precisa de nova execução; a falha anterior permanece registrada.
