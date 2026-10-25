# Progresso funcional da v1

> Gerado de `progress.json` por `python tools/progress.py`. Não editar este arquivo à mão.

Baseline: **v1-campanha-4-jogadores**. **10% verificado / 90% restante.**

Somente critérios `verified` somam seus pontos. Trabalho em andamento e configuração de ferramentas não concedem pontos parciais. O percentual não estima prazo ou esforço.

A label GitHub `status:validation` significa que o trabalho aguarda validação; no ledger permanece `in_progress`, com zero pontos até a aceitação como `verified`.

| Funcionalidade | Verificado | Falta |
|---|---:|---:|
| Núcleo, compatibilidade e saves isolados | 10/10 pontos | 0 pontos |
| Personagens remotos e movimentação | 0/20 pontos | 20 pontos |
| Combate, IA, postura e chefes | 0/25 pontos | 25 pontos |
| Sessão Steam, convites e reconexão | 0/10 pontos | 10 pontos |
| Continuidade, viagens e persistência | 0/15 pontos | 15 pontos |
| Campanha, ramificações e encontros especiais | 0/15 pontos | 15 pontos |
| Estabilidade, pacote e entrega pública | 0/5 pontos | 5 pontos |
| **Total da v1** | **10%** | **90%** |

## Preparação (fora dos 100 pontos)

| Preparação | Estado | Constatação / pendência |
|---|---|---|
| Pasta local do projeto | Pronto | A pasta local foi inspecionada. Isso não comprova um mod funcional. |
| Python, Git e CMake encontrados | Pronto | Os três executáveis foram localizados no ambiente. A integração no jogo ainda precisa ser construída e testada. |
| Investigação de Sekiro e do mod antigo | Em andamento | Investigação em andamento; instalação Steam 1.06 (build 5794815) identificada. SHA-256 do executável: 637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856. Isso não comprova hooks ou combate. |
| Publicação no GitHub | Pronto | Repositório público criado e confirmado: https://github.com/SanguineLoki700/sekiro-seamless-coop . Isso não representa funcionalidades entregues. |
| Contas e máquinas para validação multiplayer | Bloqueado | A validação final exige quatro contas Steam e quatro máquinas distintas; essa disponibilidade ainda não foi comprovada. |
| Compilação e testes locais do bootstrap | Pronto | MSVC x64 Debug/Release e testes locais do runtime, core e política da sonda passaram. Isso não comprova multiplayer nem carregamento da DLL no jogo. |
| Primeira leitura do Sekiro aberto | Pronto | A sonda confirmou a identidade exata e leu oito bytes do endereço candidato derivado do Sekiro Online. O valor não era zero e a região era comprometida/legível. O significado da estrutura e o controle de atores continuam pendentes. |

## Critérios e trabalho restante

### Núcleo, compatibilidade e saves isolados — 10/10 pontos

- [x] **core-launcher** — Lançador dedicado inicia o mod (3 pontos; Verificado)
  - Aceitação: O lançador detecta a instalação Steam suportada, valida os arquivos e inicia a DLL; abrir o jogo normalmente continua disponível.
  - Validação mínima: 1 jogador(es), 1 máquina(s).
  - Evidência: [relatório](docs/validation/core-launcher-2026-10-23.md); commit `d48d46911f78c497a7bb4b050bdabed626a892f9`; jogo Steam 1.06 / 1.6.0.0 / build 5794815; 2026-10-23; resultado `passed`.
- [x] **core-native-runtime** — DLL e compatibilidade verificadas no jogo (3 pontos; Verificado)
  - Aceitação: A DLL carrega no executável identificado e recusa versões sem suporte antes de aplicar hooks.
  - Validação mínima: 1 jogador(es), 1 máquina(s).
  - Evidência: [relatório](docs/validation/native-runtime-2026-10-23.md); commit `533a5863e9afcc333215129dc1ee1ff420ca96dd`; jogo Steam 1.06 / 1.6.0.0 / build 5794815; 2026-10-23; resultado `passed`.
- [x] **core-save-isolation** — Saves do mod isolados e recuperáveis (4 pontos; Verificado)
  - Aceitação: Criar, salvar, atualizar e remover o mod preserva os saves normais; restauração de backup foi comprovada.
  - Validação mínima: 1 jogador(es), 1 máquina(s).
  - Evidência: [relatório](docs/validation/native-save-isolation-2026-10-22.md); commit `db9c02420dae463515179f2818f2c5f0c01c8304`; jogo Steam 1.06 / 1.6.0.0 / build 5794815; 2026-10-22; resultado `passed`.

### Personagens remotos e movimentação — 0/20 pontos

- [ ] **actors-spawn** — Personagem remoto real e visível (6 pontos; Em andamento)
  - Aceitação: Duas instalações exibem atores remotos controláveis com modelo, armas e animações, sem substituir o jogador local.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **actors-movement** — Movimento e animações sincronizados (6 pontos; Em andamento)
  - Aceitação: Posição, orientação e animações são replicadas com interpolação e correções limitadas durante exploração real.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **actors-traversal** — Gancho, natação, furtividade e próteses (4 pontos; Planejado)
  - Aceitação: As formas especiais de locomoção e uso de próteses permanecem visíveis e coerentes nas duas instalações.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **actors-four-players** — Quatro jogadores simultâneos (4 pontos; Planejado)
  - Aceitação: Um anfitrião e três convidados possuem atores distintos e entram e saem sem corromper referências ou atores locais.
  - Validação mínima: 4 jogador(es), 4 máquina(s).

### Combate, IA, postura e chefes — 0/25 pontos

