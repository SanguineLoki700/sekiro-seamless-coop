# Sekiro Seamless Co-op

Projeto comunitário para criar coop contínua de até **quatro jogadores** em **Sekiro: Shadows Die Twice — GOTY Edition**, no Windows com Steam.

**Estado atual: movimento básico entre dois jogos reais na LAN confirmado; multiplayer em desenvolvimento.** O assistente abre uma cópia de teste com a DLL e um perfil `.co2` separado por conta. Inicialização, compatibilidade e saves possuem critérios aceitos. O movimento entre máquinas tem uma primeira prova parcial; combate, transporte Steam e campanha sincronizada continuam pendentes. O pacote disponível é um protótipo limitado de movimento.

Repositório público: [SanguineLoki700/sekiro-seamless-coop](https://github.com/SanguineLoki700/sekiro-seamless-coop).

**Marco atual: caminhada, giro e parada com animação vistos nos dois computadores.** O [primeiro teste LAN no jogo](docs/validation/native-lan-motion-2026-10-25.md) combina confirmação visual do participante nos dois lados e recibo nativo do PC: um ator adicional, movimento recebido, saída normal e saves pessoais preservados no PC. A aceitação completa dos personagens e da movimentação permanece em andamento; o registro do notebook e outros requisitos ainda faltam. Veja as [etapas](docs/ROADMAP.md#etapas-de-execução).

A [medição anterior](docs/validation/native-follow-metrics-2026-10-25.md), feita antes do teste LAN, encontrou custo elevado nas atualizações e uma falha no limite da coleta que encerrava o teste. Há correções; a avaliação de desempenho continua pendente. O [teste local de animação](docs/validation/local-animation-2026-10-25.md) também continua disponível como evidência histórica distinta.

**Assistente LAN simplificado:** botões [Criar jogo / Entrar](docs/LAN_TEST.md), descoberta automática na rede e perfil de teste separado por conta. O primeiro movimento entre PC e notebook foi confirmado usando esse fluxo, sem novas linhas nas propriedades da Steam. O percentual continua em 10%, pois este teste parcial não aceita sozinho os critérios completos.

**Incremento experimental de ações:** o [teste local com um PC](docs/validation/local-actions-2026-10-26.md) confirmou visualmente corrida, salto e esquiva do segundo personagem usando a integração C++, com saída normal e saves pessoais preservados. Os [testes de ações](docs/ACTION_TEST.md) entre dois jogos continuam pendentes; o pacote público anterior permanece como a referência de movimento já observada.

O REA usa uma sessão própria de análise no Ghidra. Abrir o Ghidra manualmente não conecta automaticamente aquela janela ao REA. O jogo aberto é útil nas observações em execução; compilação e testes independentes funcionam sem ele.

## Progresso verificável

<!-- progress:start -->
![Progresso funcional verificado da v1](docs/progress.svg)

**10% verificado; 90% ainda não verificado.** Este número mede critérios aceitos da v1, não tempo ou esforço restante.

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

[Critérios, preparação e evidências](PROGRESS.md) · [Marcos e regras](docs/ROADMAP.md)
<!-- progress:end -->

Os pontos pertencem a critérios funcionais aceitos. Preparar ferramentas, criar o repositório, compilar uma estrutura inicial ou fechar Issues não aumenta esse número.

CI verde comprova compilação e testes independentes; não concede pontos nem comprova funcionamento em Sekiro. O [registro da validação inicial](docs/validation/bootstrap-2026-10-20.md) distingue os testes locais da observação no jogo.

## O que existe hoje

- Lançador próprio com descoberta Steam, identificação PE/SHA-256 e [inicialização protegida validada](docs/validation/core-launcher-2026-10-23.md).
- Runtime com ABI diagnóstica e bootstrap precoce dos saves; [carregamento e recusas de compatibilidade verificados](docs/validation/native-runtime-2026-10-23.md) antes dos hooks.
- Núcleo independente para sessões, autoridade, checkpoints, eventos idempotentes e protocolo v2 com envelopes vinculados à sessão, coberto por testes locais.
- Política de caminhos, armazenamento físico e [interceptação nativa de saves](docs/NATIVE_SAVE_IO.md), com `.co2`, conta preservada, backup e recuperação; validados nos cenários locais do [relatório](docs/validation/native-save-isolation-2026-10-22.md).
- Sonda de pesquisa separada com identidade exata e modos [`--actors`](docs/ACTOR_RESEARCH.md#sonda-pública-de-leitura) e [`--creator`](docs/ACTOR_CREATION.md#nova-sonda-reproduzível) para conferir campos, tipos e uma tabela selecionada no Sekiro aberto, somente com leitura.
- Issues e marcos públicos; cálculo de progresso reproduzível com evidências obrigatórias.

O formato dos saves separados segue a experiência dos Seamless de DS1/DS2/DS3. O adaptador conserva o codec e o I/O do jogo, retornando handles Windows para arquivos privados. A [ponte de perfil](docs/SAVE_BRIDGE.md) oferece também operações independentes de configuração e armazenamento. O fluxo está integrado ao lançador para testes explícitos; o personagem remoto real, visível e controlável permanece pendente.

A observação local com RTTI identificou `WorldChrManImp` e `PlayerIns`; a sonda reconferiu o jogador e os registros selecionados sem alterar o jogo. Isso não estabelece o layout completo, criação ou controle de um ator remoto. Veja a [pesquisa de personagens e suas limitações](docs/ACTOR_RESEARCH.md).

## Produto previsto

- Lançador próprio, DLL nativa, configurações e saves exclusivos do mod.
- Um anfitrião e três convidados por convite Steam, com sessão mantida após mortes, chefes, descanso e viagens.
- Mundo e progresso compartilhados, inventário individual e autoridade do anfitrião sobre consequências do combate.
- Ressurreição de Sekiro preservada; espectador após morte definitiva e retorno ao ídolo quando todo o grupo cair.
- Coop a partir do Templo Dilapidado, após receber a prótese; campanha principal e suas ramificações na v1.

O formato de distribuição toma os Seamless de Yui como referência de experiência. O código e a integração com Sekiro são próprios; isso não pressupõe acesso ao código interno desses mods. O me3 não é uma dependência obrigatória. PvP, NG+, desafios de chefes, consoles e compatibilidade geral com outros mods ficam fora da v1.

## Compilar a infraestrutura a partir do código-fonte

Requisitos: Windows x64, Visual Studio 2022 com ferramentas C++, CMake e Python 3.10 ou posterior. Estes comandos compilam e testam a infraestrutura disponível; **não instalam um multiplayer funcional no jogo**.

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
python -m unittest discover -s tests -p test_progress.py -v
python tools/progress.py --check
```

Não copie os binários de bootstrap para a instalação do jogo como se fossem uma release. Os pontos de integração e instruções jogáveis serão publicados quando forem comprovados.

Após compilar, o lançador oferece ajuda e um diagnóstico da DLL dentro do próprio processo, sem iniciar Sekiro:

```powershell
.\build\bin\Release\skcoop_launcher.exe --help
.\build\bin\Release\skcoop_launcher.exe --diagnose
```

`--inspect` identifica um executável; `--find-game` detecta a instalação Steam. O comando `--launch` permanece desativado para gameplay. O [caminho explícito de integração](docs/LAUNCHER.md), `--steam-start`, usa instalação independente, perfil preparado e proteção adicional dos saves originais. Ele não implementa multiplayer.

## Contribuir e acompanhar

Veja [o roteiro](docs/ROADMAP.md), [os critérios completos](PROGRESS.md) e as [Issues](https://github.com/SanguineLoki700/sekiro-seamless-coop/issues). Alterações de progresso incluem a evidência e os arquivos gerados na mesma PR:

```powershell
python tools/progress.py
python tools/progress.py --check
```

O gerador usa somente a biblioteca padrão do Python. O CI valida o cálculo sem permissão de escrita. Testes reais do jogo exigem instalações legítimas dos participantes; o CI público não executa Sekiro.

O [método de pesquisa e testes](docs/WORKFLOW.md) registra as práticas adotadas do Co-op Anything, como isolamento da instalação, notas de evidência, ferramentas com versão fixada e QA automatizado planejado para clientes reais.

## English status

The v1 target is four-player seamless co-op for Windows/Steam. The public launcher starts a protected independent test install through Steam with the save runtime loaded. Runtime identity refusal has been tested before hooks; native save creation, loading, writing and recovery passed controlled local tests with original-save protection. Remote characters, Steam transport, synchronized combat and double-click release packaging remain pending. There is no playable co-op release. The percentage above counts accepted functional criteria, not elapsed development time or closed issues.

The project does not redistribute Sekiro, proprietary game files or other mod authors' binaries. It is not affiliated with FromSoftware, Activision, Valve or Yui.
