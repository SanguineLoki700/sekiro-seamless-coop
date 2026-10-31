# Controlador remoto nativo

O caminho em implementação é **comandos de jogador → controlador exclusivo do ator → comportamento nativo → ações, física e animações do Sekiro**. O segundo `PlayerIns` já é um ator criado pela fábrica do jogo. O trabalho atual muda a origem de seu controle e comprova seu processamento independente.

O lançador, assistente, instalação de teste, perfis `.co2`, backups, DLL, identificação da build, fábrica de atores, TCP autenticado, sessões e histórico de estados continuam no projeto. Os testes anteriores de eventos HKB permanecem como diagnóstico legado. O novo modo não os instala, não os usa como fallback e não muda o transporte LAN para começar essa investigação.

## Ordem aceita e situação

| Etapa | Condição para avançar | Situação |
|---|---|---|
| 1. Caminho nativo completo | Entrada, consumidor, proprietário e fase demonstrados no ator adicional. | Comprovado no escopo inicial: movimento, ataque e defesa pelo controlador próprio, com resultados nativos distintos da entrada. Outros controles/famílias ainda precisam de validação. |
| 2. Prova automática em um PC | Comandos independentes, consumo downstream, deslocamento físico, controles mantidos/liberados e teste negativo. | Aceita em `4a4bd5e`: 431 comandos, 428 consumidos, avanço de 0,759 m, parada, ataque/recuperação e 2.469 ms de defesa efetiva seguida de repouso. Uma criação/posição inicial; zero drivers manuais/legados. Saída normal e saves preservados. |
| 3. LAN existente | Mesmo adaptador alimentado pelo TCP atual; comandos com formato explicitamente versionado e proteção de sessão. | Pendente da prova do controlador. A LAN anterior continua usando o caminho legado. |
| 4. Combate compartilhado | Ataque, contato, defesa, dano e postura; anfitrião confirma consequências. | Pendente. A primeira entrega jogável continua sendo uma luta em dupla contra o mesmo inimigo. |
| 5. Ampliar e otimizar | Famílias de ações, habilidades, chefes, medições de rede, Steam, quatro jogadores e continuidade. | Pendente. Testes em um PC não substituem dois jogos ou quatro máquinas. |

O progresso permanece em **10%**. A prova local não satisfaz os critérios de personagem remoto entre jogos reais, combate compartilhado ou quatro máquinas. Código, ferramentas, testes simulados ou um resultado inconclusivo não concedem pontos.

A oitava rodada consumiu comandos móveis e registrou 0,666 m de avanço na direção comandada, com uma criação e apenas o posicionamento inicial. A sequência parou na verificação da parada, antes de ataque e defesa. A nona interrompeu antes da parada, por diferença de intensidade entre 0,35 e 0,349454; o código histórico não distingue em qual estágio ela surgiu. Esses resultados parciais não concluem o controlador nem alteram os pontos.

Na décima rodada, o diagnóstico distinguiu a falha no vetor lido pelo resolver. A terceira atualização de caminhada começou 109 ms após a primeira, além do prazo cumulativo antigo de 100 ms. A janela ainda estava aberta e nenhum movimento divergente recebeu crédito. Esse resultado motivou separar o prazo de alinhamento da validade de cada observação; não demonstra que a sequência positiva esteja concluída.

Em 30/10/2026, a execução de `6516fb6` chegou ao fim do diagnóstico: consumo de caminhada, parada, ataque, defesa mantida e solturas, com controle negativo rejeitado, uma criação e um posicionamento inicial. Não houve erro de entrada, operações legadas ou escritas manuais no grafo. A verificação registrou 0,451 m de avanço e posição estável na janela de liberação. O resultado permanece **inconclusivo**, pois os estados que comprovam ataque e defesa efetivos continuam sem classificação. Uma pesquisa separada com controles virtuais também observou o próprio motor solicitar ataque, soltura, entrada e saída da defesa; ela não substitui a aceitação do adaptador C++.

## Contratos e controle

`PlayerCommand` separa intenção de ação, estado mantido e transições ordenadas de pressionar/soltar. Sequência, tempo e geração acompanham o comando. O contexto confiável do receptor acrescenta a encarnação da sessão, conexão e jogador; esses identificadores não são aceitos apenas porque um pacote os declara. A captura retém transições a cada atualização, antes de um envio mais lento. Os snapshots de estado existentes e a observação `NativePlayerState` continuam separados dos comandos.

