# Pesquisa do controlador nativo — 2026-10-28

**Resultado: caminho de entrada e consumidores parcialmente demonstrados; novo adaptador candidato, sem aceitação nativa completa.** Um PC, uma instalação de teste independente, perfil separado e build exata identificada. Progresso 10%, sem novos pontos. [Plano e implementação](../NATIVE_CONTROLLER.md).

## Observado

- O `PadManipulator` completo é construído pela fábrica nativa, com componentes e módulos distintos do jogador principal. O controlador base anterior não oferece o mesmo caminho de entrada.
- A atualização nativa consulta um mapa lógico de botões e a entrada do dispositivo, preenche o módulo de solicitações do ator e finaliza seus buffers. Consultas do comportamento leem pressões e duração mantida desse mesmo módulo.
- No teste privado de entrada virtual corrigido, o motor recebeu uma pressão de ataque e uma de defesa; observou duração de defesa e posterior liberação. As 390 atualizações não usaram enfileiramentos manuais HKB ou correções de pose. O deslocamento de aproximadamente 1,86 m ocorreu durante a ação de ataque: **não é prova de caminhada**.
- Nas tentativas de caminhada, o vetor do controlador tornou-se não nulo, mas o personagem não caminhou. A observação do escritor nativo de `MoveSpeedLevel`, seguida da análise dos trechos exatos, encontrou um teste que pula o cálculo dos parâmetros para jogadores adicionais.
- A rotina exterior do módulo já era chamada pelo jogo. Uma tentativa de chamá-la novamente foi recusada pelo verificador de duplicidade; ela não foi incorporada à implementação.

Os primeiros callbacks privados tinham um retorno booleano incompatível com o tipo esperado pela ponte. Foram corrigidos para retornos inteiros de um byte; os resultados com erros anteriores não são aceitos como prova.

## Evidências retidas

Dados proprietários, parâmetros, endereços de heap, saves e decompilações ficam na área privada ignorada. Estes IDs identificam as observações REA retidas; não são downloads públicos:

| Registro | Evidência | Alcance |
|---|---|---|
| Consultas nativas do comportamento | `42a08a009ca8fd0ca5383a173fa72a3de618aea74aac86c9421e92448223d1e8` | Observação do produtor/consumidor do jogador original, sem comandos. |
| Entrada virtual corrigida | `76118816d9d1ab2075e83cbb3ef3f88fbfad213265005c6e28a6b9ef7b402f8e` | Pressão, duração mantida e liberação; não comprovou caminhada nem defesa efetiva sustentada. |
| Escritor do parâmetro de movimento | `c6575193ad793eed242a82126e38191b72811d7eab60c4c3df423c6408a7b8ba` | Watchpoint limitado, removido após a primeira escrita; caminho original do resolver identificado. |
| Vetores do controlador e ausência de resolver interno | `917f4b4221480b3250d4104e2f4f5f025ebf32d9f07f2f1ff227dd74b5d5b36c` | Vetor não nulo sem caminhada; contradição preservada. |
| Recusa de segundo resolver | `627d83e574264e5fd37ec2cadc957579fa031098d0532fec17ec27349ea8dc6a` | Caminho duplicado recusado, sem aceitação. |

A análise dos trechos selecionados identificou o produtor `7EF120`, mapeamento `B258D0`, consultas `95FD80` e família `9603xx`, consumidores `B2B6D0`/`B2B380` e resolver `B49D30`. O teste de papel `9F36C0` diferencia o jogador principal do adicional. Esses RVAs são específicos desta build; os digests de entrada publicados não contêm código do jogo. O uso restrito desse teste no novo adaptador é **hipótese implementada, ainda não validada em execução**.

## Implementado e verificado fora do jogo

Contrato de comandos, captura por atualização, receptor com contexto de sessão, latch de pressões/manutenção/liberação, projeção de eixos, adaptador C++ experimental, preparação no assistente, cenário automático estrito/diagnóstico e recibos com transições limitadas. O modo novo tem um único mecanismo de controle e não instala o driver HKB legado.

Testes incluem pulsos rápidos, duplicação, reordenação, perda/expiração, geração antiga, baseline, filas inválidas, sequências com seed, ausência do provedor, observação apenas do writeback, movimento ausente, controle mantido sem prova, falta de liberação e limpeza dos escopos sob exceções. Esses testes avaliam contratos e verificadores, não a integração nativa no Sekiro.

