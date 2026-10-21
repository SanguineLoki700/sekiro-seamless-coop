# Integração de configuração, política e armazenamento

`SaveProfileBridge` conecta três componentes existentes: configurações de `SaveSettings`, decisão lexical de `decide_save_path` e I/O controlado de `CoOpSaveStore`. É um controlador de perfil para testes e integração futura. **Não instala hooks, não abre o caminho solo de origem e não implementa a adaptação nativa ou o formato de save de Sekiro.**

## Abertura explícita

O chamador fornece configurações e `SaveProfileBinding`: raiz AppData explícita, caminho primário de origem, hash esperado e PID selecionado. Fornece também `SaveProcessContext`, cujos fatos são revalidados pela política. Esses valores são declarações do chamador; o bridge não mede o executável, autentica a conta nem observa o processo. A futura fronteira nativa deve estabelecer essas informações independentemente.

Somente `separate_profile` é suportado. Uma raiz cooperativa vazia é configuração incompleta; o modo que altera somente a extensão dentro da pasta solo é recusado. Depois que a política aceita a origem primária, o bridge fixa a conta decimal e abre o armazenamento no diretório de destino calculado:

```text
Origem declarada: <appdata_root>\Sekiro\<conta>\S0000.sl2
Destino:         <coop_profile_root>\Sekiro\<mesmaConta>\S0000.<extensão>
Árvore protegida: <appdata_root>\Sekiro
```

O destino final e a árvore protegida precisam existir previamente. O bridge não cria diretórios, procura AppData ou importa/converte saves. A árvore protegida é derivada da raiz de origem validada, sem um parâmetro que permita substituí-la por uma pasta sem relação com a origem. A validação física e a lease exclusiva continuam pertencendo ao armazenamento.

## Operações limitadas ao perfil

Toda leitura, escrita e restauração recebe novamente caminho de origem e contexto. O bridge aplica a política antes do I/O e recusa outra conta, mesmo que o caminho seja lexicalmente válido. O perfil aberto nunca muda de conta ou raiz durante sua vida.

- `read` aceita primário ou backup e usa o tipo determinado pela política.
- `write` exige origem primária e encaminha bytes opacos e o modo explícito de backup.
- `restore_backup` exige origem de backup e encaminha a restauração explícita para o primário da mesma conta.

Erros preservam a origem: rejeição de política, conta diferente, tipo incorreto ou falha de armazenamento. Os detalhes do armazenamento, incluindo `backup_committed`, `primary_committed`, `system_error` e `cleanup_error`, são conservados. Uma recusa nunca tenta ler ou gravar o caminho solo como fallback. A abertura bem-sucedida pode criar o arquivo de lease no diretório cooperativo, mas não cria um primário ou backup.

## Evidência e limitações

As fixtures criam árvores temporárias exclusivas, contas decimais sintéticas e arquivos-semente próprios. Cobrem o ciclo configuração → decisão de caminho → escrita/backup/leitura/restauração, preservação das sementes solo, revalidação de contexto, recusa de troca de conta, extensão e modo, diretório não preparado, hardlink real e recuperação de primário vazio. Nenhum teste usa AppData real, saves pessoais, Steam ou um processo de Sekiro.

Os nomes dos contextos não tornam os dados de teste autenticação real. O teste verifica a ligação entre componentes próprios; não comprova redirecionamento no jogo, inicialização antes de uso de saves, compatibilidade de formato, Steam Cloud ou um critério funcional da v1. Capacidades de gameplay e progresso funcional permanecem independentes.
