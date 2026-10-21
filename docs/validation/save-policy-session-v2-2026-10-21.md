# Política de saves e protocolo v2 — 2026-10-21

Código avaliado: [`e24754e5ef0a8cb3d9d206d55e94d6cc6bb93416`](https://github.com/SanguineLoki700/sekiro-seamless-coop/commit/e24754e5ef0a8cb3d9d206d55e94d6cc6bb93416).

Este incremento implementa a política independente de caminhos e a vinculação de mensagens à sessão. **Não valida redirecionamento no jogo, Steam ou um critério funcional da v1.** O progresso continua em 0%; saves passam a trabalho em andamento.

## Verificação local do código

MSVC x64 Release, Windows SDK 10.0.26100 e CMake 3.31.1:

| Teste | Resultado | Alcance |
|---|---|---|
| `windows_runtime_smoke` | Passou. | ABI/diagnóstico e recusa de gameplay continuam preservados. |
| `probe_policy_tests` | Passou. | Política da sonda e verificações RTTI com dados controlados. |
| `save_path_policy_tests` | Passou, 22 casos internos. | Nome `.co2`, backup, conta, Unicode, separadores nativos mistos, identidade declarada e recusas. Arquivos-semente temporários permaneceram intactos. |
| `skcoop_core_tests` | Passou, 14 casos internos. | Lógica de sessão/campanha, autoridade e codec v2 com regressões de mensagens antigas. |
| `test_progress.py` | Passou, 26 testes. | Baseline, evidências, estados e geração do acompanhamento. |
| `tools/progress.py --check` | Passou. | 0% funcional; documentos gerados consistentes. |

Comandos principais:

```powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build/msvc --config Release --parallel
ctest --test-dir build/msvc -C Release --output-on-failure --no-tests=error
python -B -m unittest discover -s tests -p test_progress.py
python -B tools/progress.py --check
```

Os executáveis de teste do núcleo e da política de saves também reportaram, respectivamente, `CORE_RESULT PASS cases=14 failed=0` e `SAVE_POLICY_RESULT PASS cases=22 failed=0`.

## Defeitos e diferenças detectados antes da entrega

O teste de replay foi executado primeiro contra o codec anterior. Um pacote de uma sessão encerrada, com sequência 900, foi aceito por uma nova sessão cujos contadores reiniciaram. O cenário falhou antes da correção, com 13 casos e uma falha. O protocolo v2 passou a exigir a sessão ativa em todos os envelopes e rejeita esse pacote antes da aplicação. A restauração de eventos da mesma campanha continua possível entre encarnações; sessão e campanha têm identidades distintas.

A primeira política de saves aceitava somente `\`. A pesquisa nativa mostrou fórmulas com `/` e `\`, corrigindo o contrato antes de publicá-lo. Três casos novos cobrem caminhos nativos mistos, raízes com `/` e recusa de separadores repetidos. O módulo continua sendo uma decisão lexical pura; flags de processo não autenticam o chamador e os testes não provam isolamento físico de arquivos.

## Pesquisa no alvo

Alvo de pesquisa: Steam build 5794815, executável 1.6.0.0, SHA-256 `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.

Leituras do processo original foram feitas com permissões de consulta e leitura, sem instalação do mod. A sonda limitada repetiu as verificações de identidade e reportou `WorldChrManImp`. Um helper privado separado leu regiões limitadas de código/unwind e reteve somente trechos selecionados para localizar montagem de pasta, `.sl2` e `.bak`. As instruções carregadas diferiam do código no disco; os resultados e limites estão em [RESEARCH.md](../RESEARCH.md).

Houve carregamento normal de um save durante a janela de observação, confirmado pelo responsável. Os hashes dos saves normais mudaram; essa janela não é uma prova de preservação byte a byte e foi excluída dessa aceitação. Nossas ferramentas não escreveram, renomearam ou restauraram os saves. Não foi realizado teste de salvar/carregar com o mod.

## Trabalho ainda necessário

- Validar uma fronteira completa de interceptação nativa e seu momento seguro de inicialização.
- Implementar o adaptador de arquivos com verificações físicas de destino, perfil, links e caminhos de falha.
- Comprovar criação, gravação, carga, backup/restauração e remoção em instalação e saves de teste isolados, incluindo o comportamento de Steam Cloud.
- Ligar o transporte à identidade de sessão, implementar atores reais e executar testes entre máquinas.

Os perfis de gameplay e as capacidades de hooks, saves isolados e multiplayer permanecem desabilitados. Não há pacote jogável neste incremento.
