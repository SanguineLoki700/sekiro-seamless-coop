# Assistente LAN de abertura direta

O fluxo atual tem dois botoes: **Criar jogo neste PC** e **Entrar no PC selecionado**. O assistente encontra a sala na rede local, prepara a instalacao independente e abre o jogo com o perfil de teste da conta ativa. O participante nao copia codigos nem configura novas opcoes de inicializacao da Steam. As instrucoes estao em [LAN_KIT.txt](LAN_KIT.txt).

A abertura direta foi observada localmente: o participante carregou o personagem e fechou pelo menu; o processo saiu com codigo0 e os hashes dos saves originais permaneceram iguais. Essa prova inicial usou o caminho direto com perfil importado. Depois, o participante criou Novo Jogo pelo botao do assistente e confirmou controle e saida normal; o jogo produziu o `.co2` e backup, com hashes originais preservados. Nenhuma dessas observacoes equivale a dois jogadores reais conectados.

## Perfil por conta

O modo `lan_profile=independent` prepara um perfil persistente em `SeamlessCoop/profiles/<conta>/movement-v2`. O jogo cria o proprio `.co2` na conta atual a partir de **Novo Jogo**; depois o participante usa **Continuar**. O assistente nao importa o save pessoal, nao converte o Steam ID de outro save, nao altera validadores de propriedade do jogo e nao apaga os perfis antigos. Um marcador vincula a pasta preparada a conta esperada.

A Steam continua fornecendo a conta ativa e o jogo original continua responsavel pela autenticacao. O lancador recusa uma configuracao de outra conta. A [documentacao oficial](https://partner.steamgames.com/doc/api/steam_api) distingue a inicializacao no cliente e o AppID legitimo. O projeto usa apenas o AppID814380 e DLLs originais, sem substituicoes ou alteracoes de DRM.

O bootstrap privado passa a **10**. O envelope LAN permanece `SKL1/v1`; o novo modo usa adaptador de movimento **2**. Seu hash de cenario representa uma declaracao de teste controlado na mesma area, nao o conteudo do save e nao um ID nativo de mapa. Assim, contas diferentes nao precisam de saves byte a byte iguais. O modo antigo de fixture identica permanece separado pelo adaptador e sua negociacao; um par incompativel e recusado. O codec de campanha v2 nao mudou.

## Descoberta local

A busca usa UDP24861 com pedidos limitados, nonce de consulta, respostas unicast de tamanho fixo e expiracao de salas. Seleciona o endereco de origem da resposta, evitando copiar o endereco de um adaptador VPN. O anuncio so comeca depois que o transporte nativo do anfitriao esta ouvindo; termina quando o par conecta ou o jogo sai. O TCP de movimento continua em24860.

A descoberta automatica serve a uma **rede privada de confianca**. A resposta carrega a credencial da sala em claro na rede local; ela nao e autenticacao de conta Steam. O transporte subsequente conserva HMAC-SHA256, sala e conexao aleatorias, participantes separados, sequencias e rejeicao de mensagens antigas. Nao ha encaminhamento de portas, mensagens externas ou regras de Firewall criadas pelo programa.

## Movimento e encerramento

A DLL nao le o jogador ou o grafo nas cenas e menus antes do F8. Depois de armar, publica a pose e as variaveis de caminhada a ate20Hz; a thread do jogo usa caixas de dados sem espera e a rede trabalha em outra thread. O personagem adicional recebe interpolacao de100ms, correcoes continuas de posicao e animacao recebida do parceiro. Dados vencidos por500ms liberam a caminhada; perda de mensagens por2s interrompe a sessao.

Os jogadores devem estar na mesma area segura, separados por pelo menos1metro e a ate25metros um do outro. O descritor0xa000 continua sendo apenas um descritor de criacao. Mudancas observadas de mundo/jogador/registro ou geracao encerram a tentativa. F9 pausa e F10 interrompe; uma criacao por processo, sem recriacao automatica, com limite ativo de2minutos.

A janela mostra o estado real do transporte e contagens recebidas/enviadas, sem afirmar sucesso quando falta o outro cliente. Recusa fechar o lancador durante a partida para evitar que seu job encerre o filho de surpresa; fechar primeiro pelo menu do jogo. Registros detalhados passam a ser amostrados por transicao e a cada30s, conservando a reserva do recibo final.

O jogo e executado somente na copia independente verificada, com os arquivos originais protegidos e recibos antes/depois. Originais existentes ficam bloqueados; quando nao existem, o preparador cria apenas os diretorios de namespace, fixa os pais e observa a ausencia dos arquivos sem criar placeholders. Um aparecimento externo observado interrompe o teste e nao e revertido. O limite externo do host continua30minutos. Os arquivos grandes existentes mantem a verificacao por comprimento; isso nao e verificacao criptografica dos depots. Nenhum binario e instalado na pasta original.

## Estado da verificacao

Os testes independentes cobrem codec/sessao, dois verificadores em processos separados, descoberta UDP, endereco de origem, recusa de resposta sem nonce correspondente, conexao HMAC real e expiracao sem anfitriao. A preparacao real reutilizou o mesmo perfil duas vezes sem copiar save pessoal. Relatorio e limites em [simple-launcher-2026-10-25.md](validation/simple-launcher-2026-10-25.md).

Depois desses testes, o participante confirmou caminhada, giro e parada com animação nos dois jogos reais, no PC e notebook. O recibo do PC confirmou movimento recebido, uma criação de ator adicional e saída com código zero; os saves pessoais do PC foram rechecados e preservados. O [relatório do primeiro movimento LAN](validation/native-lan-motion-2026-10-25.md) separa essa observação dos dados ainda não coletados no notebook e dos critérios completos ainda pendentes.

Ataques, postura, dano, IA, chefes, inventario, viagens, campanha, reconexao e convites Steam permanecem fora deste incremento. O progresso permanece10%; os criterios de quatro maquinas continuam obrigatorios.

Para montar um ZIP limpo depois da compilacao, usar `python tools/package_lan_test.py --out local/skcoop-lan-simple-windows-x64.zip`. O empacotador usa uma lista explicita e nunca inclui perfis, saves, chaves, arquivos do jogo ou registros.
