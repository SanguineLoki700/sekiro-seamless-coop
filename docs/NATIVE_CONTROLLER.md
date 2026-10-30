# Controlador remoto nativo

O caminho em implementação é **comandos de jogador → controlador exclusivo do ator → comportamento nativo → ações, física e animações do Sekiro**. O segundo `PlayerIns` já é um ator criado pela fábrica do jogo. O trabalho atual muda a origem de seu controle e comprova seu processamento independente.

O lançador, assistente, instalação de teste, perfis `.co2`, backups, DLL, identificação da build, fábrica de atores, TCP autenticado, sessões e histórico de estados continuam no projeto. Os testes anteriores de eventos HKB permanecem como diagnóstico legado. O novo modo não os instala, não os usa como fallback e não muda o transporte LAN para começar essa investigação.

## Ordem aceita e situação

| Etapa | Condição para avançar | Situação |
|---|---|---|
| 1. Caminho nativo completo | Entrada, consumidor, proprietário e fase demonstrados no ator adicional. | Entrada e consumidores identificados; ramo restrito do resolver observado com comandos neutros. Consumo completo de movimento e ações ainda pendente. |
| 2. Prova automática em um PC | Comandos independentes, consumo downstream, deslocamento físico, controles mantidos/liberados e teste negativo. | Rodadas automáticas executadas: controle negativo rejeitado, entrada neutra consumida; sequência positiva interrompida no início da caminhada. |
| 3. LAN existente | Mesmo adaptador alimentado pelo TCP atual; comandos com formato explicitamente versionado e proteção de sessão. | Pendente da prova do controlador. A LAN anterior continua usando o caminho legado. |
| 4. Combate compartilhado | Ataque, contato, defesa, dano e postura; anfitrião confirma consequências. | Pendente. A primeira entrega jogável continua sendo uma luta em dupla contra o mesmo inimigo. |
| 5. Ampliar e otimizar | Famílias de ações, habilidades, chefes, medições de rede, Steam, quatro jogadores e continuidade. | Pendente. Testes em um PC não substituem dois jogos ou quatro máquinas. |

O progresso permanece em **10%**. Código, ferramentas, testes simulados ou um resultado inconclusivo não concedem pontos.

A oitava rodada consumiu comandos móveis e registrou 0,666 m de avanço na direção comandada, com uma criação e apenas o posicionamento inicial. A sequência parou na verificação da parada, antes de ataque e defesa. A nona interrompeu antes da parada, por diferença de intensidade entre 0,35 e 0,349454; o código histórico não distingue em qual estágio ela surgiu. Esses resultados parciais não concluem o controlador nem alteram os pontos.

Na décima rodada, o diagnóstico distinguiu a falha no vetor lido pelo resolver. A terceira atualização de caminhada começou 109 ms após a primeira, além do prazo cumulativo antigo de 100 ms. A janela ainda estava aberta e nenhum movimento divergente recebeu crédito. Esse resultado motivou separar o prazo de alinhamento da validade de cada observação; não demonstra que a sequência positiva esteja concluída.

## Contratos e controle

`PlayerCommand` separa intenção de ação, estado mantido e transições ordenadas de pressionar/soltar. Sequência, tempo e geração acompanham o comando. O contexto confiável do receptor acrescenta a encarnação da sessão, conexão e jogador; esses identificadores não são aceitos apenas porque um pacote os declara. A captura retém transições a cada atualização, antes de um envio mais lento. Os snapshots de estado existentes e a observação `NativePlayerState` continuam separados dos comandos.

O receptor e o latch nativo preservam pulsos entre envios, rejeitam sobreposição alterada, gerações antigas, relógio regressivo e filas inválidas. Pacotes repetidos não produzem novas pressões. Pausa, expiração e erro neutralizam o controle e exigem uma baseline fresca; um controle que já estava pressionado não vira uma pressão nova ao retomar.