O receptor e o latch nativo preservam pulsos entre envios, rejeitam sobreposição alterada, gerações antigas, relógio regressivo e filas inválidas. Pacotes repetidos não produzem novas pressões. Pausa, expiração e erro neutralizam o controle e exigem uma baseline fresca; um controle que já estava pressionado não vira uma pressão nova ao retomar.

Na prova local, uma sequência automática controla o convidado; o personagem principal fica parado. A captura nativa do principal serve para verificar independência e preparar a integração futura, sem alimentar o convidado nessa prova. O adaptador inicial aceita movimento, ataque e defesa. Outros botões ainda precisam de bindings de entrada contextualizados; isso não exige catalogar suas animações.

## Adaptador da build suportada

Alvo: Steam build 5794815 / 1.06, AMD64, SHA-256 `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`. Os hooks exigem identidade completa e digests dos pontos de entrada observados. O modo é explícito: `actor_test=native-commands`, sem sala, ticket, perfil de combate legado ou F8.

A fábrica constrói o `PadManipulator` completo, com sua alocação e inicialização nativas. Durante a atualização desse controlador, um escopo por thread fornece os eixos e níveis de botões do convidado às consultas de entrada originais. Fora desse escopo, as chamadas seguem o original. O motor continua responsável pelo mapeamento contextual, buffers de ação, comportamento e física.

Foi identificado um teste de papel que exclui jogadores adicionais do cálculo de parâmetros de comportamento. O candidato permite o cálculo somente dentro do resolver original do ator comprovadamente pertencente ao mod. O ponteiro do jogador principal, a classe do objeto e sua vtable permanecem intactos. A prova `4a4bd5e` demonstrou o caminho com movimento, ataque e defesa; os demais controles continuam pendentes. Não chamamos manualmente um segundo resolver nem repetimos tentativas anteriores de copiar vetores como prova de controle completo.

O observador exige chamadas nativas originais que escrevem os parâmetros do grafo do próprio convidado, além da visita ao teste de papel e das consultas nativas de controles mantidos. Ler de volta o comando ou o vetor fornecido pelo adaptador não basta. A observação dessas escritas não as transforma em comandos de rede, e o adaptador não chama o setter do grafo.

A direção é observada no getter chamado pelo cálculo nativo de movimento, imediatamente antes da instrução que a lê. O diagnóstico conserva o primeiro comando de caminhada em três pontos: após o Pad, na leitura do consumidor e após o resolver. Os estágios têm marcadores imutáveis e só recebem a atualização comprovada pelos checks anteriores e posteriores à leitura.

O protocolo entre lançador e DLL passa a **16**, exigindo os binários correspondentes. O recibo conserva posições relativas e consumo confirmado durante a liberação do movimento, em intervalos de pelo menos 50 ms, incluindo o primeiro neutro confirmado, e uma cauda física de 1,2 segundo após a interrupção. A cauda mantém controles neutros, não muda o resultado e não transforma entrada solicitada em prova de consumo. Posição ou consumo indisponíveis aparecem como desconhecidos; o marcador final exige posição válida, propriedade ainda publicada e coleta sem lacunas acima de 250 ms. O índice do histórico é publicado depois de preencher cada entrada. Diferenças de input na primeira falha usam resolução de um milionésimo.

O leitor novo observa as máquinas de estado ativas do grafo próprio do convidado, com tipos e campos comprovados na reflexão do executável selecionado e validados em execução. A categoria canônica determina qual família está ativa; filhos antigos que persistem durante uma mistura de animações não tornam um ataque ou uma defesa atuais. Entrada na defesa não conta como defesa sustentada. A recuperação exige um estado de repouso real, separado da ausência das flags de ataque/defesa. Categoria desconhecida, ambiguidade, tipo inválido ou troca de propriedade não viram resultado neutro.

No protocolo 16, `state_code` identifica essa categoria canônica; não é mais a variável de um ramo que podia conservar um valor antigo. As flags do histórico acrescentam `8` para resultado classificado, `16` para ataque efetivo, `32` para defesa sustentada efetiva e `64` para repouso efetivo. As três últimas só têm significado com `8`. Mudanças nessas flags geram entradas imutáveis. O transporte LAN continua na versão 3.

