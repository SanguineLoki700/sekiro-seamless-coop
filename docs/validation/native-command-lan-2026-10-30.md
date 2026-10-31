# Protótipo LAN de comandos nativos — 30/10/2026

Implementado o caminho **entrada nativa contextualizada → PlayerCommand → TCP autenticado → receptor → controlador próprio do convidado → comportamento do Sekiro**. A integração precisa agora do teste no PC e notebook; este relatório não declara aceitação entre dois jogos reais. Progresso permanece **10%**.

## Implementação

- Protocolo LAN 4, finalidade específica de comandos nativos, adaptador 5 e formato de comando 1. Kits antigos falham por incompatibilidade. O codec legado continua disponível explicitamente para diagnóstico, sem fallback do modo novo.
- Mensagens mantêm geração, sequência, tempo de origem, direção/intensidade e histórico de pressões/solturas capturado em cada atualização do Pad. Ataque e defesa são intenções. Não se enviam IDs de animação como comandos.
- Sessão, conexão e remetente vêm do envelope autenticado. Replays, alteração de histórico, gerações/atores trocados, dados inválidos e filas excedidas são recusados. A finalidade é negociada antes de aceitar comandos.
- Filas de envio e recepção limitadas preservam lotes e sua chegada original, sem substituir pressões ainda não entregues por um estado mais novo. O envio de comandos é separado dos snapshots e da interpolação.
- Pausa é explícita. Enquanto o consumidor está inativo, a rede conserva somente a metadata mais recente; a retomada exige baseline, descarta pressões antigas e bloqueia botões já mantidos até sua soltura. A thread de acompanhamento também detecta perda de foco sem depender de callbacks do mundo.
- Perda de atualização neutraliza a entrada. Atrasos de fila têm limite de 500 ms; a linha temporal relativa da origem evita aplicar um acúmulo atrasado como se fosse uma sequência atual. Não há reconexão na mesma partida.
- A fábrica constrói o mesmo Pad completo já comprovado. A criação começa automaticamente quando os dois jogadores carregam, estão próximos e o perfil/descritor observado é compatível. Um snapshot autenticado fornece apenas a posição inicial. Depois, o adaptador não corrige a posição nem reproduz eventos HKB; divergência acima de 8 m interrompe a representação.
- Uma parada mantém o controlador ainda pertencente ao mod em entrada virtual neutra. Ele não passa a receber os controles físicos da máquina local depois de perder o parceiro. Troca de propriedade invalida a seleção.
- O assistente abre diretamente o jogo pela instalação independente, preserva perfis por conta e anuncia a sala quando o novo conjunto de hooks fica pronto. Não exige F8, código copiado ou mudanças nas opções da Steam. Protocolo entre DLL e lançador: 17.

## Comando e resultado

A captura LAN lê a intenção mundial do Pad próprio antes da seleção de comportamento; estados e velocidade de animação não são usados como entrada de rede. O resultado nativo é observado separadamente. A prova automática local continua usando sua política estrita e seus limites aceitos em `4a4bd5e`.

No modo interativo, a intenção pode mudar enquanto o corpo ainda gira. Um vetor válido, mas diferente, observado no consumidor não recebe crédito de consumo nem é substituído pelo vetor solicitado. Essa diferença, por si só, não encerra o controle interativo; valores inválidos, propriedade, epochs e consultas de botões continuam protegidos. A política é selecionada antes de qualquer atualização. Essa integração de captura e controle contínuo ainda requer validação dentro dos dois jogos.

## Validação e limites

Build MSVC x64 Release sem avisos; **35/35 testes CTest**, **26 testes Python** e conferência do progresso passaram. O novo teste executa dois processos distintos com o TCP/HMAC real do projeto e verifica comandos nas duas direções, escopo autenticado, pressões, defesa mantida, soltura, pausa/baseline e supressão de ataque mantido durante a retomada. Também verifica codec, truncamento, versão antiga, finalidade incorreta, configurações e payload, e rejeita crédito para movimento divergente. É um teste do transporte e das políticas, sem executar Sekiro.

O controlador local anterior já teve prova estrita em um jogo para caminhada, ataque e defesa. Isso não valida esta nova integração entre jogos. O ZIP limpo contém somente assistente, lançador, DLL, manifesto e licenças/instruções; não contém saves, configurações locais, credenciais, logs ou arquivos do jogo.

O teste deve usar a mesma versão nos dois PCs, contas Steam com acesso legítimo ao jogo e o mesmo trecho plano/seguro do cenário já suportado. Há uma criação por abertura e limite de aproximadamente 25 minutos. Mudança de área, viagem, descanso, origem de mapa incompatível, divergência grande ou perda de sessão exigem nova abertura. Campo/descritor e posição próxima não comprovam identidade completa de mapa.

Ataque e defesa representam ações do ator; contato, bloqueio eficaz, parry, dano, postura, inimigos, chefes e campanha compartilhados continuam pendentes. Salto, esquiva e corrida ainda precisam dos respectivos controles nativos neste adaptador. Dois jogos reais e quatro máquinas continuam sendo provas obrigatórias separadas.
