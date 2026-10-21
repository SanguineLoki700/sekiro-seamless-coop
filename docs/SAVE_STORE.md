# Armazenamento de saves em diretório controlado

`CoOpSaveStore` implementa I/O real de bytes opacos em **Windows x64/NTFS**, limitado a um diretório cooperativo existente e a uma árvore protegida existente, ambos fornecidos explicitamente. Não procura AppData, não descobre saves, não importa o solo e não instala hooks no jogo. O adaptador de Sekiro e a capacidade de saves isolados do runtime continuam pendentes.

## Interface e nomes

`open(SaveStoreConfig)` recebe o diretório final de conta cooperativa, a árvore solo protegida e a extensão. A extensão é ASCII alfanumérica de 1–120 caracteres; `sl2` é sempre proibida, independentemente de maiúsculas. A API expõe apenas `S0000.<extensão>` e `S0000.<extensão>.bak`; o padrão é `co2`. Nenhum nome arbitrário pode ser usado para ler ou gravar arquivos.

- `read(kind, expected_size)` lê entre 1 byte e 64 MiB. O tamanho esperado é opcional; quando fornecido, precisa coincidir exatamente. Bytes opacos não revelam sozinhos o formato ou checksum de Sekiro: detectar truncamento sem esse tamanho ou uma validação de formato é responsabilidade do adaptador.
- `write(bytes, BackupMode::none)` substitui o primário sem criar backup automaticamente. Um payload vazio ou maior que 64 MiB é recusado.
- `BackupMode::create_if_missing` preserva o primário anterior em backup antes do commit; se já existir backup, recusa sem sobrescrevê-lo nem alterar o primário.
- `BackupMode::replace_existing` permite explicitamente substituir o backup com o primário anterior.
- `restore_backup(expected_size)` é uma operação explícita: substitui o primário com bytes validados do backup e conserva o backup. Permite recuperar um primário vazio ou de tamanho inválido, sem relaxar verificações de links, reparse points ou identidade física.

Uma recusa nunca tenta usar `.sl2` ou outro caminho como fallback. Abrir o store valida a segurança física dos arquivos, mas permite conteúdo de tamanho inválido para possibilitar recuperação explícita. Ler valida o arquivo solicitado; gravar mantém verificações conservadoras de tamanho dos arquivos existentes.

## Vinculação física e exclusividade

A abertura fixa cada diretório ancestral por handle sem `FILE_SHARE_DELETE`, rejeita reparse points/junctions e compara o nome canônico do handle com o caminho solicitado. Isso também recusa aliases 8.3 e aliases de volume que não correspondam ao caminho canônico. Compara volume e IDs físicos dos diretórios para recusar árvores iguais, ancestrais ou descendentes da árvore protegida. Nenhuma dessas verificações enumera ou lê o conteúdo dos saves protegidos.

Os arquivos são abertos **sem truncar**, com `FILE_OPEN_REPARSE_POINT`; antes de ler ou alterar o namespace, a implementação verifica tipo, caminho canônico e `NumberOfLinks == 1`. Um hardlink de primário ou backup para um arquivo solo é recusado antes de qualquer truncamento. As operações de arquivo e rename usam nomes internos relativos ao handle do diretório fixado, via APIs nativas do Windows; não percorrem novamente o caminho completo para resolver o destino.

O arquivo interno `.skcoop-store.lock`, aberto sem compartilhamento, fornece uma lease exclusiva entre processos e entre instâncias. O arquivo pode permanecer vazio depois do encerramento; sua existência não representa um lock ativo. A mesma instância serializa chamadas com mutex. A aplicação deve entregar um diretório sob seu controle: isso não constitui proteção contra um administrador, manipulação do kernel ou alterações hostis fora do modelo de permissões do Windows.

## Commit, backup e falhas

A escrita cria um staging interno exclusivo com `CREATE_NEW`/`FILE_CREATE`, verifica sua identidade, grava e executa `FlushFileBuffers`. Só então renomeia o staging atomicamente para o primário, no mesmo diretório/volume. Nunca apaga o primário para depois tentar gravar um substituto. Em falha antes do rename, o primário anterior permanece e o staging é removido por seu próprio handle. `cleanup_error` informa falha dessa limpeza; um staging deixado por crash não é apagado automaticamente na próxima abertura.

**Primário e backup não formam uma transação atômica conjunta.** Quando solicitado, o backup é confirmado primeiro. O commit do primário ainda pode falhar depois disso. O resultado distingue `backup_committed` e `primary_committed`; uma falha com somente o primeiro flag significa que o primário anterior e o backup permanecem recuperáveis. O chamador não deve tratar esse resultado parcial como sucesso da gravação nova. `system_error` conserva o erro Windows correspondente.

O flush antecede o rename de namespace. Isso não promete persistência de todos os metadados após perda de energia, transação entre arquivos ou recuperação automática de qualquer crash. Erros de alocação C++ podem propagar; a futura fronteira de hook precisa capturá-los e falhar sem fallback solo.

## Validação controlada

Os testes usam somente diretórios temporários exclusivos e arquivos-semente sintéticos. Cobrem bytes v1/v2, backup sem sobrescrita implícita, restore, primário vazio reaberto para recuperação, limites de leitura, aliases, sobreposição física, hardlinks para uma árvore protegida, lock em outro processo e falha real de rename causada por um handle que não compartilha delete. Essa falha também testa limpeza de staging e resultado parcial após confirmar backup.

O teste de junction cria um reparse point real quando o ambiente permite. Se a criação for recusada, registra `SKIP` explícito, sem alegar que o caso foi validado. A disponibilidade de aliases 8.3 também pode produzir `SKIP` explícito. Nenhum teste acessa saves pessoais, abre Sekiro ou usa Steam. Esses resultados não comprovam ainda preservação dos saves reais pelo jogo.