O novo `failed_input_sample` conserva os quatro componentes nativos da intenção e da saída do Pad, além do vetor efetivamente lido pelo consumidor, quando disponíveis na atualização que falhou. Uma janela interna de oito atualizações retém valores; o resultado público fixa somente a atualização da primeira falha, publicada antes de seu código. Flags distinguem amostra válida, chamada original de giro observada, alvo angular correspondente e leitura efetiva do consumidor. Ausências ou epochs diferentes aparecem como `null`. Esses valores permanecem diagnósticos separados dos comandos e não fornecem crédito de consumo nem classificação de ação.

Novas falhas de movimento distinguem o ponto de observação no campo `mailbox_failure.kind`, sem mudar tolerância ou requisitos de consumo:

| Código | Origem e condição |
|---|---|
| 12 | Intenção nativa de giro inválida (`invalid_turn_intent`). |
| 13 | Intenção nativa de giro diferente do comando (`turn_intent_mismatch`). |
| 14 | Movimento lido pelo resolver inválido (`invalid_resolver_movement`). |
| 15 | Movimento lido pelo resolver diferente do comando (`resolver_movement_mismatch`). |

Os códigos históricos 7 e 8 continuam reservados aos registros antigos: indicavam valor inválido ou divergente sem distinguir essas duas origens. Um registro antigo com código 8 permanece de origem desconhecida; os novos códigos não o reclassificam. O diagnóstico conserva o epoch e os valores observados da primeira falha, inclusive quando a intenção de giro é validada durante a publicação atômica da conclusão do Pad.

A quinta rodada confirmou eixos fornecidos não nulos, mas vetor zero já ao sair do Pad. O filtro depende do stick anterior. A sexta passou desse zero e encontrou uma diferença entre a intenção de direção e o movimento produzido enquanto o corpo vira. Duas sequências privadas automáticas, com os dois eixos e ambos os sentidos, comprovaram intenção correta e chamada nativa de giro; o vetor físico se alinhou duas atualizações depois. A conversão dos eixos foi preservada.

O binding observa a chamada original que calcula o giro, no chamador e escopo comprovados do convidado. Depois de seu retorno, compara o alvo angular com a intenção realmente produzida pelo motor. Isso permite diferir o início sem crédito: zero filtrado na primeira atualização e, somente com prova de giro no mesmo epoch, até as três primeiras atualizações e no máximo 250 ms para o alinhamento. A quarta precisa corresponder, mesmo sem pacote novo. Cada observação continua limitada a 100 ms de idade; o prazo cumulativo de alinhamento é separado e acompanha o timeout de consumo do cenário. Uma correspondência fecha a janela imediatamente. Baseline, suspensão e geração nova não a renovam sem atualização neutra real. O consumo ainda exige vetor físico correspondente, escritas downstream e consultas de controles mantidos; giro ou intenção sozinhos não aprovam. A política padrão portátil permanece estrita. A integração C++ concluiu a sequência inicial estrita em `4a4bd5e`.

A comparação compartilhada preserva os valores nativos observados: direção com tolerância `0,0001`, intensidade com `max(0,0001; 0,001 × maior intensidade)`. Isso trata a diferença de 0,063% registrada na rodada seguinte, sem aceitar direção trocada ou controles mantidos incorretos. A prova de caminhada exige pelo menos 0,15 m de avanço X/Z na direção comandada; queda vertical, deslocamento lateral ou reverso não contam. O limite de segurança de 4 m e a verificação da parada continuam em três dimensões.

Um binding inclui mundo, registro, ator, handles observados, controlador, componentes, raiz e módulos próprios. Mudança de identidade aposenta as referências publicadas; endereço antigo não continua selecionado. Um ator ainda válido recebe entrada neutra depois do término. Os escopos de entrada e propriedade são restaurados em retorno normal, chamadas aninhadas, exceções C++ e SEH; a limpeza possui teste com um executável próprio.

Durante a preparação, pausas de carregamento mantêm o convidado neutro e reiniciam a espera de estabilidade. Depois de armar a sequência, perda de foco, atualização expirada ou erro interrompem o teste. O recibo conserva a primeira causa de parada; não há retomada silenciosa de comandos antigos.

A criação e o verificador continuam na rotina de mundo já observada. A entrada e as consultas do comportamento podem rodar nas threads de trabalho do motor. `NativeCommandMailbox` sincroniza somente os dados do mod: os métodos liberam o mutex antes das chamadas ao Sekiro. Cada atualização recebe uma identidade monotônica, e as observações de consumo só valem para aquela atualização concluída. Callback atrasado, troca de geração, pausa ou atualização sobreposta não podem validar um comando mais novo. O snapshot devolve os valores realmente observados, após conferência, e não uma cópia da solicitação.

