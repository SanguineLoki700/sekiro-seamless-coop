# Validação inicial — 2026-10-20

Build de código avaliada: [`d7f0ec6570746aae75d47512f81476bbe93a43f0`](https://github.com/SanguineLoki700/sekiro-seamless-coop/commit/d7f0ec6570746aae75d47512f81476bbe93a43f0).

Este relatório registra preparação e uma observação limitada de pesquisa. **Não aceita nenhum critério funcional da v1 e não altera os 0% verificados.** Não houve teste multiplayer.

## Compilação e testes locais

Ambiente: Windows x64, MSVC 19.44.35228.0, Windows SDK 10.0.26100 e CMake 3.31.1. As configurações Debug e Release compilaram; os três testes CTest passaram nas duas configurações. A verificação final do responsável pela integração foi em Release.

| Verificação | Resultado observado | Alcance |
|---|---|---|
| `windows_runtime_smoke` | Passou. | ABI, diagnóstico da DLL em processo próprio, capacidades e recusa do lançamento de gameplay. |
| `probe_policy_tests` | Passou. | Identidade, limites de leitura e validação dos metadados RTTI com dados controlados. |
| `skcoop_core_tests` | Passou, 12 casos internos. | Sessões, autoridade, snapshots, codec e histórico de eventos em memória. |
| `test_progress.py` | 26 testes passaram. | Baseline, evidências, cálculo e arquivos gerados em diretórios temporários. |
| `tools/progress.py --check` | Passou, 0% funcional. | Consistência do acompanhamento versionado. |

Comandos da verificação final, a partir da raiz do repositório:

```powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build/msvc --config Release --parallel
ctest --test-dir build/msvc -C Release --output-on-failure --no-tests=error
python -B -m unittest discover -s tests -p test_progress.py -v
python -B tools/progress.py --check
```

O núcleo simula participantes em memória. Ele não autenticou contas Steam nem executou quatro instâncias do jogo. O diagnóstico da DLL ocorre fora de Sekiro.

## Observação no jogo já aberto

Alvo: Sekiro Steam, arquivo/produto 1.6.0.0, build Steam 5794815. SHA-256 do executável: `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.

O responsável pela integração executou `skcoop_probe --exe <executável identificado>` e a variante com `--rtti` contra um único processo já aberto. A identidade do arquivo, arquitetura AMD64, identidade do módulo principal, tamanho de imagem esperado e limites do endereço candidato passaram nas verificações da sonda. A leitura completa do slot de oito bytes retornou um ponteiro não nulo; a região correspondente estava comprometida e permitia leitura.

A variante RTTI, limitada a cinco leituras, reportou `.?AVWorldChrManImp@NS_SPRJ@@`. A origem independente do endereço candidato e as referências REA estão em [RESEARCH.md](../RESEARCH.md). O resultado é consistente com o gerenciador de personagens, sem comprovar seu layout, ciclo de vida ou as funções de controle.

A sonda abriu o processo somente para consulta e leitura. Nenhum arquivo do jogo foi modificado e não houve escrita de memória, instalação de hooks, injeção de DLL ou acesso a saves. Identificadores do processo e endereços de heap foram omitidos. As modificações gráficas presentes na instalação não foram avaliadas quanto à compatibilidade.

## Pendências

- Validar estruturas de atores e funções nativas antes de implementar seu controle.
- Implementar e comprovar inicialização dentro do jogo e saves exclusivos do mod.
- Implementar transporte Steam e observar um personagem remoto real entre duas máquinas.
- Executar os critérios de quatro jogadores em quatro máquinas e testar continuidade e campanha.

Os registros de CI no GitHub mostram verificações automatizadas de compilação e testes independentes. Eles não executam Sekiro nem substituem aceitação funcional por participantes reais.