Na prova local, uma sequência automática controla o convidado; o personagem principal fica parado. A captura nativa do principal serve para verificar independência e preparar a integração futura, sem alimentar o convidado nessa prova. O adaptador inicial aceita movimento, ataque e defesa. Outros botões ainda precisam de bindings de entrada contextualizados; isso não exige catalogar suas animações.

## Adaptador da build suportada

Alvo: Steam build 5794815 / 1.06, AMD64, SHA-256 `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`. Os hooks exigem identidade completa e digests dos pontos de entrada observados. O modo é explícito: `actor_test=native-commands`, sem sala, ticket, perfil de combate legado ou F8.

A fábrica constrói o `PadManipulator` completo, com sua alocação e inicialização nativas. Durante a atualização desse controlador, um escopo por thread fornece os eixos e níveis de botões do convidado às consultas de entrada originais. Fora desse escopo, as chamadas seguem o original. O motor continua responsável pelo mapeamento contextual, buffers de ação, comportamento e física.

Foi identificado um teste de papel que exclui jogadores adicionais do cálculo de parâmetros de comportamento. O candidato permite o cálculo somente dentro do resolver original do ator comprovadamente pertencente ao mod. O ponteiro do jogador principal, a classe do objeto e sua vtable permanecem intactos. A execução já demonstrou o ramo com entrada neutra; seu efeito completo com movimento e ações ainda precisa de prova. Não chamamos manualmente um segundo resolver nem repetimos tentativas anteriores de copiar vetores como prova de controle completo.

O observador exige chamadas nativas originais que escrevem os parâmetros do grafo do próprio convidado, além da visita ao teste de papel e das consultas nativas de controles mantidos. Ler de volta o comando ou o vetor fornecido pelo adaptador não basta. A observação dessas escritas não as transforma em comandos de rede, e o adaptador não chama o setter do grafo.

A direção é observada no getter chamado pelo cálculo nativo de movimento, imediatamente antes da instrução que a lê. O diagnóstico conserva o primeiro comando de caminhada em três pontos: após o Pad, na leitura do consumidor e após o resolver. Os estágios têm marcadores imutáveis e só recebem a atualização comprovada pelos checks anteriores e posteriores à leitura.

O protocolo entre lançador e DLL passa a **15**, exigindo os binários correspondentes. O recibo conserva posições relativas e consumo confirmado durante a liberação do movimento, em intervalos de pelo menos 50 ms, incluindo o primeiro neutro confirmado, e uma cauda física de 1,2 segundo após a interrupção. A cauda mantém controles neutros, não muda o resultado e não transforma entrada solicitada em prova de consumo. Posição ou consumo indisponíveis aparecem como desconhecidos; o marcador final exige posição válida, propriedade ainda publicada e coleta sem lacunas acima de 250 ms. O índice do histórico é publicado depois de preencher cada entrada. Diferenças de input na primeira falha usam resolução de um milionésimo.

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

O binding observa a chamada original que calcula o giro, no chamador e escopo comprovados do convidado. Depois de seu retorno, compara o alvo angular com a intenção realmente produzida pelo motor. Isso permite diferir o início sem crédito: zero filtrado na primeira atualização e, somente com prova de giro no mesmo epoch, até as três primeiras atualizações e no máximo 250 ms para o alinhamento. A quarta precisa corresponder, mesmo sem pacote novo. Cada observação continua limitada a 100 ms de idade; o prazo cumulativo de alinhamento é separado e acompanha o timeout de consumo do cenário. Uma correspondência fecha a janela imediatamente. Baseline, suspensão e geração nova não a renovam sem atualização neutra real. O consumo ainda exige vetor físico correspondente, escritas downstream e consultas de controles mantidos; giro ou intenção sozinhos não aprovam. A política padrão portátil permanece estrita. A integração C++ avançou até comandos móveis consumidos; a sequência completa ainda não foi aceita.

