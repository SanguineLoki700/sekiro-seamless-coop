# Base LAN e preparação — 2026-10-25

**Resultado: implementação e testes independentes concluídos; validação nativa LAN e duas máquinas pendentes.** Este relatório não aceita critérios da v1 e não altera os 10% verificados.

| Verificação realizada | Resultado | Limite |
|---|---|---|
| Release MSVC x64 | Compilou lançador, DLL, verificador e fixtures | Não executa Sekiro |
| CTest | 20/20 conjuntos passaram | Inclui políticas com adaptadores falsos |
| Dois executáveis de verificação separados | Troca bidirecional real em loopback e dois relatórios obrigatórios | Uma máquina, nenhum jogo |
| Código de sala incorreto nos dois executáveis | Ambos falharam | Não é auditoria de segurança completa |
| Codec, sessão e sockets | Corrupção, truncamento, replay, outra conexão/sala, propósito incompatível, ausência, perda e mudança de geração rejeitados | Rede local controlada |
| Política do personagem remoto | Sem criação na ausência de peer, sem fallback local, release de movimento vencido, parada em troca de identidade e criação única | Adaptador falso |
| Preparador na instalação do proprietário | Perfil novo e configuração criados; `.sl2` e backup preservados por hashes antes/depois | Preparação com jogo fechado |
| Preflight do lançador no perfil gerado | Exit0; identidade, cópia, isolamento e locks passaram | Somente inspeção, nenhum jogo iniciado |
| Ledger | 26 testes Python passaram; 10% válido | Não concede critérios de multiplayer |

O teste de processos exige ambos os resultados. Um verificador isolado, um relatório faltante ou um código de sala danificado não substitui uma dupla concluída. Relatórios omitem o código e declaram `native_game_test=false`.

O preparador reutilizou a instalação de teste independente já presente no PC e verificou arquivos pequenos por conteúdo e arquivos grandes por comprimento, conforme o lançador. A primeira cópia completa em uma instalação nova do notebook ainda precisa ser exercitada. Nenhum arquivo da instalação original foi alterado; nenhum jogo foi iniciado nesta validação de preparação.

Uma fixture de peer roteirizado está disponível apenas para validar um Sekiro real contra outro processo. Ela não faz parte do ZIP do participante e ainda não foi usada para aceitar movimento nativo LAN. A confirmação anterior de caminhada animada local está no [relatório próprio](local-animation-2026-10-25.md) e não foi reclassificada como rede.

Próximos resultados necessários: dois verificadores na rede física; um teste nativo limitado; depois dois Sekiros com controle independente e recibos de ambos, observando a autorização normal da Steam. Os critérios de quatro máquinas permanecem obrigatórios para a v1.
