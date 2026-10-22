# Saves nativos — aceitação local em 2026-10-22

Código do adaptador avaliado: `db9c02420dae463515179f2818f2c5f0c01c8304`, incluindo o incremento anterior `8c5e837a0bef2f7aa0f7c268d18c60974b2acfbd`. Windows x64, MSVC Release, um participante e uma máquina. O responsável local operou o jogo e confirmou os resultados; Codex executou as medições e os testes de arquivos.

Alvo: Sekiro Steam 1.06, versão 1.6.0.0, build 5794815. SHA-256 do executável: `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.

## Cenários executados

| Cenário | Resultado e evidência |
|---|---|
| Instalação independente | Cópias físicas dos arquivos necessários, sem hardlinks, verificadas contra a origem. A cópia inicialmente falhou fora da biblioteca da Steam, inclusive sem DLL e pela Steam. Após a mudança para uma pasta de teste própria na biblioteca, permaneceu aberta no teste de dez segundos e nos testes humanos seguintes. A instalação pessoal não foi substituída. |
| Inicialização protegida | O pai mediu e fixou executável/DLL e as raízes físicas; criou o processo suspenso e entregou PID, nonce e IPC antes de retomá-lo. A DLL validou o binding, instalou a fronteira nativa e sinalizou READY. A observação posterior confirmou o processo vivo e a janela do jogo respondendo. |
| Perfil copiado explicitamente | Uma cópia byte a byte do primário pessoal foi criada como `.co2` e backup em árvore privada. Nenhum `.sl2` foi convertido, movido ou substituído. O responsável confirmou carregar o personagem, descansar em um Ídolo e sair normalmente. O primário co-op mudou e seu backup ficou idêntico ao novo primário. |
| Atualização do adaptador | A primeira versão recusava a abertura de metadados da pasta e o jogo exibia `FDP_System_Message(401171)`. A DLL foi atualizada, mantendo o perfil. O responsável repetiu o carregamento e o descanso com sucesso. |
| Recuperação pelo backup real | Com o jogo fechado, o controller verificou que primário e backup co-op eram idênticos. Removeu somente o primário privado, comprovou sua ausência e executou `restore_backup`. Conferiu os bytes restaurados e o backup preservado. Uma nova inicialização carregou esse save; o responsável confirmou o personagem e o encerramento normal. |
| Criação pelo próprio jogo | Outro perfil começou vazio, contendo apenas o arquivo de lease do controller. O responsável criou um Novo Jogo, chegou ao controle do personagem e saiu pelo menu. O jogo criou `S0000.co2` e `.co2.bak`, ambos com 11.928.528 bytes e hashes iguais entre si, diferentes do perfil copiado. |
| Preservação do solo | Durante cada execução modificada, os dois arquivos pessoais foram mantidos por handles de leitura sem compartilhamento. Seus hashes antes/depois permaneceram iguais, inclusive após o último teste e antes da abertura normal. Nenhuma restauração automática foi usada. |
| Retirada do iniciador | A opção temporária de inicialização foi apagada e a configuração vazia foi conferida. A Steam abriu o executável original; a enumeração de módulos não encontrou nossas DLLs de teste. O responsável confirmou carregar o jogo normal e encerrá-lo pelo menu. |

As ações humanas acima foram confirmadas diretamente durante os respectivos testes. Os processos modificados terminaram com código zero; não houve relançamento para outro Sekiro, timeout ou incerteza do monitor. O resultado `host_run_completed` sozinho não foi usado como aceitação: uma execução anterior terminou normalmente apesar do erro de save na interface.

A comparação de preservação se encerra **antes** do carregamento final do jogo normal. Esse carregamento foi solicitado ao responsável e pode produzir gravações legítimas no `.sl2`. Não houve tentativa de reverter esse progresso pessoal.

## Diagnóstico e limites da instrumentação

REA 6.7.0 com Frida 17.23.2 observou a repetição do erro: a consulta AppData sucedeu, seguida por quatro aberturas de diretório recusadas com `ERROR_NOT_SUPPORTED` (50), acesso zero e backup-semantics. Evidence: `ev_80371d53a2c287f09d97d399f2bd3f98b33021c0c4b197dd40983532b63df25f`.

Após a correção, outra coleta reteve 257 operações: uma consulta AppData, 12 aberturas, 11 fechamentos e 233 leituras, todas bem-sucedidas. Evidence: `ev_ccd8e6ce612526239b272245a5eeb602c9a65d30d1ec5f8e1b36d3ab5098e938`. Ela terminou pelo orçamento de eventos; não representa a totalidade do carregamento nem capturou uma gravação completa. A aceitação de gravação combina a ação humana confirmada com a alteração dos arquivos co-op e a preservação dos originais.

Ambas as coletas encerraram seus listeners e o provider sem erro de callback ou de limpeza. Frida instala interceptação temporária em memória; não houve substituição de argumentos/resultados, chamada de função do jogo ou leitura de buffers de conteúdo do save pelo observador. Os contadores compartilhados obtidos apenas em `DLL_PROCESS_DETACH` permaneceram não medidos em encerramentos do jogo: zeros nesses campos não foram tratados como ausência de I/O.

Dados pessoais, hashes de saves, caminhos locais, cópias, binários e logs completos permanecem privados. O iniciador privado de integração contém verificações específicas do ambiente e não faz parte de uma release pública.

## Verificação do código público

- Compilação MSVC Release aprovada.
- 9/9 suítes CTest aprovadas, incluindo 10 cenários do bootstrap, 22 casos de política nativa e 19 casos do armazenamento; junction/alias reais executados localmente sem skips.
- 26 testes Python do progresso aprovados.
- Fixtures verificam READY antes de TLS/global/main, escritas em offsets distintos, preservação integral, recuperação e recusa de variantes/caminhos não suportados. Não abrem saves pessoais ou Sekiro.

## Decisão e pendências

**Aceito: `core-save-isolation`, quatro pontos**, para os cenários locais e a build identificada. Criação, leitura/gravação, atualização do adaptador, recuperação e retorno ao solo foram exercitados com confirmação humana e medições. Os demais critérios mantêm suas exigências anteriores.

O progresso da v1 passa a **4% verificado / 96% restante**. Isso não concede pontos por compilação ou pesquisa e não comprova multiplayer. O lançador/runtime públicos permanecem diagnósticos; a integração do fluxo testado no pacote distribuível continua pendente, assim como Steam Cloud, outras builds, falhas de energia, outras contas/slots, campanha prolongada e múltiplos participantes. A proteção externa dos saves originais faz parte do ambiente aceito; não se afirma uma execução equivalente sem essa proteção.
