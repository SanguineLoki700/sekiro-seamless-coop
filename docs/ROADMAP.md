# Roteiro da v1

Objetivo: quatro jogadores no Windows/Steam, do Templo Dilapidado aos finais da campanha, com lançador próprio, DLL nativa, configurações, sessões persistentes e saves exclusivos. [PROGRESS.md](../PROGRESS.md) mostra a aceitação atual; [progress.json](../progress.json) é a única fonte do percentual.

## Marcos e condições para avançar

| Marco | Evidência necessária |
|---|---|
| Ambiente e isolamento | Executável identificado, núcleo carregado e saves normais preservados em teste real. |
| Viabilidade para dois | Dois personagens reais e visíveis, combate contra o mesmo inimigo e convite Steam entre máquinas distintas. |
| Quatro participantes | Um anfitrião e três convidados com atores distintos, IA, combate e conexão estáveis. |
| Continuidade | Morte, ressurreição, chefe, descanso, viagem e reconexão preservam a sessão e a campanha. |
| Campanha | Matriz de eventos, chefes especiais e ramificações concluída sem bloqueios de progressão. |
| Release | Duas horas com quatro jogadores, pacote reproduzível, documentação, licenças e recuperação testada. |

Não estimar uma data de campanha completa antes da prova de viabilidade para dois jogadores. Uma ferramenta conectada, uma DLL compilada ou um simulador de rede não comprova multiplayer dentro de Sekiro.

## Referências e divisão do produto

O formato dos Seamless de Yui orienta a experiência de instalação e sessão: lançador, DLL, configurações e saves separados. Não atribuímos a eles nosso compilador, protocolo ou arquitetura interna. A integração e a replicação são específicas de Sekiro, que precisa receber os recursos multiplayer necessários.

O núcleo mantém interfaces para jogo, transporte e persistência. O anfitrião confirma inimigos, vida, postura, execuções, recompensas e eventos do mundo; clientes apresentam seu personagem imediatamente e recebem correções limitadas. Sessão e campanha usam identidades e revisões para evitar confusão após carregamentos ou reconexões. Saída do anfitrião encerra a conexão e permite retomada posterior, sem migração automática na v1.

REA/Ghidra orientam a investigação e registram evidências; Co-op Anything organiza conhecimento e triagem. Nenhuma conexão de ferramenta é contada como funcionalidade entregue. Os arquivos do Sekiro Online antigo servem apenas como referência de investigação conforme as permissões aplicáveis, sem redistribuição de binários ou recursos de terceiros.

## Regras do percentual

- Sete grupos fixos: núcleo 10; atores 20; combate 25; Steam 10; continuidade 15; campanha 15; entrega 5. Total: 100 pontos.
- A baseline `v1-campanha-4-jogadores` também fixa o conjunto completo de IDs por grupo e a tupla peso/mínimo de jogadores/mínimo de máquinas de cada critério. O validador rejeita renomear, mover, remover, reponderar ou reduzir os mínimos apenas no ledger. Uma nova baseline exige revisão deliberada do contrato do validador/esquema e da documentação; não há migração automática de baseline nesta versão.
- Somar somente o peso de critérios `verified`. `planned`, `in_progress` e `blocked` valem zero, independentemente do esforço investido.
- Cada critério verificado possui evidência versionada e revisada por humano. O validador confere os campos e o relatório; não autentica uma sessão de jogo sozinho.
- O commit da evidência identifica a build testada, não precisa ser o commit que altera o ledger. Usar hash completo e SHA-256 do executável real do jogo, sem publicar esse executável.
- Preparação aparece separada. Issues duplicadas, canceladas ou fechadas não alteram a conta. Regressões removem pontos; mudanças de escopo exigem revisão explícita da baseline.

Exemplo do objeto `evidence` de um critério `verified` (os hashes abaixo são ilustrativos e não constituem evidência):

```json
{
  "commit": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "game_executable_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "game_version": "versão identificada da instalação",
  "scenario": "Passos reproduzíveis e condições de rede",
  "tested_on": "2026-10-20",
  "tested_by": "responsável pela validação",
  "participants": {"players": 2, "machines": 2},
  "result": "passed",
  "report": "docs/validation/relatorio-versionado.md"
}
```

O relatório registra build, cenário, resultado observado, máquinas/contas participantes, latência/perda medida, limitações e evidências pertinentes. Não incluir credenciais, identificadores privados desnecessários ou arquivos proprietários. Critérios de quatro jogadores exigem quatro máquinas distintas; simuladores verificam lógica, sem substituírem essa prova.

## GitHub e rotina de trabalho

Repositório público criado: [SanguineLoki700/sekiro-seamless-coop](https://github.com/SanguineLoki700/sekiro-seamless-coop). Criar o repositório conclui uma tarefa de preparação, sem conceder pontos funcionais.

Issues descrevem tarefas e citam os IDs dos critérios. Milestones organizam os marcos acima. Um Projects pode oferecer as colunas Planejado, Em andamento, Bloqueado, A validar e Verificado; a porcentagem nativa desse painel não substitui o percentual ponderado do README.

A label `status:validation` identifica trabalho aguardando teste ou revisão de evidência. Seu estado no `progress.json` continua `in_progress`, valendo zero pontos. `validation` não é um novo estado funcional do ledger.

Uma PR de aceitação altera `progress.json`, adiciona o relatório e executa `python tools/progress.py`. A mesma PR inclui README, PROGRESS e SVG gerados. `python tools/progress.py --check` confere tudo sem escrever arquivos.

CI em PRs, `push` da branch principal e execução manual: compila/testa a infraestrutura no Windows e verifica o acompanhamento no Ubuntu. Permissões somente de leitura, sem PAT, credenciais Steam ou execução de Sekiro. Actions são fixadas por commit completo. Recomenda-se exigir PR e os checks para a branch principal; a revisão humana adicional depende de haver outro mantenedor disponível.

Não subir código-fonte ou arquivos proprietários do jogo, dumps de memória ou binários de outros mods. O teste no jogo permanece local aos participantes, documentado no repositório.