Validação local desta implementação: build MSVC x64 Release sem avisos; **33/33 suites CTest** e **26/26 testes Python** aprovados; `tools/progress.py --check` confirma 10%. A nova suite usa três threads reais para testar entrada, atualização e observação simultâneas, callbacks atrasados, falta de evidência, valores efetivamente observados e neutralização. O protocolo entre lançador e DLL passa a 13 e exige atualizar os binários juntos. O protocolo LAN permanece em 3; o novo modo de comandos ainda não é enviado pela rede.

## Sessão e pendências

A última sessão de pesquisa atingiu o limite de 30 minutos do host e terminou com código 70. Não conta como saída normal ou cenário aceito. As verificações independentes confirmaram saves pessoais, backup e configuração gráfica inalterados. Sessões REA e instrumentação de pesquisa foram encerradas.

Depois disso, o primeiro candidato C++ `99aab2d05396d114bb66caa45eb8bc0434546eee` foi executado na cópia protegida: hooks carregados, uma criação e um posicionamento. O teste foi interrompido durante a preparação, com **zero comandos emitidos e zero consumidos**. Houve uma pausa de 998 ms no callback; o verificador tratava pausas anteriores à sequência como falha ativa. Isso não demonstra controle ou caminhada. A saída foi normal, código 0, sem timeout, com saves pessoais, backup e gráficos preservados; os hashes dos saves foram reconferidos independentemente.

A preparação foi corrigida para manter entrada neutra e aguardar estabilização antes de armar comandos. As condições de interrupção da sequência ativa permanecem. O recibo agora identifica a primeira causa interna de parada, permitindo distinguir pausa, propriedade, captura e comandos em uma próxima execução; a causa fina do primeiro corte não havia sido registrada. A correção ainda requer teste no jogo.

O candidato seguinte, `75d5b68e326523a1d233e27e2615237126e0536b`, registrou a causa exata: a atualização do controlador adicional ocorreu em uma thread diferente da rotina que o criou. A exigência anterior de uma única thread interrompeu o teste antes de qualquer comando; não houve prova de movimento. O jogo encerrou normalmente, código 0, com saves pessoais, backup e gráficos preservados e hashes dos saves reconferidos independentemente.

A integração foi corrigida para verificar o ator e a fase da chamada nativa, com entrega e observações sincronizadas por atualização. A caixa de comandos rejeita observações anteriores à conclusão do controlador e não combina evidências parciais de resolvers diferentes. A prova completa no Sekiro continua pendente; os avanços parciais estão registrados abaixo.

O candidato `d35fc0de913dbe1f9547ee83773345dbeec4612e` avançou no jogo: o controle negativo foi rejeitado corretamente e o registro contabilizou 16 comandos emitidos, 15 consumidos na fase neutra, com observações nativas do resolver e dos controles mantidos. A sequência foi interrompida antes da caminhada por uma falha de entrega ainda sem subtipo registrado. **Não comprova caminhada, ataque ou defesa.** Saída normal, código 0; saves pessoais, backup e gráficos preservados, com reconferência independente dos hashes dos saves.

O participante também informou que precisava criar um jogo novo a cada candidato. O erro era do preparo privado dos pacotes separados: o perfil existente era copiado para uma pasta diferente daquela que o botão usa. A configuração pré-conferida não era a configuração posteriormente criada pelo assistente. Os perfis anteriores permanecem preservados. O preparo foi corrigido para copiar o perfil co-op e seu marcador de propriedade para o caminho exato por conta, sem importar o save pessoal; essa correção precisa ser confirmada pelo carregamento do jogo existente.

O recibo passa a conservar o primeiro subtipo de falha da caixa de comandos, com atualização, valores esperados/observados e origem do erro. Evidências incompatíveis continuam sendo recusadas; o diagnóstico não relaxa o verificador. Valores não finitos são serializados por sentinela inteira, sem JSON inválido.

## Continuação — leitura no consumidor

