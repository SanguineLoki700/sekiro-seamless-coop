# Corrida, salto e esquiva: incremento experimental

O adaptador de ações está implementado como candidato para teste protegido.
A compilação e os testes independentes não confirmam ainda essas ações entre
dois jogos reais. O movimento LAN anterior já teve caminhada, giro e parada
confirmados pelo participante nos dois computadores. O progresso permanece 10%.

Na integração C++, o [teste local de 2026-10-26](validation/local-actions-2026-10-26.md)
já teve corrida, salto e esquiva confirmados visualmente no segundo personagem,
com encerramento normal e saves pessoais preservados nas duas rodadas do PC.
Foi um jogo com parceiro
simulado; os dois jogos reais ainda precisam ser testados.

## Caminho implementado

A DLL observa os comandos originais do grafo do jogador após sua execução,
preservando argumentos e retorno. Observa os resultados dos auxiliares que o
próprio jogo chamou; não repete operações na pilha Lua. Filtra pelo dono do
grafo, pelos nomes permitidos e pelos IDs resolvidos na build suportada.
Não transmite ponteiros, nomes arbitrários ou IDs nativos pela rede.

O produtor conserva quatro eventos com serial, tempo relativo e direção,
antes da limitação de envio a 20 Hz. Só pode remover um registro após entregá-lo
à caixa local do transporte; isso não equivale a uma confirmação remota.
Uma lacuna maior que o histórico, transbordamento ou contenção que impediria
registrar um evento encerra o teste com erro visível.

O primeiro incremento de movimento usou `SKL1/v2`: frames de 244 bytes, incluindo HMAC-SHA256,
com 96 bytes explícitos para histórico e estado de corrida. O adaptador nativo
independente 3 e bootstrap privado 11. O candidato de [ataques e defesa locais](COMBAT_PREVIEW.md)
usa LAN 3, adaptador 4 e bootstrap 12, conservando o tamanho do frame. Os dois computadores
precisam atualizar o pacote completo. O codec de campanha não mudou.

O receptor usa sala, conexão, participante e época autenticados pelo transporte.
Separa eventos discretos da pose interpolada; a fila ordenada tem capacidade 16,
atraso de 100 ms, validade local de eventos de 250 ms e de frames de 500 ms.
Esses relógios não medem a idade real de trânsito da rede. Duplicatas não
disparam novamente. Menu, criação, acomodação, pausa e retorno após dados
vencidos estabelecem uma base atual e descartam pedidos antigos. A passagem
entre descarte inativo e consumo pelo jogo usa a mesma trava, sem esperar no
callback do jogo. Corrida retida é restaurada como estado, sem repetir histórico.

No personagem adicional, a DLL resolve os eventos nos próprios dados do jogo,
confere mundo, registro, dono, grafo, variáveis e fila e usa o auxiliar local do
ator. Recusa fila cheia ou índices inválidos antes de chamar o código nativo.
Não usa auxiliares que mudam o jogador global. Pausa, dados vencidos e perda de
conexão soltam comandos apenas enquanto a vida do ator continua verificável.
Mudança de mundo impede escrever no ator antigo. Os limites anteriores de
uma criação e dois minutos ativos continuam válidos.

## Evidência e limites

Na build Steam 5794815 / 1.06, SHA-256
`637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`,
uma observação protegida registrou 31 eventos do jogador original durante
movimento, corrida, saltos e esquivas feitos com controle. Essa observação
não usava o novo caminho C++ de captura.

Uma experiência separada enfileirou o início de salto no personagem adicional,
com variáveis pertencentes a ele. A posição vertical subiu cerca de 1,975 unidade
e retornou ao ponto inicial, sem correção de pose nessa experiência, sem escrita
no jogador original e sem erro registrado. O participante não confirmou ver o
salto naquele ensaio; a confirmação visual posterior pertence ao teste C++
com correções de pose descrito no relatório acima. O evento de preparação
observado na origem é normalizado para esse início no destino. Não há pouso
fabricado por temporizador: os eventos de pouso vêm da origem.

Um teste misto teve relato ambíguo de corrida ou esquiva, sem salto visual.
Também apareceu `FDP_SYSTEM_MESSAGE(4161)` após criar o ator; o participante
dispensou a mensagem. Sua causa continua desconhecida. A última sessão de
pesquisa atingiu o limite externo de 30 minutos; não foi uma saída normal.
Os hashes dos saves pessoais e backup no PC foram rechecados e preservados.
Nenhum desses resultados constitui aceitação nativa do novo adaptador C++.

Os testes independentes cobrem pulsos entre frames, perda/duplicação/reordenação
com 64 seeds, consumidores deliberadamente defeituosos, HMAC real, escopo de
conexão, confirmação de lotes, transbordamento, descarte inativo, pausa,
retomada, estado de corrida e guardas de vida/fila. Binários, decompilações,
registros brutos e saves permanecem privados.

## Verificação necessária

1. Captura C++ de corrida, salto e esquiva no PC: observada com contagens por
   ação, sem erro e com saída normal; resultado registrado no relatório local.
2. Corrida, salto e esquiva visuais do ator adicional: confirmados no teste local.
   Pouso como transição específica e pausa/retomada continuam pendentes; uma
   fixture de desenvolvimento continua sendo apenas um jogo real.
3. Atualizar o notebook e verificar cada ação nos dois sentidos, além de pausa,
   parada e saída. Recolher recibos e preservação dos saves dos dois clientes.

Sem notebook, a fixture de desenvolvimento `--mirror-actions` pode receber os
comandos capturados e devolvê-los como um parceiro simulado autenticado no
próprio PC. Usa um ponto observado e espera o participante se afastar entre
1,25 e 6 metros; conserva essa separação ao espelhar um trajeto de até 6 metros,
durante no máximo 90 segundos. Exige o par original de pose/histórico, recusa
troca de época ou descritor e deixa dados vencidos soltarem os comandos.
O participante deve ficar em um trecho plano e seguro. Essa fixture não é
incluída no pacote público. O teste confere a captura/aplicação C++ e a aparência
do ator: as correções de posição, inclusive altura, continuam presentes, então
não comprova sozinho física independente, navegação nem dois jogadores reais.

O assistente mantém Criar/Entrar, descoberta LAN e perfis existentes por conta.
Não exige códigos nem novas opções da Steam. Ataques, dano, IA, chefes, campanha,
reconexão e convites Steam ainda não pertencem a este teste.
