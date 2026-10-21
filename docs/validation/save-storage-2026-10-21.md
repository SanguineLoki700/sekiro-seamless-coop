# Armazenamento e configuração de saves — 2026-10-21

Código avaliado: [`6df4a5bb79ee7f6d7fbeed70b74d54332aa8b7f5`](https://github.com/SanguineLoki700/sekiro-seamless-coop/commit/6df4a5bb79ee7f6d7fbeed70b74d54332aa8b7f5).

Este incremento implementa armazenamento de bytes opacos em diretórios de teste NTFS e leitura estrita de configuração. **Não intercepta saves de Sekiro, não habilita gameplay e não aceita um critério funcional da v1.** O progresso funcional permanece em 0%.

## Resultados locais

Windows x64, MSVC 19.44, SDK 10.0.26100, CMake 3.31.1, configuração Release:

| Verificação | Resultado e alcance |
|---|---|
| Build nativa | Passou, sem avisos reportados. |
| CTest | Seis suítes passaram: runtime, probe, política de caminhos, configuração, armazenamento e núcleo. |
| Configuração | 15 casos passaram: UTF-8/BOM, Unicode, limite de 64 KiB, chaves obrigatórias, duplicatas, extensão e caminhos. |
| Armazenamento | 13 casos passaram, zero falhas e zero casos ignorados neste ambiente. Junction real e alias 8.3 foram exercitados. |
| Política / núcleo | Respectivamente 22 e 14 casos passaram. |
| Progresso Python | 26 testes passaram; `tools/progress.py --check` confirmou os arquivos gerados e 0% funcional. |

```powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build/msvc --config Release --parallel
ctest --test-dir build/msvc -C Release --output-on-failure --no-tests=error
python -B -m unittest discover -s tests -p test_progress.py
python -B tools/progress.py --check
```

Os testes de armazenamento criaram somente diretórios temporários exclusivos. Foram verificados bytes exatos na sequência primário v1 → primário v2/backup v1 → restauração v1, substituição explícita de backup e recusa de sobrescrita implícita. Hardlinks para arquivos-semente protegidos foram recusados, conservando seus bytes. Um segundo processo de teste confirmou exclusividade do armazenamento.

Um handle que não compartilha exclusão provocou falha real de rename: o primário anterior permaneceu, o staging foi removido e o resultado informou a falha. Outro caso confirmou que backup concluído e primário recusado são reportados separadamente. Não se promete transação atômica entre os dois arquivos nem persistência de todos os metadados após perda de energia.

Durante a revisão, a validação de tamanho na abertura impedia recuperar um primário vazio. A abertura passou a validar segurança física sem exigir conteúdo válido; a restauração exige backup válido e mantém a recusa de links e reparse points. Um caso específico reabre o armazenamento com primário vazio e restaura os bytes do backup.

## Limites e próxima integração

O novo comando `--inspect-save-profile` verifica somente configuração. Uma raiz fornecida não demonstra isolamento físico nem habilita o jogo. Nenhuma capacidade do runtime ou flag de compatibilidade foi habilitada neste incremento.

O armazenamento ainda não está conectado à política de caminhos ou às chamadas nativas de Sekiro. Não decodifica, converte, verifica checksum ou importa o formato do jogo. O adaptador deverá estabelecer identidade do processo, inicialização antes de uso dos saves, validação de formato e tratamento de erros sem fallback solo.

Não houve acesso aos saves pessoais, instalação/injeção de mod, nova inicialização do jogo ou interação com Steam nesta validação. Criação, gravação, carga, backup/restauração e remoção dentro de uma instalação de teste isolada, incluindo o comportamento de Steam Cloud, permanecem pendentes. O critério `core-save-isolation` continua aberto e sem pontos aceitos.
