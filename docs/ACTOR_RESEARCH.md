# Personagens: primeira observação nativa

Continuação em [criação de personagens, 24/10](ACTOR_CREATION.md): tabela ligada
em execução, atualização observada e distinção dos caminhos de `EnemyIns` e
`PlayerIns` adicional. Este registro preserva a primeira coleta de23/10.

Pesquisa de **23/10/2026**, uma pessoa e uma máquina. A sonda reconheceu o
jogador local e os registros selecionados de uma coleção do jogo. **Nenhum
segundo personagem foi criado, movido ou conectado.** O critério
`actors-spawn` está em andamento, sem pontos; o total da v1 permanece **10%**.

## Identidade e procedimento

- Steam 1.06 / produto 1.6.0.0 / build 5794815, AMD64.
- Executável de 68.005.144 bytes; imagem mapeada de `0x42d2000` bytes.
- SHA-256: `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
- Fonte da sonda testada: `1044138a393805647d78a597669220856220f4e0`.

O responsável configurou a opção da Steam e carregou o personagem em uma
instalação de teste independente. Usamos os mesmos binários de lançador e
runtime aceitos anteriormente, com perfil `.co2` e proteção externa dos saves
originais; nenhum runtime novo foi instalado. READY e a presença da DLL correta
foram conferidos antes da confirmação de carregamento pelo responsável.

As novas coletas usaram somente consulta e leitura do processo selecionado.
Caminho, tempo de criação, hash em disco, arquitetura, módulo e tamanho da
imagem foram conferidos. Não houve escrita de memória, chamada de função do
jogo, thread remota ou hook de personagens. O runtime de saves já existente
continuou ativo; a afirmação de somente leitura descreve estas novas sondas,
não todo o processo do jogo.

O responsável fechou por **Alt+F4**. O host registrou saída **0**, sem timeout,
sem processo estrangeiro e com saves originais e configuração gráfica
inalterados. Depois, a ausência do jogo/lançador, os hashes originais e o campo
vazio de Opções de inicialização foram conferidos. Isso não testa a durabilidade
do save co-op após encerramentos abruptos, nem substitui o ciclo de saves
aceito anteriormente. Não houve restauração de saves.

## Campos corroborados no alvo

Todos os RVAs abaixo pertencem exclusivamente à identidade acima. Os nomes
vêm de RTTI MSVC observado; não são símbolos de depuração originais.

| Elemento | Evidência observada | Limite |
|---|---|---|
| Global candidato `0x3d7a1e0` | Ponteiro para RTTI `WorldChrManImp@NS_SPRJ`, COL válido e ajuste de objeto zero. | Não define o layout inteiro do gerenciador. |
| `WorldChrMan + 0x88` | RTTI `PlayerIns@NS_SPRJ`, ajuste zero. O caminho carregado em `0x6c1a10` seleciona este campo para a entidade `0x2710`. | Reconhece o candidato local observado; não cria nem controla outro jogador. |
| Header em `WorldChrMan + 0x40` | Inteiro de 32 bits e ponteiro em `+0x48`. O código em `0xa4a3f0` acessa essa coleção. | Não foi demonstrado que ela contenha todos os NPCs ou todos os atores. |
| Registros com passo `0x38` | O getter em `0xa07b50` multiplica o índice por `0x38` e lê o primeiro ponteiro do registro. | Tamanho do registro observado, sem significado comprovado para os campos restantes. |

O endereço global teve origem como candidato na pesquisa do mod antigo. Os
campos novos foram derivados do código do **nosso alvo identificado** e
conferidos em execução; não foram aceitos por transferência de layout de
Dark Souls ou de outro executável de Sekiro.

Na amostra parada, o header indicou **seis registros**: o primeiro apontava para
o mesmo `PlayerIns` de `+0x88`; os outros cinco eram nulos. As releituras dos
links e metadados selecionados permaneceram iguais. Isso não significa seis
personagens, seis jogadores, limite de participantes ou seis slots livres
utilizáveis para coop. Leituras iguais não produzem uma fotografia atômica nem
garantem a vida útil futura dos objetos.

## Sonda pública de leitura

Após compilar, o modo explícito é:

```text
skcoop_probe --exe <executável da instalação de teste aprovada> --actors
```

A ferramenta não inicia Sekiro. A identidade incompatível é recusada antes de
consultar processos. Os modos anteriores, padrão e `--rtti`, continuam
disponíveis separadamente.

- Aceita somente contagens entre 0 e 64; esse teto é da **sonda**, não do jogo.
- Lê no máximo os primeiros oito registros; quando houver mais, informa
  explicitamente que a amostra é parcial.
- Limita os campos selecionados a **512 bytes do heap e 4.096 bytes do módulo**,
  além das verificações iniciais de identidade. A amostra real usou **192/1.072**.
- Verifica bounds, overflow, regiões comprometidas/legíveis, leituras completas
  e RTTI/COL. Manager e primary exigem os tipos conhecidos e ajuste zero.
- Reconfere registros, tipos, header, primary, global e vida do processo antes
  de aceitar a amostra. Mudança, tipo indisponível ou orçamento excedido causa
  recusa, sem usar um resultado parcial como sucesso.
- Não publica endereços do heap. Os flags de capacidade da saída descrevem
  somente a própria sonda, não a DLL de saves carregada no jogo.

Os testes de política cobrem contagens inválidas, amostras parciais, limites de
endereço/tamanho/índice e recusa de executável incorreto com `--actors`.
Compilação MSVC Release e **13/13 conjuntos CTest** passaram, incluindo os
testes anteriores do lançador, runtime e saves. Esses testes independentes não
executam o jogo nem comprovam multiplayer.

## Caminho interno de criação: identificado, ainda não invocado

A referência ao texto `Create Debug Chr` em `0xa49670` levou a dois contextos
do gerenciador. Leituras selecionadas confirmaram seus tipos:

| Campo | RTTI observado | Alvo virtual selecionado |
|---|---|---|
| `WorldChrMan + 0x3148` | `CSDebugChrCreator@NS_SPRJ` | Vtable `0x2a952a8`, slot `+0x20` aponta para `0xc103b0`. |
| `WorldChrMan + 0x3150` | `CSDebugChrPerfChecker@NS_SPRJ` | Vtable `0x2a964c8`, slot `+0x20` aponta para `0xc13820`. |

Na análise do código carregado, `0xc0f3a0` registra ações com o callback
`0xc10b70`. Esse callback direciona a ação 1 para `0xc10880`; as ações 2 e 3
seguem outros caminhos do gerenciador. O caminho da ação 1 prepara dados usando
o jogador local e alcança o slot virtual `+0x20`. A rotina `0xc103b0` grava a
ação em `this+0x44` e examina um estado em `this+0x40`.

O consumidor candidato em `0xc0f7a0` também está entre os bytes retidos. A
vtable em disco aponta para ele no slot `+0x48`; o código conserva `this`, copia
o pedido de `+0x44` para o estado em `+0x40` e despacha uma tabela obtida de
`this+0x8`, com descritores de 16 bytes. Os callbacks concretos dessa tabela
ainda não foram identificados. O setter examina o estado anterior, sem
estabelecer validação de qualquer novo número de pedido escolhido pelo mod.

**Inferência:** o fluxo usa pedidos e uma máquina de estados, em vez de
comprovar uma API imediata de criação. Ainda precisamos resolver os callbacks,
recursos necessários, falhas, thread correta e remoção antes de invocar o
caminho. Nenhuma dessas funções foi chamada pela pesquisa. Textos de IDs e
botões do menu não validam estruturas ou parâmetros de spawn.

Três helpers de pose usados nesse caminho (`0x9eeb40`, `0x9eec10`, `0x9f0870`)
encaminham para componentes obtidos de `this+0x50`. Os destinos não foram
retidos nesta coleta; isso não fornece um campo de posição gravável validado.

## REA/Ghidra e limites da análise

Os bytes de código mapeados diferem da seção de código em disco. Retivemos
somente janelas selecionadas, com hashes e releituras, sob acesso de leitura.
Os metadados de unwind conferiram com a cópia privada, mas suas entradas podem
ser fragmentos de prólogo/código frio: não equivalem a funções completas.

REA 6.7.0 / Ghidra 12.1.4 analisaram carriers sintéticos privados, feitos a
partir desses trechos retidos. Eles nunca foram executados ou carregados como
DLL. Faltam `.rdata`, imports, unwind e outros destinos; endereços exportados
pelo analista não estabelecem ABI ou cobertura de fluxo completa. Um aviso de
decodificação em outro candidato impede conclusões sobre aquele caminho.

| Evidência REA | Escopo |
|---|---|
| `ev_107de552346484a84b2bf1f41b1562d91ba18d01a20267c4548afaf89a523c1a` | Registro das ações em `0xc0f3a0`, carrier inicial. |
| `ev_09ae0c496e4c3bf2efb75ac61dfb76a6e7ac913d1596c2029e8424c5e3ca971d` | Preparação em `0xc10880`, carrier inicial. |
| `ev_2ac3e1ccba4bf8198cd2a6cfd1207dcfb666bf3979b6f176e04a71d9c5c00781` | Despacho de ações em `0xc10b70`, carrier ampliado. |
| `ev_8cb753c0dbb9324fb520037ccaaeecff4fd25241ab09b02a104b1d92a470f2c6` | Estado/ação em `0xc103b0`, carrier ampliado. |
| `ev_37935361d613b7f3fd88e3ad2d4d1f0db588c0d6b4396ca4bdda44cbc937549a` | Consumo do pedido e despacho indireto em `0xc0f7a0`, carrier ampliado. |

SHA-256 dos carriers: inicial
`659aa2855c0a9d759dab9900f18d7d46dfa0b84409f18266c1e8283c3ae09840`;
ampliado `d020e59b2a112502c3490765b6327d256ed3a7b6f1d07eb0ab94c875bf90ca26`.
São identidades de artefatos de análise, não builds suportadas do jogo.
Decompilações, bytes proprietários e recibos privados não são publicados.

Consultamos o diretório e trabalho existente do Co-op Anything. A fonte do
[SekiroTool](https://github.com/borgCode/SekiroTool/tree/189649781b00ba1f2fddb5d8bbfff7684ff5b647)
serviu para localizar candidatos, sem executar suas rotinas de injeção. O
[Debug Patch do Yui](https://github.com/yuiamoroll/Sekiro-Debug-Patch/tree/88d51093deba3c28efdc8063a86823f4363dd027)
habilita o menu, mas não documenta o contrato interno de criação. Não copiamos
essa implementação nem aplicamos seus patches.

## Próximo teste e aceitação

O próximo incremento precisa resolver o ciclo de criação/remoção e a atualização
do ator antes de uma tentativa local limitada de segundo personagem. Depois,
será necessário demonstrar modelo, armas, animações e controle em **duas
instalações e duas máquinas** para aceitar `actors-spawn`. A coleta atual é
uma ferramenta e pesquisa de suporte; nenhuma aceitação multiplayer foi feita.