A comparação compartilhada preserva os valores nativos observados: direção com tolerância `0,0001`, intensidade com `max(0,0001; 0,001 × maior intensidade)`. Isso trata a diferença de 0,063% registrada na rodada seguinte, sem aceitar direção trocada ou controles mantidos incorretos. A prova de caminhada exige pelo menos 0,15 m de avanço X/Z na direção comandada; queda vertical, deslocamento lateral ou reverso não contam. O limite de segurança de 4 m e a verificação da parada continuam em três dimensões.

Um binding inclui mundo, registro, ator, handles observados, controlador, componentes, raiz e módulos próprios. Mudança de identidade aposenta as referências publicadas; endereço antigo não continua selecionado. Um ator ainda válido recebe entrada neutra depois do término. Os escopos de entrada e propriedade são restaurados em retorno normal, chamadas aninhadas, exceções C++ e SEH; a limpeza possui teste com um executável próprio.

Durante a preparação, pausas de carregamento mantêm o convidado neutro e reiniciam a espera de estabilidade. Depois de armar a sequência, perda de foco, atualização expirada ou erro interrompem o teste. O recibo conserva a primeira causa de parada; não há retomada silenciosa de comandos antigos.

A criação e o verificador continuam na rotina de mundo já observada. A entrada e as consultas do comportamento podem rodar nas threads de trabalho do motor. `NativeCommandMailbox` sincroniza somente os dados do mod: os métodos liberam o mutex antes das chamadas ao Sekiro. Cada atualização recebe uma identidade monotônica, e as observações de consumo só valem para aquela atualização concluída. Callback atrasado, troca de geração, pausa ou atualização sobreposta não podem validar um comando mais novo. O snapshot devolve os valores realmente observados, após conferência, e não uma cópia da solicitação.

## Cenário e resultado

O assistente acrescenta **Testar controlador nativo neste PC**, reutilizando a preparação e os saves separados. A sequência cria uma vez, posiciona uma vez, aguarda estabilização e testa entrada neutra, caminhada, parada, ataque, soltura, defesa mantida e soltura. O limite total é 4 m; a caminhada exige avanço próprio no plano comandado e a parada tem verificação separada. Nenhuma correção posterior de posição, escrita manual no grafo ou enfileiramento manual HKB é permitido.

Antes da sequência positiva, o fornecimento de comandos é deliberadamente desligado. O cenário precisa rejeitar esse caso por ausência de consumo. Um resultado negativo diferente interrompe o teste. Esse controle negativo já foi observado nas rodadas recentes; a sequência positiva completa continua pendente.

Há duas modalidades explícitas do verificador:

- **Estrita:** exige binding de resultado, ações reais, defesa sustentada e liberação, além das condições de entrada e física. Somente ela pode satisfazer as asserções.
- **Diagnóstico com resultado desconhecido:** permite coletar a sequência completa e os códigos de estado reais, conservando as condições de entrada, propriedade, física e tempo. Termina **inconclusivo**, nunca aprovado. Um binding descoberto depois não valida retroativamente a coleta.

O candidato usa a segunda modalidade porque os códigos que comprovam ataque e defesa efetivos ainda não estão classificados. Nível de botão, tempo de controle mantido ou estado escrito pelo provedor não são usados para inventar esse resultado. O registro contém transições de estado por atualização em uma lista limitada e publicada atomicamente, preservando mudanças curtas entre leituras mais lentas da interface. Ausência, overflow, erro ou timeout impede aceitação.

Depois que um neutro da fase atual estabelece a origem da parada, toda amostra física fresca participa da deriva máxima, inclusive durante uma lacuna de observação do consumidor. Uma excursão seguida de retorno não desaparece do registro. Essa correção conserva o limite de 0,15 m, a duração de 500 ms, a distância tridimensional e a exigência de consumo nativo para estabelecer a origem; não resolve nem aprova a parada observada no jogo.

Veja os [resultados e limites da pesquisa](validation/native-controller-research-2026-10-28.md). O pacote público anterior não foi substituído por esta implementação.
