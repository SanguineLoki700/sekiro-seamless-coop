# Experimento de locomoção do personagem adicional

O acompanhamento local por posição já foi confirmado no jogo; o ator ficava
sem animação de caminhada. O modo experimental `actor_test=locomotion` tenta
alimentar o controle nativo do ator, para o motor processar movimento e animação.
**A animação ainda requer confirmação no jogo.** Não há rede nesta etapa.

## Evidência selecionada

A captura da build suportada reteve os métodos do `PadManipulator` usado pelo
jogador original e do `ChrManipulator` base usado pelo ator adicional. O
REA/Ghidra analisou uma imagem de pesquisa montada com quatro intervalos de
instruções retidas, identificados por SHA-256; ela não é carregável como mod.
Bytes, pseudocódigo e ponteiros privados continuam fora do repositório público.

O caminho de atualização do pad em `0x7ef120` reseta os campos do controlador e
processa entrada. O helper `0x7f1710` escreve os vetores de movimento nos offsets
`0x10` e `0xc0`, com componente w zero. Também escreve delta de orientação em
`0x20` e orientação alvo em `0x40`. O reset base `0x7e3510` corrobora esses campos;
seu trecho posterior tem limites de análise e não foi adotado como função
callable. O método correspondente de atualização base não processa o pad.

Isso justifica um experimento limitado nesses quatro campos, mas não estabelece
que todas as transições do grafo de animação irão funcionar. Sprint, combate,
saltos, ataques e reprodução de IDs de animação permanecem fora desta tentativa.

## Adaptador experimental

O modo é explícito e desligado por padrão. Depois de F8, cria e posiciona o ator
como no teste anterior. Após o assentamento, lê os quatro vetores do controlador
do original e os escreve apenas no controlador próprio do ator adicional. A
orientação usa o alvo do original e o delta calculado a partir da rotação atual
do próprio ator adicional; esta etapa espelha a entrada local.
O posicionamento repetido por setter fica desativado durante essa fase.

Na primeira rodada desse modo (payload4), o participante relatou que o ator
continuou parado e virou enquanto ele caminhava. Portanto a cópia dos vetores
no callback do mundo não aceitou caminhada nem animação. Uma tentativa privada
posterior foi interrompida por uma checagem de acesso antes do posicionamento e
de qualquer chamada de solicitação de movimento; não é evidência dessa chamada.

A revisão seguinte aplica a entrada no callback de atualização do controlador
adicional (slot9 da vtable base), depois do método original. Os demais objetos
passam pelo método original. O componente recebido precisa ser o componente do
ator registrado; os vínculos e campos são reconferidos antes das escritas. O
callback do mundo publica uma intenção com validade de250ms, além do orçamento
geral. O teste dessa revisão (payload5) registrou724 callbacks do controlador,
sem recusas de contexto, mas o participante continuou vendo o ator parado.
Isso não aceitou caminhada nem animação.

O caminho nativo do pad também solicita movimento em `0xb2bf40`: a análise da
rotina mostrou que ela altera somente o bit0 em módulo+`0x184`. O getter base
`0x7e3ea0`, conferido em instruções, resolve o módulo em
ator+`0x1ff8` → módulo+`0x80`. O RTTI dos módulos de origem e destino foi observado
como `SprjChrActionRequestModule`, em objetos distintos. A revisão combina a
entrada com essa solicitação apenas no módulo do guest e solta a solicitação
ao pausar/parar. Não chama os wrappers de animação encontrados nas referências
externas, cujo contexto de uso ainda não foi estabelecido para este ator.

Uma observação posterior do jogador original, durante caminhada confirmada pelo
participante, registrou deslocamento de4,45 unidades e vetores não nulos nos
campos selecionados. Os períodos anteriores sem entrada, alguns durante ausência
do participante, não foram tratados como prova de campos incorretos. Evidência
REA privada: `ev_c0000115f14f3687fb91be31c227b272f43863e4644b152748c5e8f1ad8a01fa`.

