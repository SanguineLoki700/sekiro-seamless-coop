# Ponte de perfil e investigação nativa — 2026-10-21

Código avaliado: [`a6fdb674cf4312dcebbd35cdb1655cd01203f9a0`](https://github.com/SanguineLoki700/sekiro-seamless-coop/commit/a6fdb674cf4312dcebbd35cdb1655cd01203f9a0).

`SaveProfileBridge` conecta configuração, decisão de caminho e armazenamento em um perfil de conta fixo. Cada operação revalida o contexto declarado, conta, origem e tipo de arquivo antes do I/O. O diretório protegido deriva da raiz solo aceita; diretórios precisam existir. Erros de commit parcial são conservados e não existe fallback para arquivos solo.

## Validação do código

Windows x64, MSVC 19.44, SDK 10.0.26100 e CMake 3.31.1, Release:

- Build passou, sem avisos reportados.
- Sete suítes CTest passaram; a nova ponte passou 12 casos de integração.
- As suítes existentes de runtime, sonda, política, configuração, armazenamento e núcleo continuam passando.
- 26 testes Python passaram; `tools/progress.py --check` confirmou documentos gerados consistentes e 0% funcional.
- Revisão independente somente de leitura não encontrou impedimentos no contrato atual da ponte.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
python -B -m unittest discover -s tests -p test_progress.py
python -B tools/progress.py --check
```

Os 12 casos usam árvores temporárias e dados sintéticos. Cobrem escrita/backup/leitura/restauração pelo controlador, preservação das sementes solo, recusa de troca de conta e contexto, modos/extensões inválidos, diretórios ausentes, hardlinks e recuperação de primário vazio. A ponte não mede o executável nem autentica conta ou processo; os contextos desses testes são declarações controladas.

## Pesquisa separada no jogo original

Uma inicialização normal pela Steam permitiu novas consultas somente de leitura de código e metadados. Backups privados foram preparados antes. Não instalamos o mod, executamos funções nativas por endereço ou carregamos uma partida. Essa observação não é teste de preservar saves com o mod.

A pesquisa refinou a origem do componente de conta, o thunk de leitura e a camada `DLIO::DLFileInputStream`. Também mostrou que o backend polimórfico abaixo do stream e o momento de inicialização continuam sem cobertura suficiente para habilitar hooks. A falha na consulta AppData pode seguir para `L"."`; retornar sucesso de um helper de caminho também não prova abertura de arquivo.

REA/Ghidra analisou três procedimentos de uma imagem sintética privada montada exclusivamente com intervalos retidos, completos e verificados por hash. O replay não contém a imagem integral do jogo, nem foi executado; assinaturas inferidas e destinos ausentes permanecem desconhecidos. [RESEARCH.md](../RESEARCH.md#save-boundary-follow-up-2026-10-21) registra proveniência, RVAs e Evidence IDs. O snapshot foi retido e a sessão própria foi fechada.

## Limite da entrega

Este incremento conecta componentes próprios, ainda sem interceptar chamadas do jogo. Formato nativo, inicialização segura, comportamento de Steam Cloud e preservação durante um ciclo real de criar/gravar/carregar/remover o mod permanecem pendentes. O critério `core-save-isolation` continua aberto; capacidades de gameplay e o percentual funcional não foram habilitados por esses testes.
