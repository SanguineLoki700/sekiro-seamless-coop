# Sekiro Seamless Co-op

Projeto comunitário para criar coop contínua de até **quatro jogadores** em **Sekiro: Shadows Die Twice — GOTY Edition**, no Windows com Steam.

**Estado atual: infraestrutura inicial; ainda não é um mod jogável.** O código de bootstrap e os testes independentes não comprovam carregamento no jogo, personagens remotos, integração Steam, hooks de saves nem combate sincronizado. Não há release jogável neste estágio.

Repositório público: [SanguineLoki700/sekiro-seamless-coop](https://github.com/SanguineLoki700/sekiro-seamless-coop).

**Fase atual: 1 — pesquisa da integração nativa.** O jogo aberto já foi usado para uma observação limitada, somente de leitura, que identificou `WorldChrManImp`. Ainda precisamos validar as funções e os dados que permitem controlar personagens. As próximas etapas são carregar a DLL no jogo com saves isolados, provar coop entre duas máquinas e ampliar para quatro jogadores. Veja as [etapas e quando o jogo precisa estar aberto](docs/ROADMAP.md#etapas-de-execução).

O REA usa uma sessão própria de análise no Ghidra. Abrir o Ghidra manualmente não conecta automaticamente aquela janela ao REA. O jogo aberto é útil nas observações em execução; compilação e testes independentes funcionam sem ele.

## Progresso verificável

<!-- progress:start -->
![Progresso funcional verificado da v1](docs/progress.svg)

**0% verificado; 100% ainda não verificado.** Este número mede critérios aceitos da v1, não tempo ou esforço restante.

| Funcionalidade | Verificado | Falta |
|---|---:|---:|
| Núcleo, compatibilidade e saves isolados | 0/10 pontos | 10 pontos |
| Personagens remotos e movimentação | 0/20 pontos | 20 pontos |
| Combate, IA, postura e chefes | 0/25 pontos | 25 pontos |
| Sessão Steam, convites e reconexão | 0/10 pontos | 10 pontos |
| Continuidade, viagens e persistência | 0/15 pontos | 15 pontos |
| Campanha, ramificações e encontros especiais | 0/15 pontos | 15 pontos |
| Estabilidade, pacote e entrega pública | 0/5 pontos | 5 pontos |
| **Total da v1** | **0%** | **100%** |

[Critérios, preparação e evidências](PROGRESS.md) · [Marcos e regras](docs/ROADMAP.md)
<!-- progress:end -->

Os pontos pertencem a critérios funcionais aceitos. Preparar ferramentas, criar o repositório, compilar uma estrutura inicial ou fechar Issues não aumenta esse número.

CI verde comprova compilação e testes independentes; não concede pontos nem comprova funcionamento em Sekiro. O [registro da validação inicial](docs/validation/bootstrap-2026-10-20.md) distingue os testes locais da observação no jogo.

## O que existe hoje

- Lançador próprio com identificação PE/SHA-256 e diagnóstico da DLL.
- Runtime com ABI explícita, inicialização fora de `DllMain` e capacidades declaradas.
- Núcleo independente para sessões, autoridade, checkpoints, eventos idempotentes e protocolo v2 com envelopes vinculados à sessão, coberto por testes locais.
- Política de caminhos de save com extensão padrão `.co2`, conta preservada e tratamento de nomes de backup; ainda sem interceptação nativa ou gravação no jogo.
- Sonda de pesquisa separada para observar um endereço candidato no Sekiro já aberto, somente com leitura e identidade exata do executável.
- Issues e marcos públicos; cálculo de progresso reproduzível com evidências obrigatórias.

**Trabalho atual:** ligar o carregamento e os saves separados à integração nativa, seguindo a experiência dos Seamless de DS1/DS2/DS3. A [ponte de perfil](docs/SAVE_BRIDGE.md) já conecta configuração, política de caminhos e armazenamento com backup/restauração, com testes em arquivos temporários. A pesquisa também identificou mais do caminho nativo de leitura, conta e stream. Esses componentes ainda não interceptam os saves do jogo. O personagem remoto real, visível e controlável continua sendo um resultado de gameplay necessário. Os testes em memória não substituem esse resultado.

A observação local com RTTI identificou o tipo `WorldChrManImp` no endereço candidato. Isso ajuda a orientar a investigação; não valida o layout de personagens nem implementa um ator remoto. Veja a [pesquisa e suas limitações](docs/RESEARCH.md).

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

`--inspect` apenas identifica um executável fornecido; `--launch` permanece desativado nesta infraestrutura. Identificar o hash de uma instalação não significa que os hooks ou os saves isolados foram implementados.

## Contribuir e acompanhar

Veja [o roteiro](docs/ROADMAP.md), [os critérios completos](PROGRESS.md) e as [Issues](https://github.com/SanguineLoki700/sekiro-seamless-coop/issues). Alterações de progresso incluem a evidência e os arquivos gerados na mesma PR:

```powershell
python tools/progress.py
python tools/progress.py --check
```

O gerador usa somente a biblioteca padrão do Python. O CI valida o cálculo sem permissão de escrita. Testes reais do jogo exigem instalações legítimas dos participantes; o CI público não executa Sekiro.

O [método de pesquisa e testes](docs/WORKFLOW.md) registra as práticas adotadas do Co-op Anything, como isolamento da instalação, notas de evidência, ferramentas com versão fixada e QA automatizado planejado para clientes reais.

## English status

This is an early infrastructure project, **not a playable co-op mod**. The v1 target is four-player seamless co-op for Windows/Steam, with a dedicated launcher, native DLL, persistent Steam sessions and isolated mod saves. In-game hooks, remote characters, Steam transport and save redirection remain unverified. The percentage above counts accepted functional criteria, not elapsed development time or closed issues.

The project does not redistribute Sekiro, proprietary game files or other mod authors' binaries. It is not affiliated with FromSoftware, Activision, Valve or Yui.
