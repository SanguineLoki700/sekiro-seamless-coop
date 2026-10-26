# Captura e reprodução de ações em um PC — 2026-10-26

**O participante confirmou acompanhamento, corrida, salto e esquiva do segundo
personagem em duas rodadas locais.** A DLL registrou captura e enfileiramento
dessas ações no ator adicional, sem erro registrado. A segunda rodada confirmou
a esquiva, que não havia sido vista na primeira.
Esta é uma prova parcial com um jogo e um parceiro simulado no mesmo PC;
o progresso funcional permanece **10%**.

## Identidade e cenário

- Fonte testada: `a457aaa04cc78585483e5f8ca481fff97b0fc912`.
- Sekiro Steam 1.06 / build 5794815, AMD64; executável SHA-256
  `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
- DLL SHA-256
  `54ec50484323ee19f364b5cfc0f82ff80aea5553c3437c826d360b927765aa63`.
- Bootstrap 11, protocolo LAN 2, adaptador nativo independente 3.
- Um computador, um jogo real, instalação de teste independente e perfil
  persistente `.co2` da conta ativa. O notebook não participou.

O executável instalado foi rechecado antes da preparação. A DLL instalada,
o manifesto do pacote e o hash registrado pelo host foram comparados e
coincidiram. A preparação preservou os perfis existentes. O participante abriu
o jogo pelo assistente, carregou por Continuar, ativou com F8 e se afastou do
ponto inicial para permitir a criação do segundo personagem.

A fixture de desenvolvimento `--mirror-actions` recebeu a pose e o histórico
originais, sem interpolá-los no retorno, e devolveu os comandos como outro
participante autenticado no transporte local. O personagem adicional usou o
caminho C++ da DLL, sem script Frida nesta sessão. O auxiliar não faz parte do
pacote público e não representa outro cliente humano ou outro jogo real.

## Primeira rodada

| Observação | Resultado |
|---|---|
| Hooks de save, ator e captura | Máscara 19 registrada |
| Personagem adicional | Uma criação e uma tentativa; acompanhamento observado |
| Transporte do jogo | 314 frames enviados, 236 recebidos, zero rejeitados |
| Erros registrados | Ator, ações, transporte e sistema iguais a zero |
| Eventos permitidos capturados | 15 |
| Eventos enfileirados no ator adicional | 11 |
| Eventos descartados pelo receptor | 3; não há comprovação de entrega integral dos 15 capturados |
| Corrida | 1 início capturado e 1 enfileirado; confirmação visual do participante |
| Salto | 1 início capturado e 1 enfileirado; confirmação visual do participante |
| Esquiva | 1 comando capturado e 1 enfileirado; aparência não confirmada |
| Liberação de comandos | 1 registrada; estado final sem corrida retida |
| Encerramento do jogo | Código 0, recibo final completo, sem timeout ou jogo externo observado |
| Save pessoal e backup | Preservados no recibo e em comparação independente após fechar o jogo |
| Configuração gráfica | Preservada segundo o recibo |

Enfileirar um evento com retorno positivo do auxiliar nativo não comprova
sozinho que sua animação foi executada. O contador de esquiva é, portanto,
evidência do caminho de comandos, não confirmação visual dessa ação.

O auxiliar registrou `completed=false`: não completou todo o cenário programado
de 90 segundos. Seu relatório não distingue a causa específica desse término;
não se aceita aqui uma execução integral da fixture. O encerramento normal do
jogo e a preservação dos arquivos são verificados separadamente pelo host.

O participante primeiro descreveu dificuldade para ver a esquiva porque o
segundo personagem parecia estar no chão. Depois esclareceu que correu em
direção a uma parede, fora de um trecho plano, e que não estava relatando um
afundamento no chão. Esse relato não é classificado como defeito de
posicionamento. Também não constitui uma avaliação geral de colisões ou
navegação.

## Segunda rodada: esquiva confirmada

O participante reutilizou a mesma versão e o assistente, com um novo processo
do jogo e uma nova conexão do auxiliar. A instrução foi observar duas esquivas
laterais em um trecho plano, mantendo o segundo personagem visível. O
participante confirmou o resultado e depois confirmou fechar pelo menu.

| Observação | Resultado |
|---|---|
| Ator adicional | Uma criação, sem erro registrado |
| Eventos | 25 capturados, 23 enfileirados, 2 descartados |
| Esquiva | 3 comandos capturados e 3 enfileirados; confirmação visual do participante |
| Salto | 2 inícios capturados e 2 enfileirados |
| Corrida | 1 início capturado e 1 enfileirado |
| Transporte do jogo | 334 frames enviados, 235 recebidos, zero rejeitados, 2 frames vencidos registrados |
| Liberação de comandos | 2 registradas; estado final sem corrida retida |
| Erros | Ator, ações, transporte e sistema iguais a zero |
| Encerramento | Código 0, recibo final completo, sem timeout ou jogo externo observado |
| Save pessoal e backup | Preservados no recibo e em nova comparação independente após encerrar |
| Configuração gráfica | Preservada segundo o recibo |

O auxiliar desta rodada também registrou `completed=false`; a confirmação
visual específica não transforma o cenário integral de 90 segundos em aprovado.
O jogo, o auxiliar e o observador de teste já estavam encerrados ao concluir a
verificação. O assistente foi mantido aberto para o participante.

## Alcance e pendências

Este teste exercitou os novos hooks C++, a captura do jogador original, o
transporte autenticado local e o enfileiramento no grafo pertencente ao ator
adicional. A confirmação visual de corrida, salto e esquiva aproveita o resultado
obtido, sem exigir repetir a preparação anterior.

As correções contínuas de posição, inclusive altura, permaneceram presentes.
O salto visto aqui não comprova física independente do ator. Não foram
confirmados visualmente o pouso como transição específica, os casos
de pausa/retomada nem a transmissão dessas ações entre dois jogos reais.
Desempenho sustentado, terreno, colisões e perda de rede não receberam aceitação
quantitativa nesta sessão.

Os testes locais e do GitHub passaram na fonte testada: compilação Release,
26 suítes CTest, 26 testes Python e verificação do progresso. Nenhum requisito,
peso ou mínimo de jogadores foi alterado. Os critérios de personagens e
movimentação continuam em andamento, sem pontos novos. Combate, dano, IA,
campanha, reconexão e convites Steam continuam fora deste incremento.

Recibos brutos, contas, credenciais, endereços, caminhos pessoais e saves
permanecem privados. Pouso como transição específica e pausa/retomada ainda
precisam de verificação; a validação com dois jogos fica para quando o notebook
estiver disponível novamente.