O candidato `a5630e6b52640c44dd02f763b59d4c04e25262ec` carregou o perfil co-op no caminho efetivamente usado pelo assistente. Registrou o controle negativo esperado, 30 comandos emitidos e 29 consumidos durante a entrada neutra. No primeiro comando de caminhada, a caixa recusou a diferença entre direção mundial solicitada `(0, 1)`, intensidade `0,35`, e direção observada zero. A sequência não foi aprovada. O jogo encerrou normalmente, código 0, sem timeout; saves pessoais, backup e gráficos preservados, com hashes reconferidos independentemente.

Uma observação limitada de quatro segundos confirmou o getter `9EFEA0` retornando o controlador do convidado ao chamador `B4B019`, 239 vezes, em threads de trabalho do motor. Os trechos retidos mostram a leitura do vetor imediatamente após esse retorno. Evidência `a58efdc46a1e6ed722093ccb83c2483cec116d6d7a9f41a1e66f7739e1ba7876`. Não houve alteração de argumentos, resultados, valores do jogo ou chamadas de funções do jogo; apenas instrumentação temporária de observação, removida ao terminar.

O validador passa a observar esse ponto do consumidor, conservando a exigência das escritas nativas posteriores e das consultas de controles mantidos. O diagnóstico registra os valores após o produtor, no consumidor e após o resolver para distinguir entrada ignorada de leitura tardia. A causa da divergência ainda precisa ser demonstrada; não se presume que o motor limpou o vetor.

Outra leitura confirmou, no original e no convidado, que o índice global 384 é mapeado para o slot local 164, com tipo nativo int32 e valor zero no estado observado. Evidência `e8b3e822f07fb0af0cc7e4afd216d1fefb61acbbc31d2a99a0e20a74a294b49f`. O diagnóstico anterior indexava a metadata pelo índice global e lia dois bytes do tipo; foi corrigido para usar o slot local e o byte de tipo. Isso corrige a leitura, sem classificar automaticamente ataque ou defesa.

As observações REA foram encerradas. O novo ponto de leitura e o diagnóstico foram executados na rodada seguinte, registrada abaixo. Não há novos pontos, aceitação de combate ou teste novo entre máquinas.

## Continuação — filtro da primeira entrada de caminhada

O candidato `a10e4ef356429d63ec262e0522afec3335a89543` carregou o perfil co-op existente no caminho do assistente, rejeitou o controle negativo e registrou 31 comandos emitidos e 30 consumidos na fase neutra. No primeiro comando de movimento, todas as seis consultas de eixos foram observadas; a projeção forneceu `(-0,35, 0)` com base zero. O vetor era zero nos três pontos: após o Pad, na leitura exata do consumidor e depois do resolver. Portanto, a hipótese de leitura apenas tardia não explica essa rodada. Houve uma criação, um posicionamento inicial, zero comandos legados e zero escritas manuais no grafo. Saída normal, código 0, sem timeout; saves pessoais, backup e gráficos preservados, com hashes dos saves reconferidos independentemente.

Uma captura somente de leitura da mesma sessão reteve os trechos selecionados. A análise REA/Ghidra do procedimento `7F0F50` demonstra que o filtro lê o vetor anterior em `+340`, armazena o atual e zera a saída se o anterior era zero. Evidência `f6cbe6261c245b6410bc370e33dc38bd58178dbbc58b4c24f76da79464875f78`. Trata-se de análise estática de um carrier privado com bytes retidos, sem execução do carrier; os protótipos e constantes ausentes da decompilação não são tratados como código-fonte original.

Uma observação REA de três segundos, com o convidado já neutro após a parada, viu 180 chamadas desse procedimento e 180 visitas ao ramo de saída zerada, com vetor anterior zero. Evidência `8f0ae4e0cdc1c04d1138801bb12b6234bcee4b213d26bc2b8a6bf0abab23fb67`. Não houve alteração de argumentos, retornos, valores do jogo ou chamadas manuais de funções; a instrumentação foi removida. Essa observação neutra corrobora o ramo, mas não demonstra a segunda atualização com entrada positiva.

