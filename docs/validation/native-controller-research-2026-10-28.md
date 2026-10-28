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

A integração foi corrigida para verificar o ator e a fase da chamada nativa, com entrega e observações sincronizadas por atualização. A caixa de comandos rejeita observações anteriores à conclusão do controlador e não combina evidências parciais de resolvers diferentes. Essa correção passou nos testes independentes; sua execução no Sekiro ainda está pendente.

Ainda faltam executar o novo caminho restrito de propriedade, comprovar caminhada física com consumo downstream, vincular resultados reais de ataque/defesa e executar a sequência positiva e o controle negativo no jogo. Depois, ligar comandos versionados ao TCP existente e validar dois jogos, contato, dano e postura com autoridade do anfitrião. Não há aceitação nova de LAN, Steam, combate compartilhado ou quatro participantes.
