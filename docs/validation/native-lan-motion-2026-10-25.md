# Primeiro movimento entre dois jogos reais na LAN — 2026-10-25

**Caminhada, giro e parada com animação foram confirmados pelo participante nos dois jogos.** O recibo nativo do PC confirma conexão, movimento recebido e criação de um personagem adicional. Este resultado é uma prova parcial do protótipo, sem aceitar integralmente um critério da v1. O progresso verificado permanece em **10%**.

## Identidade e cenário

- Fonte da versão distribuída: `c546422d79c30a5146832e87f07b9935559c854a`.
- Pacote: [lan-simple-2026-10-25](https://github.com/SanguineLoki700/sekiro-seamless-coop/releases/tag/lan-simple-2026-10-25); bootstrap 10, protocolo LAN 1 e adaptador de movimento independente 2.
- Sekiro Steam 1.06 / build 5794815, AMD64. Executável do PC: SHA-256 `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
- DLL do PC: SHA-256 `e0549f8cb324de0d377a59de1991c78671fb88d533c2afcb97d6fcc1754948c2`.
- Duas máquinas Windows na mesma LAN e duas contas Steam, conforme informado pelo participante. A quantidade de operadores humanos simultâneos não foi aferida.
- Responsáveis: SanguineLoki700, operação e confirmação visual dos dois jogos; Codex, inspeção do recibo, arquivos e implementação do PC.

O fluxo proposto foi Criar no PC, selecionar a sala e Entrar no notebook, carregar os perfis de teste de cada conta, separar os personagens e ativar com F8 nos dois jogos. O participante confirmou que o personagem do outro jogo andou, virou e parou com animação em ambos. Depois confirmou o fechamento dos jogos.

Os arquivos do pacote instalado no PC foram conferidos contra seu manifesto, e o executável de teste foi novamente conferido contra o hash registrado no recibo. O notebook não foi inspecionado diretamente: sua versão exata, arquivos e recibo final permanecem sem verificação independente.

## Medições do PC

| Observação | Resultado |
|---|---|
| Conexão nativa | Estado conectado observado; 1.980 frames enviados e 1.929 recebidos |
| Integridade registrada | Zero mensagens rejeitadas; erros de ator, transporte e sistema iguais a zero |
| Personagem adicional | Uma chamada de criação; fase de acompanhamento observada |
| Posição | 7.023 escritas; afastamento máximo medido de 6,019 unidades de jogo em relação à posição após estabilização |
| Animação | 28.090 escritas no grafo; contador de comandos de movimento não nulos igual a 888 |
| Duração limitada | Acompanhamento amostrado a 125.875 ms; estado encerrado sem erro a 246.062 ms, compatível com o limite ativo de dois minutos |
| Saída | Recibo final completo, código de saída 0, sem timeout externo ou jogo externo observado |
| Save pessoal e backup do PC | Hashes iguais antes/depois no recibo e em leitura independente após o encerramento |
| Configuração gráfica do PC | Preservada segundo o recibo |

O afastamento máximo não é distância total percorrida. Os contadores de escrita não são frames de rede nem uma medição de suavidade. O protótipo aplica correções contínuas de posição recebida, além da animação: a prova anterior de caminhada local com apenas um posicionamento inicial não descreve esta sessão LAN.

O recibo bruto foi preservado somente no armazenamento privado. SHA-256 do registro concluído: `b6931182ac95ba6bd5c94fd549efafa09b9add59f17373faf91073831bc58496`. Saves, contas, endereços de rede, credenciais e caminhos pessoais não integram este relatório.

## Alcance e próximo aceite

Esta sessão demonstra movimento básico entre dois jogos, com confirmação visual bidirecional e telemetria do PC. Não mede quantitativamente erro de interpolação, latência, colisões, desempenho sustentado ou comportamento sob perda de rede. A ausência de frames vencidos registrada neste teste não substitui uma injeção de perda/atraso.

A identificação da área continua sendo uma declaração do cenário controlado; não é identificação nativa do mapa. Armas e equipamentos remotos não tiveram verificação visual específica. Não foram testados combate, gancho, natação, furtividade, próteses, transições de mapa, morte, reconexão ou quatro jogadores. O encerramento e a preservação dos saves do notebook não possuem recibo coletado.

`actors-spawn` e `actors-movement` continuam em andamento. Para aceitar seus pontos, faltam completar e registrar seus requisitos: modelo/armas e independência dos atores, exploração representativa com avaliação das correções/interpolação, resultados de ambos os clientes e os mínimos de jogadores. Os requisitos dos demais critérios e a baseline de quatro máquinas permanecem iguais. O teste atual é aproveitado como evidência; não exige repetir a preparação manual antiga.
