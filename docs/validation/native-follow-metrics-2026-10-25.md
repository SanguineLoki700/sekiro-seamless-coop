# Medição do acompanhamento e falha da coleta — 25/10/2026

Rodada local com uma pessoa e uma máquina, fonte `5ee6add`. O participante
confirmou que desta vez o jogo ficou leve tanto acompanhando quanto pausado.
O personagem adicional continuava sem animação de caminhada. Não se mediu FPS
de apresentação; a cadência abaixo pertence ao callback nativo do mundo.

## Medidas retidas

F8 criou uma vez o ator. F9 pausou as escritas de acompanhamento, mantendo o
ator e as verificações de identidade. As amostras periódicas do host registraram
contadores cumulativos do adaptador, tempo de CPU e memória do processo.

| Estado | Janelas comparáveis | Atualizações nativas/s | Custo médio do callback |
|---|---:|---:|---:|
| Original carregado, antes de F8 | 15 | 60,08 | 0,45 µs |
| Acompanhando | 4 | 60,11 | 4,94 ms |
| Movimento pausado | 2 | 55,83 | 4,96 ms |

São medianas de médias por janela, em intervalos curtos e sem controle de carga
de GPU/câmera. A amostra pausada é especialmente pequena. A chamada síncrona da
fábrica levou115µs; isso não mede carregamento assíncrono do modelo. Foram331
escritas de posição em532 atualizações do experimento. Um intervalo superior
a250ms encerrou os comandos conforme a proteção do teste. Isso não encerra
o processo nem remove o ator.

O custo semelhante com e sem setter motivou reduzir as consultas de páginas
repetidas. A versão seguinte reutiliza respostas `VirtualQuery` dentro de um
único callback serializado, limpa esse cache a cada callback e antes/depois da
fábrica e mantém os guards de acesso. O ganho ainda requer medição no jogo.

O payload/status privado desta rodada usa versão3; lançador e DLL precisam
ser atualizados juntos. Há controles de diagnóstico F9 (pausar/retomar) e F10
(encerrar os comandos do experimento), sem despawn manual.

## Encerramento inesperado explicado

O participante relatou que o jogo fechou sozinho. A Steam registrou **70 para
o jogo e1 para o lançador**, e faltou o recibo final. Não é saída normal aceita.

O arquivo alcançou130.711bytes, perto do limite de131.072. A próxima amostra
periódica ultrapassava esse limite: `Receipt::emit` lançava uma exceção, o catch
do host chamava `Child::stop`, que termina seu filho com70, e a tentativa de
gravar o recibo final também excedia o limite, resultando em1 no host. Esta é
uma regressão introduzida na coleta periódica. Não há evidência para atribuir
o fechamento ao Alt+Tab nem a uma exceção de acesso dentro do ator.

A correção reserva32KiB para eventos obrigatórios/final e descarta novas
amostras opcionais quando o espaço reservado seria consumido. Emite um marcador
de limite uma vez e continua monitorando o jogo. Um teste de regressão alimenta
30minutos de amostras e verifica que ainda cabem o marcador e um recibo final
de tamanho máximo. O teto total do arquivo continua128KiB.

Após a falha, hashes dos saves originais e do perfil co-op anterior permaneceram
intactos. O par de binários aceito anteriormente e sua configuração também foram
preservados. Steam conferida com opção vazia; nenhum jogo/host ficou aberto.

As correções passam na compilação MSVC Release, **16 suítes CTest**, 26 testes
Python e verificação do ledger. Testes Windows adicionais cobrem reutilização
de consultas, mudança de proteção entre callbacks e invalidação no limite de
alocação. A nova execução no jogo e animações continuam pendentes. Progresso
funcional da v1 permanece **10%**.
