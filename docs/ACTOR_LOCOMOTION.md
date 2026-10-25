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
orientação acompanha os valores do original; esta etapa espelha a entrada local.
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
geral. Esta revisão ainda precisa de teste no jogo.

O caminho nativo do pad também solicita movimento em `0xb2bf40`: a análise da
rotina mostrou que ela altera somente o bit0 em módulo+`0x184`. O getter base
`0x7e3ea0`, conferido em instruções, resolve o módulo em
ator+`0x1ff8` → módulo+`0x80`. O RTTI dos módulos de origem e destino foi observado
como `SprjChrActionRequestModule`, em objetos distintos. A revisão combina a
entrada com essa solicitação apenas no módulo do guest e solta a solicitação
ao pausar/parar. Não chama os wrappers globais de animação encontrados nas
referências externas.

Antes de escrever, confere o mundo, registros, tipos dos dois atores,
controladores distintos e seus ponteiros de dono. Todos os campos de destino
precisam estar graváveis. Os vetores devem ser finitos, limitados e com w zero;
dados inválidos interrompem o teste. Não copia ponteiros, botões, inventário nem
buffers inteiros do controlador original.

F9 zera movimento e delta de orientação enquanto pausa; F10 solicita parada e
zera a entrada após reconferir os vínculos. O término do orçamento temporal
também solta a entrada. Uma troca de mundo/registro interrompe os comandos sem
escrever em um objeto antigo. A DLL continua fixa até a saída do processo; não
há despawn individual implementado. O payload/status privado da revisão passa
à versão5; contadores distinguem callbacks do controlador e recusas de contexto.

Testes independentes cobrem limites dos vetores, ausência de setters de posição
na fase de locomoção, pausa, retomada, soltura da entrada na parada/expiração e
recusa de configurações malformadas. Somente um teste real pode confirmar
caminhada, giro e retorno à postura parada. O total da v1 continua10%.
