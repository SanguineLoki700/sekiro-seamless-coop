# Protótipo de movimentação por LAN

O teste inicial conecta dois participantes Windows na mesma rede, com uma instância normal da Steam em cada máquina. Cada jogo fornece posição, giro e variáveis de caminhada do seu jogador; a DLL aplica os dados recebidos ao personagem adicional. **A integração está implementada, mas ainda não foi validada com dois Sekiros reais.** O progresso funcional permanece em 10%.

O pacote tem um executável na raiz e a pasta `SeamlessCoop` com o lançador interno e a DLL. O executável da raiz é um assistente de conexão e preparação; abrir o jogo continua usando a entrada protegida pela Steam. A experiência final de iniciar o mod por dois cliques continua pendente. As instruções do participante estão em [LAN_KIT.txt](LAN_KIT.txt).

## Separar conexão e jogo

O verificador funciona sem Sekiro, sem Steam e sem ferramentas de desenvolvimento. Um computador cria uma sala e mostra o código, o outro entra com esse código. Só apresenta conexão verificada depois de observar tráfego nos dois sentidos. Cada computador pode salvar um relatório sem o código da sala. Essa prova verifica sockets e mensagens; não verifica personagens no jogo.

O verificador e o jogo declaram finalidades distintas na negociação. Um verificador não é admitido na sessão de personagens. Não existe retorno automático ao modo de imitação local quando falta o outro participante.

O assistente de preparação encontra a instalação Steam, verifica o executável suportado, prepara uma cópia independente do jogo na mesma biblioteca e clona o progresso original para um perfil novo `.co2`. Arquivos existentes não são sobrescritos. Ele recusa links e aliases nos caminhos usados. A cópia ocupa aproximadamente 25 GB extras; arquivos grandes já existentes conservam a verificação por comprimento do lançador, sem alegar verificação criptográfica completa dos depots. O lançamento revalida arquivos, isolamento e identidade antes de executar o jogo suspenso com a DLL.

Cada preparação conserva perfis anteriores e fornece uma linha nova para copiar nas opções da Steam. A primeira configuração LAN exige que o `.co2` preparado ainda seja uma cópia do `.sl2` original. Depois de uma tentativa que grava progresso, preparar novamente em ambas as máquinas. O pacote limpo não contém saves, conta, código de sala nem configurações pessoais. Depois da preparação, a pasta usada passa a conter esses dados e não deve ser redistribuída.

## Transporte e identidade

O transporte inicial usa TCP IPv4, fora da thread do jogo, com frames de movimento a até 20 Hz, heartbeat de 250 ms e perda de sessão após 2 segundos sem mensagens válidas. A interface fica separada para substituir o transporte posteriormente. TCP pode acumular atraso sob perda de pacotes; esta versão serve ao cenário curto de LAN e não satisfaz os critérios de rede da v1.

O envelope `SKL1`, versão 1, tem framing limitado a 256 bytes e autenticação HMAC-SHA256. O código contém endereço, porta, sala aleatória de 128 bits e segredo aleatório de 256 bits. Uma negociação em cinco mensagens vincula cada conexão a um nonce novo e às identidades aleatórias dos dois participantes. Sequências antigas, outra sala, outra conexão, versão/finalidade/build incompatíveis e valores inválidos são rejeitados. O segredo não entra nos recibos. O tráfego não é criptografado e o código não autentica uma conta Steam.

Este protocolo experimental é separado do [codec de campanha v2](PROTOCOL.md), que não mudou. Identificadores de participantes LAN também são separados de Steam IDs. O pacote preserva o AppID legítimo de Sekiro e os arquivos Steam originais.

Usar a mesma conta não comprova que duas instâncias de Sekiro serão autorizadas simultaneamente. O verificador não depende disso. A tentativa com os dois jogos deve observar o comportamento real da Steam; não há alteração de autenticação nem promessa de simultaneidade. Veja as informações oficiais sobre [uso de uma conta em mais de um computador](https://help.steampowered.com/ms/faqs/view/71EA-CDCE-FB5C-82B3) e [modo offline](https://help.steampowered.com/en/faqs/view/0E18-319B-E34B-B2C8).

## Adaptador nativo e limites do primeiro cenário

O bootstrap privado passa à versão 9 para levar papel, código e identidade do cenário. A sessão nativa compara o SHA-256 do jogo, a versão do adaptador e o hash do save original de partida. Isso identifica um cenário controlado com o mesmo save inicial, **não uma sincronização de campanha nem um identificador nativo de mapa**. O campo observado `0xa000` continua sendo apenas um descritor de criação.

Os acessos ao jogo continuam limitados ao executável 1.06/build5794815 já estudado. Reutilizam a fábrica, os getters/setter de pose e as variáveis de animação previamente observados. A thread do jogo usa apenas caixas de dados com tentativa de acesso sem espera; sockets, enquadramento e HMAC ficam na thread de rede.

F8 arma uma única criação após existir um frame remoto válido. Os jogadores devem estar separados por pelo menos 1 metro e dentro de 25 metros um do outro, na mesma área inicial, em local seguro. A implementação não teleporta o jogador original nem altera flags de colisão sem evidência. Aplica interpolação de 100 ms, correções de posição do personagem adicional e os valores de caminhada recebidos. **Essas correções são contínuas neste teste LAN**; a prova anterior de animação local usava apenas o posicionamento inicial. São caminhos e resultados diferentes.

Dados sem atualização por mais de 500 ms liberam a caminhada. Perda de conexão interrompe o controle remoto. Mudanças observadas de mundo/jogador/registro, de geração ou descritor remoto encerram a tentativa; não há recriação automática entre carregamentos. F9 pausa a representação remota e F10 encerra. O cenário ativo dura até dois minutos, com o limite externo do lançador preservado. Não descansar, viajar ou trocar de área neste primeiro teste.

Ataques, dano, postura, IA, chefes, inventário, eventos de campanha, reconexão e convites Steam continuam fora deste incremento. Não há aceitação de dois ou quatro jogos reais. Veja [os resultados disponíveis](validation/lan-foundation-2026-10-25.md).

## Reproduzir

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release --parallel 4
ctest --preset windows-release --no-tests=error
python -m unittest discover -s tests -p test_progress.py
python tools/progress.py --check
python tools/package_lan_test.py --out local/skcoop-lan-test.zip
```

O empacotador usa uma lista explícita de binários, instruções e licenças, com hashes no manifesto. Nunca empacotar recursivamente a pasta de um teste usado.