- [ ] **combat-damage-ai** — Dano e alvos da IA compartilhados (7 pontos; Planejado)
  - Aceitação: Jogadores combatem o mesmo inimigo; o anfitrião confirma vida e dano, e a IA reconhece os participantes sem fogo amigo.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **combat-posture-deflect** — Postura, deflect e Mikiri (6 pontos; Planejado)
  - Aceitação: Deflect, postura e Mikiri mantêm consequências coerentes para atacante, defensor e anfitrião com latência medida.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **combat-executions** — Deathblows e ataques simultâneos (6 pontos; Planejado)
  - Aceitação: Execuções e recompensas são confirmadas uma vez, inclusive quando dois jogadores disputam o mesmo deathblow.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **combat-boss-phases** — Fases de chefes e ressurreição (6 pontos; Planejado)
  - Aceitação: Fases e marcadores de execução dos chefes convencionais permanecem consistentes para quatro jogadores.
  - Validação mínima: 4 jogador(es), 4 máquina(s).

### Sessão Steam, convites e reconexão — 0/10 pontos

- [ ] **steam-lobby-invites** — Lobby e convites Steam reais (4 pontos; Planejado)
  - Aceitação: Contas Steam distintas criam lobby e entram por convite com o mod aberto, sem servidor antigo de sinais.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **steam-transport** — Transporte compatível e tolerante a falhas (3 pontos; Planejado)
  - Aceitação: Versões incompatíveis são recusadas; atraso de até 150 ms e perda simulada de 3% não duplicam eventos duráveis.
  - Validação mínima: 2 jogador(es), 2 máquina(s).
- [ ] **steam-reconnect** — Entrada tardia e reconexão (3 pontos; Planejado)
  - Aceitação: Entrada tardia e reconexão retomam um checkpoint compatível; saída do anfitrião permite retomar depois sem migração automática.
  - Validação mínima: 4 jogador(es), 4 máquina(s).

### Continuidade, viagens e persistência — 0/15 pontos

- [ ] **continuity-death** — Morte e ressurreição sem encerrar sessão (5 pontos; Planejado)
  - Aceitação: Ressurreição é preservada; morte definitiva permite espectador e derrota do grupo retorna ao ídolo sem perder a sessão.
  - Validação mínima: 4 jogador(es), 4 máquina(s).
- [ ] **continuity-travel** — Descanso e viagens coordenados (5 pontos; Planejado)
  - Aceitação: Chefes, descanso e transições mantêm a sessão; carregamento lento e pacotes de uma região anterior são tratados corretamente.
  - Validação mínima: 4 jogador(es), 4 máquina(s).
- [ ] **continuity-checkpoints** — Checkpoints e recuperação de gravação (5 pontos; Planejado)
  - Aceitação: Identidade e revisão da campanha permitem retomada; gravação interrompida é recuperável e campanhas divergentes não se misturam.
  - Validação mínima: 2 jogador(es), 2 máquina(s).

### Campanha, ramificações e encontros especiais — 0/15 pontos

- [ ] **campaign-progression** — Progresso compartilhado e inventário pessoal (5 pontos; Planejado)
  - Aceitação: História, chefes e desbloqueios são compartilhados; inventário é pessoal e recompensas também chegam a participantes mortos.
  - Validação mínima: 4 jogador(es), 4 máquina(s).
- [ ] **campaign-branches** — Campanha principal e ramificações (5 pontos; Planejado)
  - Aceitação: Do Templo Dilapidado aos finais previstos, escolhas do anfitrião e eventos não bloqueiam progressão; a matriz de cobertura está anexada.
  - Validação mínima: 4 jogador(es), 4 máquina(s).
- [ ] **campaign-special-bosses** — Chefes e eventos especiais (5 pontos; Planejado)
  - Aceitação: Macacos da Biombo, Dragão Divino e outros encontros especiais listados na matriz possuem adaptações e testes reproduzíveis.
  - Validação mínima: 4 jogador(es), 4 máquina(s).

### Estabilidade, pacote e entrega pública — 0/5 pontos

- [ ] **release-endurance** — Sessão de duas horas com quatro jogadores (3 pontos; Planejado)
  - Aceitação: Uma sessão contínua de duas horas termina sem crash nem divergência persistente de progressão, com relatório das condições de rede.
  - Validação mínima: 4 jogador(es), 4 máquina(s).
- [ ] **release-package** — Instalação, atualização, remoção e recuperação (2 pontos; Planejado)
  - Aceitação: Pacote reproduzível, documentação PT/EN, licenças e diagnósticos permitem instalar, atualizar e recuperar saves do mod em uma instalação limpa.
  - Validação mínima: 1 jogador(es), 1 máquina(s).

## Como aceitar ou reabrir um critério

A alteração de estado entra por PR com evidência em arquivo versionado: commit completo, SHA-256 do executável do jogo, versão do jogo, cenário reproduzível, data, responsável, quantidade real de jogadores e máquinas e resultado `passed`. O relatório inclui condições de rede, passos e limitações.

O validador verifica formato, quantidades mínimas e existência do relatório. Ele não comprova sozinho que uma sessão de Sekiro aconteceu; essa aceitação exige revisão humana da evidência.

Issues fechadas, duplicadas ou canceladas não alteram o percentual. Regressões voltam a `blocked` e retiram os pontos. Mudança do escopo ou dos pesos requer revisão explícita da baseline; nunca retirar um requisito silenciosamente para aumentar a porcentagem.

O validador fixa os IDs por grupo, o peso e os mínimos de jogadores e máquinas de cada critério da baseline v1. Alterar somente `progress.json` não redefine esses requisitos. Uma nova baseline exige revisão deliberada do contrato do validador/esquema e da documentação.

Testes do jogo e de convites Steam ocorrem nas instalações dos participantes. CI pública compila o código e verifica testes independentes, mas não executa Sekiro nem recebe seus arquivos proprietários.
