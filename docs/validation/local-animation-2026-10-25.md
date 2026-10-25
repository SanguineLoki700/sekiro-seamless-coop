# Caminhada com animação do ator adicional — 25/10/2026

O participante confirmou que o segundo personagem **caminhou com animação**
no teste local acionado por F7. A medição registrou3,0234 unidades de deslocamento,
sem chamadas de posicionamento durante a caminhada. O teste soltou o comando
ao atingir seu limite de distância. Não houve controle pela rede.

## Caminho exercitado

As tentativas anteriores entregavam vetores ao controlador, mas o ator não
caminhava. A referência com `PadManipulator` completo também não resolveu.
Uma coleta confirmou entrada não nula nos dois atores, porém deslocamento
somente do original. Os módulos de comportamento tinham donos corretos,
instâncias distintas e ligações válidas para91 variáveis.

A tabela nativa ligou `MoveSpeedLevel` e `MoveAngle` aos índices213 e209
nessa instância. Uma observação de oito segundos registrou as consultas dessas
variáveis pelo original, sem as consultas correspondentes no adicional. O
experimento seguinte comandou somente essas duas variáveis no adicional,
pelo helper nativo de escrita escalar e no contexto de atualização observado.

O teste usou velocidade0,75 e ângulo0, com teto de oito segundos e parada ao
atingir três unidades de deslocamento. Registrou260 chamadas nativas,
234 comandos de variável no guest e zero escritas no original. Houve zero erros
de callback; os valores anteriores foram restaurados e o observador removido.
O pequeno excedente de distância corresponde à verificação no quadro seguinte.

## Identidade e conservação

- Sekiro1.06, Steam build5794815, AMD64.
- SHA-256 do executável:
  `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
- Instalação independente e perfil `.co2`, com os originais protegidos.
- Saída do jogo0, recibo final sem timeout, gráficos e saves originais intactos.
- Hashes dos originais também conferidos depois do fechamento.
- REA: `ev_dab276cb1e2d8aed80a36e6f86b98dc3232bc5882e151fbafe5b166842159925`.

O [template original testado](../../research/experiments/local_graph_locomotion.template.js)
tem SHA-256 `d604180ae755969c70c099861472360e07928e0c778123473f99bce865fbf520`.
A configuração privada contém a identidade da sessão e os fingerprints dos
trechos carregados; bytes proprietários e ponteiros ficam fora do repositório.

## Integração e limites

O modo C++ explícito `actor_test=graph-locomotion` usa o mesmo caminho de
variáveis, lendo velocidade/ângulo do original e escrevendo apenas no adicional.
Confere tipos, donos, instâncias diferentes, ligações e anchors da build antes
das chamadas. Usa handles próprios de24 bytes, sem copiar a estrutura do jogo.
F9 pausa; F10, expiração e mudança de cena encerram os comandos. Não reposiciona
o personagem continuamente.

O teste posterior da DLL, fonte `ebfb95574f08dc6c409cf7d599701be77b9dabe8`,
foi confirmado pelo participante: **caminhou, virou e parou com animação**.
O registro mediu10,241 unidades de deslocamento, uma criação, um único
posicionamento inicial e zero erros. O recibo final registrou3710 comandos de
variável e490 quadros com velocidade não nula. Não houve
observador Frida nesse processo, hooks no pad original ou no controlador do
adicional: somente o callback do mundo e a proteção já existente dos saves.

A saída posterior terminou com código0, sem timeout, com recibo final; os
hashes dos saves originais e do perfil co-op anteriormente aceito permaneceram
inalterados. O acompanhamento animado local pela DLL está confirmado.
F9 e F10 têm testes de política independentes; não foram solicitados como uma
nova rodada manual nesta sessão.

MSVC Release,17 conjuntos CTest,26 testes Python e conferência do ledger passaram.
O experimento real teve uma pessoa e uma máquina. Movimento remoto, ataques,
saltos, combate e duas máquinas continuam pendentes. O percentual permanece10%.
