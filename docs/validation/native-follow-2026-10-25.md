# Criação e acompanhamento pela DLL — 25/10/2026 (UTC)

Teste local com **uma pessoa, uma máquina e dois objetos do jogo**. O participante
confirmou o segundo personagem seguindo o original. **Não havia animação de
caminhada e o participante relatou que o jogo ficou pesado**, com queda de
desempenho percebida. Não houve medição de FPS nesta rodada. Esses problemas
permanecem abertos; isto não aceita multiplayer nem estabilidade.

## Implementação exercitada

O runtime C++ cria uma vez um `PlayerIns` adicional pelo wrapper nativo observado
no experimento anterior. Não usa Frida para criar ou mover o personagem nesta
rodada. O jogador original conserva o registro0 e o guest usa o registro1 com
`ChrManipulator` base, sem entrada do pad local.

A opção estrita `actor_test=follow` é desligada por padrão. Na sessão protegida,
F8, com o jogo em foco, arma o teste depois do carregamento manual. Um worker
confere os digests de cinco entradas nativas e instala um callback por troca
atômica de um slot da vtable do criador; o método original continua sendo
chamado. Os digests cobrem 16 bytes por entrada, não funções completas.

Somente o callsite nativo já observado executa o adaptador. O teste valida os
tipos e vínculos da coleção, fontes de parâmetros e componentes. Usa o setter
nativo de posição, buffers alinhados e deslocamento limitado a seis unidades
por segundo. Aguarda o modelo por até120 atualizações e assenta o posicionamento
por60 atualizações. Interrompe os comandos ao detectar troca de mundo, jogador,
registro ou handle de área; a fase de acompanhamento tem limite de dois minutos.
O ator não é destruído manualmente: remoção individual continua sem binding.

O deslocamento usa uma posição relativa constante no mundo. Não transmite rede,
não comanda a locomação do jogo, não replica rotação e não implementa animações.
O callback permanece carregado, sem trabalho de ator após a parada, até o
encerramento do processo. A ABI diagnóstica continua versão1; o payload privado
entre lançador/runtime passa a versão2, exigindo o par atualizado.

## Resultado e encerramento

- Recibo: uma tentativa de criação, 251 atualizações selecionadas, 185 chamadas
  do setter de posição e nenhum erro detectado pelo adaptador; fase following.
- Confirmação humana: apareceu e acompanhou, sem animações; jogo pesado.
- Jogo retornou código0; host terminou normalmente, sem timeout ou processo
  estrangeiro, com um detach da DLL e nenhum erro fatal do bootstrap.
- Saves originais, backup, configuração gráfica, perfil co-op anterior,
  configuração e par de binários aceitos anteriormente permaneceram intactos.
  O teste utilizou outro par de binários e cópias descartáveis dos saves.
- Opções da Steam conferidas vazias após o teste; nenhum jogo/host ficou aberto.

A sonda pública de atores foi executada depois das observações de acompanhamento
e recusou um campo inválido. Uma tentativa posterior já não encontrou o processo.
Isso não fornece uma segunda confirmação de RTTI no momento do movimento; os
vínculos foram checados pelo adaptador dentro do callback. Os recibos e binários
exatos testados ficam privados. A saída com código0 não comprova remoção
individual, viagem, reconexão nem repetição da criação na mesma execução.

MSVC Release passou, assim como **14 suítes CTest**, 26 testes Python e a
verificação do ledger. Os testes novos cobrem falha sem repetição, modelos
atrasados, mudança de mundo/registro, posições inválidas, limite de movimento,
expiração, configuração opt-in e recusa do payload antes dos hooks.

**Próximo trabalho:** medir o custo por atualização e da criação, distinguir
queda de desempenho do ator do custo do acompanhamento e conectar entradas de
locomoção/animação com evidência nativa. O critério `actors-spawn` continua
aberto e não ganha pontos por este teste de uma máquina: total da v1 **10%**.
