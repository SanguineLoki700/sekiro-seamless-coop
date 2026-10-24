# Primeiro PlayerIns adicional visível

**Resultado local confirmado em 24/10/2026:** o jogo criou um segundo
`PlayerIns`, o participante confirmou sua presença na tela e depois confirmou
que continuava controlando normalmente o personagem original, separado dele.
Um participante, uma máquina, dois objetos de jogador; nenhuma conexão entre PCs.

## Alvo e fonte executada

- Sekiro Steam 1.06 / produto 1.6.0.0 / build 5794815, AMD64.
- SHA-256: `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
- Fonte do corpo executado: `1bc94b0e5962ca105043b51ae84d30ac477e1667`.
- [Template original testado](../../research/experiments/local_player_spawn.template.js), SHA-256
  `426b80bc22fb796d74e80583107c08fc9e021307388eecf423870fdf4f45bd7e`.
- Evidência REA: `ev_501f8bca7697af1f4fc436f48e922a733258cfd5834ba7aebcde37c057a20764`.

O template público é uma cópia byte a byte do corpo testado. A configuração
gerada, com PID, endereços e âncoras do processo, permanece privada. O código é
um registro de experimento: não está integrado ao runtime C++ ou ao lançador
como funcionalidade distribuída. As pré-condições externas pertencem ao host
privado; o template isolado não comprova proteção dos saves.

## Experimento e verificações

A instalação independente e os binários de lançador/runtime já aceitos foram
reutilizados. Preparou-se uma cópia nova dos dois arquivos `.co2` em um perfil
descartável. O host protegeu os saves originais, e o perfil co-op anterior não
foi usado como destino de escrita deste teste. Não houve nova DLL instalada.

O campo `PlayerIns+0x70` foi conferido contra a atribuição nativa do descritor;
seu valor observado foi reutilizado como argumento de criação. Ele **não** foi
tratado como o identificador especial `0x2710`. O parâmetro canônico de campo
foi derivado de `+0x78` usando a máscara observada em `0xbf4bf0`; relações entre
os ponteiros de dados e o jogador carregado também foram conferidas. Nenhum
valor padrão foi inferido da captura anterior de zero eventos.

No contexto de atualização já observado, com RTTI, links e âncoras revalidados,
o experimento chamou **uma vez** o wrapper adicional `0xa49460`. A configuração
usou `ChrManipulator` base, sem comando local de pad. A rotina nativa escolheu
o registro1, construiu `PlayerIns` e retornou o mesmo ponteiro desse registro.
O registro0 e `WorldChrMan+0x88` mantiveram o jogador original.

O novo ator recebeu posição à frente do original através do setter nativo.
Após 60 atualizações de assentamento, seu RTTI e o registro1 continuavam
válidos, o primary permanecia nos links originais e a distância observada era
aproximadamente 2,2 unidades do mundo. O participante confirmou visualmente
o segundo personagem e o funcionamento do controle do original.

Foram observadas uma chamada de criação, 62 entradas selecionadas de atualização,
zero erros de callback e destaque dos listeners. O jogo continuou vivo após o
teste. A sessão REA/Frida foi liberada, com os campos de erro de cleanup nulos.
Chamadas de leitura de pose e posicionamento também ocorreram: "uma chamada"
refere-se especificamente à criação, não ao total de funções nativas.

`Interceptor` e as funções nativas modificaram o processo e criaram estado real
do jogo. Escritas diretas de JavaScript ficaram limitadas aos próprios buffers
de argumentos; as mudanças do novo ator e do registro foram feitas pelo jogo.
Esta etapa não é uma sonda de somente leitura.

## Encerramento e limites

Jogo e lançador foram encontrados fechados. Os hashes dos dois saves originais
continuaram iguais aos valores anteriores à sessão. O campo da Steam estava
vazio, e a configuração privada do lançador foi devolvida ao perfil anterior.
Não houve restauração ou sobrescrita de saves originais. O perfil descartável
e os recibos brutos foram mantidos privados.

O log da Steam registrou código1 para jogo e lançador, sem recibo final do host;
portanto **saída normal e destruição ordenada do ator não estão aceitas** nesta
rodada. A confirmação visual e de controle não depende dessa aceitação de saída.
O participante relatou fechamento sem problema aparente.

Ainda faltam integrar o adaptador ao runtime C++, verificar remoção/carregamentos,
comandar movimento/animação e conectar máquinas. Não houve teste de armas,
combate, campanha ou estabilidade prolongada. `actors-spawn` continua aberto:
seu critério exige atores remotos controláveis em duas instalações e duas máquinas.
O marco local não concede os seis pontos; o total da v1 permanece **10%**.