A revisão6 corrige a assinatura do slot9: o segundo argumento é um `float` no
registrador XMM1 e o terceiro é o componente em R8. A revisão anterior o declarou
como ponteiro; uma tentativa privada posterior de desreferenciá-lo falhou antes
de executar a finalização. O callback do criador do mundo tem assinatura
diferente e continua recebendo o ponteiro de tempo original.

Esta revisão captura a entrada após o método nativo do pad original e mantém
uma amostra com validade de250ms para o callback do ator adicional. Depois da
solicitação, chama `0xb2c190` com um objeto de tempo de16 bytes na própria pilha,
como o pad nativo faz: vtable em0, delta em8. Preserva o tempo de movimento
acumulado entre quadros ativos; uma soltura o limpa. Nenhum ponteiro de pilha é
retido. O getter de rotação `0x9eec10` permite calcular o menor giro até o alvo,
com ajuste ao atravessar ±pi. No teste da revisão6, o participante confirmou
giro, mas não caminhada. O recibo registrou636 capturas do original,25 com
movimento,3067 aplicações de entrada e3942 finalizações, com zero deltas
inválidos e zero recusas de contexto. O processo terminou com código0 e
originais/gráficos inalterados. Isso confirma a execução da chamada;
**não aceita caminhada**.

Antes de escrever, confere o mundo, registros, tipos dos dois atores,
controladores distintos e seus ponteiros de dono. Todos os campos de destino
precisam estar graváveis. Os vetores devem ser finitos, limitados e com w zero;
dados inválidos interrompem o teste. Não copia ponteiros, botões, inventário nem
buffers inteiros do controlador original.

F9 zera movimento e delta de orientação enquanto pausa; F10 solicita parada e
zera a entrada após reconferir os vínculos. O término do orçamento temporal
também solta a entrada. Uma troca de mundo/registro interrompe os comandos sem
escrever em um objeto antigo. A DLL continua fixa até a saída do processo; não
há despawn individual implementado. Na locomoção, uma lacuna de mais de250ms
solta a entrada por um quadro e permite retomar; não encerra permanentemente o
experimento por causa de uma alternância de janela. O orçamento total de dois
minutos continua valendo. O payload/status privado passa à versão6; contadores
distinguem captura de entrada, amostras com movimento, finalizações e deltas
inválidos, além das recusas de contexto.

Testes independentes cobrem limites dos vetores, ausência de setters de posição
na fase de locomoção, pausa, retomada, soltura da entrada na parada/expiração e
recusa de configurações malformadas, passagem de float em XMM1 e giro ao cruzar
±pi. Somente um teste real pode confirmar
caminhada, giro e retorno à postura parada. O total da v1 continua10%.

## Referência com o controlador completo

O modo separado `actor_test=pad-reference`, desligado por padrão, serve para
comparar o resultado com o caminho nativo completo. Passa valor1 ao seletor
de manipulador da fábrica, que aloca0x450 bytes e chama o construtor do
`PadManipulator`, em vez dos0x340 bytes do controlador base. O construtor
também cria seus próprios objetos auxiliares. A análise selecionada foi retida
em REA `ev_40b77b1802fb1c3a8e0baa197b01e4f0295f4428df8d3f057584a208c8d00022`.

Não troca a vtable de um objeto base nem reutiliza seu tamanho menor. O retorno,
registro adicional, RTTI, controlador e vínculo de dono são conferidos, com
os links do jogador original preservados. Depois do posicionamento inicial,
o runtime somente observa o deslocamento; não aplica acompanhamento por posição,
entrada espelhada, solicitação de movimento ou finalização de ações. O próprio
motor executa o controlador do pad. O payload privado dessa referência é7.

**É uma referência local, ainda não validada no jogo, que lê o mesmo teclado.**
O controlador nativo pode continuar lendo entrada até o jogo fechar. F9 não
pausa esse controlador; F10 e o limite de dois minutos encerram somente a nossa
observação. O teste deve ser curto, em lugar seguro, seguido de saída pelo menu.
O contador de deslocamento observado não usa setters de posição na fase de
caminhada. Confirmar animação exige a observação do participante.

Esta comparação testa a hipótese de uma limitação do controlador básico;
o tipo do controlador ainda não foi estabelecido como causa da falha anterior.
Não representa controle remoto nem muda o percentual verificado.
