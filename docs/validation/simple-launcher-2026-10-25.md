# Abertura direta e fluxo LAN simplificado - 2026-10-25

**Progresso funcional:10%, sem novos criterios aceitos.** O objetivo deste incremento e reduzir o trabalho de preparar cada tentativa e evitar importar saves entre contas.

| Verificacao | Resultado observado | Limite |
|---|---|---|
| Abertura direta protegida inicial | Carregamento confirmado pelo participante, saida0, recibo final concluido e hashes originais iguais | Um jogo, uma maquina; fluxo CLI inicial, bootstrap9, sem atores |
| Preparacao do perfil independente | Duas preparacoes devolveram a mesma pasta; marcador de conta correto e `.co2` inicialmente ausente | Nao prova que um save de outra conta pode ser convertido |
| CTest Release |23/23 conjuntos passaram | Simulacoes e sockets locais, nao dois jogos |
| Descoberta UDP | Host encontrado pelo IP da resposta, pacote nao solicitado recusado, conexao real HMAC e expiracao depois do fim do host | Loopback em uma maquina |
| Perfis/negociacao | Modo independente e finalidade/adaptador antigo separados; payload de cenario errado recusado | Nao verifica mapa nativo ou sincroniza campanha |
| Novo assistente integrado | Participante criou Novo Jogo, controlou o personagem e fechou; saida0, `.co2` e backup criados, originais preservados | Um jogador, uma maquina; movimento entre maquinas nao aceito |

A prova direta inicial deixou as opcoes existentes da Steam intactas e nao as utilizou. O participante confirmou Continue e fechamento normal; o lancador registrou codigo0 e protecao dos saves originais, rechecados por hashes fora do processo. A copia do jogo ficou na biblioteca Steam independente existente; nao houve alteracao de Steam DLLs ou DRM.

O novo assistente organiza Criar/Entrar, descoberta automatica, preparo e abertura no mesmo fluxo. O perfil separado por conta comeca vazio e e escrito pelo proprio jogo. O participante confirmou a criacao pelo proprio botao do assistente, controle do personagem e fechamento normal. O jogo criou `.co2` e backup, e os originais mantiveram seus hashes. Uma nova preparacao depois dessa partida reutilizou o mesmo perfil e preservou ambos os arquivos byte a byte. A recarga por Continuar e o notebook ainda precisam de sua verificacao; a primeira preparacao nao importou save pessoal.

A negociacao do adaptador2 identifica um cenario manual de movimento na mesma area. A coincidencia de descritor e a proximidade nao foram promovidas a identificacao nativa do mapa. O primeiro teste precisa comecar no mesmo local seguro, sem ataques, descanso ou viagens. Controle entre dois jogos/multiplas maquinas e os criterios da v1 permanecem pendentes.

Contas sem arquivos originais tambem sao contempladas sem criar saves falsos: o preparador cria apenas os diretorios vazios de namespace. O host fixa os pais e verifica a ausencia das folhas originais; se observar um arquivo novo fora do perfil, a tentativa falha e o arquivo externo e preservado. Testes de sistema de arquivos verificaram ausencia sem placeholders, pin do pai, deteccao de criacao e preservacao do conteudo externo. A fixture nativa tambem executou I/O antes de TLS/global/main sem primario nem backup originais, produzindo somente `.co2`/backup privados. Esses sao testes de plataforma, nao prova de uma segunda conta num segundo jogo.