O candidato seguinte permite apenas que a primeira atualização nativa de neutro para movimento devolva zero sem crédito de consumo. Não há tolerância por tempo renovada a cada pacote: a atualização seguinte deve corresponder ou falhar. Baseline, suspensão e troca de geração sem uma atualização nativa neutra não renovam a exceção. A comparação de direção e intensidade permanece exata dentro da tolerância original, e todos os requisitos de propriedade, consultas downstream, controles mantidos, física, neutralização e controle negativo continuam. Testes portáteis cobrem zero inicial sem crédito, callback repetido, observação antiga, ausência de pacote novo, zero persistente, perda posterior, direção incorreta e tentativas de renovar a exceção por rebaseline; a política padrão segue estrita. Esta correção ainda não é prova de caminhada. O procedimento também recebe uma âncora adicional de compatibilidade.

Ainda faltam comprovar o caminho completo com caminhada física e consumo downstream, vincular resultados reais de ataque/defesa e concluir a sequência positiva no jogo. O controle negativo já foi observado. Depois, ligar comandos versionados ao TCP existente e validar dois jogos, contato, dano e postura com autoridade do anfitrião. Não há aceitação nova de LAN, Steam, combate compartilhado ou quatro participantes.

## Continuação — intenção e alinhamento do corpo

O candidato `f83e4bb1fcb623ae8a57f19f18f100ffe1541e0b` rejeitou o controle negativo, emitiu 32 comandos e registrou 30 consumidos na fase neutra. O primeiro zero de caminhada ficou sem crédito, conforme previsto. Na atualização seguinte, a direção observada tinha intensidade `0,35`, mas sentido oposto ao solicitado. A sequência parou; isso não foi aceito como caminhada. O perfil carregado era o previsto pelo assistente. Saída normal, código 0, sem timeout; saves pessoais, backup e gráficos preservados, com hashes dos saves reconferidos independentemente.

A observação somente de leitura `2aa1ece138762bc7f589c52cb8d7a58053da3acc503bb2081c35ce325a1e52fd` encontrou o ramo de cálculo livre em 180 chamadas durante três segundos, além da matriz do corpo. Em seguida, duas sequências privadas e explicitamente limitadas forneceram entrada somente ao procedimento nativo do convidado: 30 atualizações cada, duas janelas de seis atualizações com intensidade `0,2`, no máximo 700 ms ativos e limite físico de 1 m. Os quatro sentidos dos dois eixos foram exercitados sem movimentos do participante.

As sequências observaram a intenção mundial produzida em `+C0` correta, enquanto o vetor físico `+10` ainda refletia a orientação anterior por duas atualizações. Na terceira atualização não nula ele se alinhou à intenção. Portanto, inverter a projeção para satisfazer o primeiro vetor físico seria incorreto. Evidências `98d9fe510773460a78a64da50214b343f72aff9b80d06e5adfb4c382addc3942` e `bd9c69b8a59efcc2a84755629ba9b26f2157daca3153441e0d9ffdaa1a46f798`. Houve deslocamentos curtos, inferiores ao mínimo da prova; não substituem a caminhada positiva do adaptador C++.

A segunda sequência também observou a chamada original `7EEF80`, a partir de `7F1403`, recebendo o alvo angular correspondente à intenção e a orientação atual. A captura retida da mesma build confirma os três ponteiros e o retorno pelo buffer de saída. A instrumentação temporária substituiu somente as quatro consultas de eixos, no escopo comprovado do convidado, e encaminhou os outros contextos ao original; não houve chamadas manuais de funções do jogo, eventos HKB, escritas no grafo, reposicionamentos ou entradas no personagem principal. As substituições foram removidas ao terminar. Esses experimentos de pesquisa ocorreram após a falha da sequência C++, não a retomaram e não mudam seu resultado.

O candidato passa a observar essa chamada original, sem alterar argumentos ou retorno. Depois do Pad, confere o alvo de giro contra a intenção nativa produzida e associa a prova à atualização concluída. Somente essa prova adicional autoriza diferir um vetor físico ainda desalinhado nas três primeiras atualizações, por até 100 ms. Não recebe crédito até o vetor físico corresponder e as demais evidências estiverem presentes. A primeira correspondência fecha a janela; novo pacote, baseline, suspensão ou geração não a renovam sem entrada neutra realmente processada. O padrão estrito e a política anterior de um zero continuam disponíveis nos testes. A nova integração C++ ainda não foi executada; resultados de ataque e defesa continuam sem classificação.
