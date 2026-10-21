# Política de caminhos de save

Este módulo define uma **decisão pura de caminho para Windows**. Ele não instala hooks, não altera o executável e não lê, grava, copia, converte, importa ou renomeia saves. Sua existência não habilita a capacidade de saves isolados do runtime nem concede pontos funcionais.

## Contrato

`skcoop::saves::decide_save_path` recebe um caminho absoluto UTF-16, `SavePolicyConfig` e `SaveProcessContext`. Uma decisão aceita fornece conta, tipo primário/backup e caminho de destino. Uma recusa fornece erro e **destino vazio**: o adaptador futuro não deve voltar silenciosamente ao caminho solo quando uma operação de save candidata for recusada.

O contexto exige opt-in explícito, processo de jogo selecionado, PID não zero correspondente, modo diagnóstico desligado e hashes esperado/observado iguais e não zerados. São fatos fornecidos pelo chamador: esta função não autentica o processo nem mede o executável. O adaptador deverá estabelecê-los por verificações independentes. Os flags não afirmam que um hook ou redirecionamento foi implementado.

As raízes são parâmetros explícitos, sem expansão de variáveis, consulta de ambiente, diretório corrente ou acesso ao filesystem. `appdata_root` representa a pasta Roaming fornecida pelo chamador; o caminho esperado é:

```text
<appdata_root>\Sekiro\<decimalAccountId>\S0000.sl2
<appdata_root>\Sekiro\<decimalAccountId>\S0000.sl2.bak
```

Os nomes `.sl2` e `.sl2.bak` foram confirmados por inventário limitado dos nomes dos saves existentes, sem leitura de seu conteúdo. A comparação de componentes usa `CompareStringOrdinal` do Windows, sem depender da locale do usuário. A conta decimal é preservada textualmente e deve representar um inteiro positivo de até 64 bits, sem zeros iniciais.

A investigação estática das rotinas candidatas nos RVAs `0x01151c60` e `0x01151dc0` encontrou formatos `%s/Sekiro/%s/` e `%s/Sekiro/`. O formato de nome de backup também usa `\`, de modo que caminhos nativos podem misturar os dois separadores. O contrato aceita `/` e `\` nas raízes e nos caminhos de entrada e produz separadores `\` no destino. Essa operação somente separa componentes válidos; não colapsa repetições nem resolve `.`/`..`, links ou aliases. A fórmula que a função auxiliar usa para obter a conta ainda é desconhecida; aceitar uma pasta decimal não valida esse binding nativo.

## Destino e extensão

O padrão é `separate_profile`:

```text
<coop_profile_root>\Sekiro\<mesmaConta>\S0000.co2
<coop_profile_root>\Sekiro\<mesmaConta>\S0000.co2.bak
```

O save primário e seu backup ficam no mesmo diretório de conta. Contas distintas não são achatadas no mesmo destino. Árvores de saves sobrepostas são recusadas nesse modo.

O modo `same_account_directory` é um opt-in separado. Ele exige que as duas raízes explícitas sejam iguais pela comparação Windows e propõe somente a extensão alternativa no diretório original da conta. Isso prepara uma possibilidade de integração futura; não demonstra que o jogo já aceite essa extensão. Nenhum dos modos move ou importa o save solo.

`extension` aceita de 1 a 120 caracteres ASCII alfanuméricos, sem ponto inicial. `sl2` é proibida em qualquer combinação de maiúsculas/minúsculas. O padrão `co2` reproduz uma escolha de configuração observada nos pacotes Seamless de referência, sem afirmar como seu redirecionamento interno funciona. `config/save_profile.ini` registra `[SAVE] save_file_extension=co2` e mantém a raiz vazia até configuração explícita; o campo C++ continua `extension`.

O componente `save_settings` lê esse INI com UTF-8 estrito, limite de 64 KiB, uma seção `[SAVE]` e exatamente as três chaves documentadas. Recusa duplicatas, chaves desconhecidas, caminhos ambíguos e a extensão solo. Raiz vazia é configuração incompleta, nunca autorização para iniciar o jogo. O comando `skcoop_launcher --inspect-save-profile [ini]` verifica somente essa configuração, sem abrir saves ou processos de jogo.

## Rejeições e limite de segurança

São recusados caminhos relativos, UNC, nomes de dispositivo e namespaces estendidos, separadores repetidos (inclusive `//`, `\\`, `\/` e `/\`), `.`/`..`, streams alternativos, controles/NUL, componentes com espaço ou ponto final, nomes reservados de dispositivo, UTF-16 malformado, nomes inesperados e pastas de conta ambíguas. Um único separador final de qualquer dos dois tipos é aceito apenas nas raízes de diretório. O módulo aceita caminhos locais com letra de unidade, conserva Unicode e usa limites de 255 unidades por componente e 32.767 por caminho.

**A validação lexical não é um sandbox de acesso a arquivos.** Ela não resolve reparse points, junctions, symlinks, hardlinks, aliases 8.3 ou mudanças entre verificação e abertura. Um caminho aparentemente separado pode apontar ao mesmo armazenamento. Antes de qualquer I/O real, o adaptador deve validar os destinos por handles, rejeitar redirecionamentos inesperados e comprovar isolamento dos saves, perfis e Steam Cloud. Não escrever através de hardlinks compartilhados. Se um caminho vier de um hook de API mais permissivo, rejeitá-lo nessa política exige tratamento explícito; não usar uma normalização genérica para contornar a recusa.

O componente independente [CoOpSaveStore](SAVE_STORE.md) implementa I/O limitado a um diretório co-op explícito, com verificações por handles, gravação em arquivo temporário, substituição do primário, backup e restauração explícitos. Ele recebe bytes opacos e foi projetado para perfis separados em NTFS. A decisão lexical e o armazenamento ainda não estão conectados ao jogo; o modo `same_account_directory` da política não é suportado por esse armazenamento.

Hooks, ligação com chamadas nativas de Sekiro, importação, conversão de conteúdo, migração e política de Steam Cloud permanecem pendentes. O campo `SaveFileKind::backup` na política somente identifica o nome `.bak`; a operação física pertence ao armazenamento separado. Seus testes temporários não comprovam salvar/carregar Sekiro com o mod.

## Testes

Os testes usam caminhos sintéticos e arquivos-semente em diretórios temporários exclusivos. Eles verificam o mapeamento, extensão, identidade e opt-in, conta, Unicode, recusa de caminhos ambíguos e ausência de I/O na decisão. Nenhum teste abre saves pessoais, a pasta Roaming real, Sekiro ou uma sessão Steam. Esses testes comprovam a política isolada, não o redirecionamento dentro do jogo.