## Cenário e resultado

O assistente acrescenta **Testar controlador nativo neste PC**, reutilizando a preparação e os saves separados. A sequência cria uma vez, posiciona uma vez, aguarda estabilização e testa entrada neutra, caminhada, parada, ataque, soltura, defesa mantida e soltura. O limite total é 4 m; a caminhada exige avanço próprio no plano comandado e a parada tem verificação separada. Nenhuma correção posterior de posição, escrita manual no grafo ou enfileiramento manual HKB é permitido.

Antes da sequência positiva, o fornecimento de comandos é deliberadamente desligado. O cenário precisa rejeitar esse caso por ausência de consumo. Um resultado negativo diferente interrompe o teste. O controle negativo e a sequência positiva estrita foram aceitos em `4a4bd5e`; a sequência diagnóstica anterior continua inconclusiva.

Há duas modalidades explícitas do verificador:

- **Estrita:** exige binding de resultado, ações reais, defesa sustentada e liberação, além das condições de entrada e física. Somente ela pode satisfazer as asserções.
- **Diagnóstico com resultado desconhecido:** permite coletar a sequência completa e os códigos de estado reais, conservando as condições de entrada, propriedade, física e tempo. Termina **inconclusivo**, nunca aprovado. Um binding descoberto depois não valida retroativamente a coleta.

O candidato atual usa a modalidade **estrita**, com o novo leitor de resultados. A modalidade diagnóstica continua disponível explicitamente nos testes; não há fallback silencioso. Nível de botão, tempo de controle mantido ou estado escrito pelo provedor não são usados para inventar o resultado. O registro contém transições por atualização em uma lista limitada e publicada atomicamente. Ausência, overflow, erro ou timeout impede aceitação.

A preparação aguarda pelo menos 0,5 segundo de repouso nativo: a animação de entrada no mapa não pode estabelecer a origem do teste de parada. A sequência permite 2,5 segundos para recuperação do ataque, 3 segundos de comando de defesa e 1,2 segundo para sua recuperação. A defesa precisa entrar em até 1 segundo e permanecer efetivamente sustentada por pelo menos 1,5 segundo. Frescor (100 ms), consumo (250 ms) e limite tridimensional total (4 m) permanecem. Esses prazos de ação vêm das transições observadas do motor, não substituem a verificação do seu resultado.

A revisão 2 do cenário muda explicitamente a prova de parada: depois do primeiro neutro consumido, permite até 0,4 m de deslocamento residual e até 2 segundos para concluir a transição, exigindo em seguida repouso nativo e pelo menos 250 ms contínuos dentro de uma janela tridimensional de 0,05 m. A etapa dura no mínimo 500 ms. Ausência de consumo ou de repouso reinicia a janela de estabilidade, sem apagar o deslocamento acumulado; uma excursão seguida de retorno continua no envelope. A duração ativa total passa de 10 para 12 segundos para acomodar essa espera. Não há reposicionamento nem alteração da física. O antigo envelope de 0,15 m em 500 ms foi rejeitado pela rodada `b77c6b9`: controles neutros já eram consumidos, mas a transição agachada só chegou ao repouso aproximadamente 1,2 segundo depois. A revisão passou em `4a4bd5e`, sem aprovar retroativamente aquela falha.

Depois que um neutro da fase atual estabelece a origem da parada, toda amostra física fresca participa da deriva máxima, inclusive durante uma lacuna de observação do consumidor. Uma excursão seguida de retorno não desaparece do registro. O diagnóstico histórico `6516fb6` de 30/10 passou a versão antiga dessa verificação, com limite de 0,15 m em 500 ms: as seis amostras retidas ao longo de 469 ms ficaram na mesma posição. Esse resultado continua sendo diagnóstico; a prova estrita atual usa a revisão 2 descrita acima.

Veja os [resultados e limites da pesquisa](validation/native-controller-research-2026-10-28.md). O pacote público anterior não foi substituído por esta implementação.

O [binding de resultados de 30/10](validation/native-action-results-2026-10-30.md) registra a pesquisa independente, a falha intermediária e a prova estrita aceita do candidato C++. A conexão desse controlador ao TCP existente é o próximo marco; a LAN anterior permanece como diagnóstico legado explícito.
