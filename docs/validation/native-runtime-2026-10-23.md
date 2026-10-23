# Validação do runtime nativo — 2026-10-23

Critério: `core-native-runtime`, 3 pontos. Escopo: a DLL carrega no executável
identificado e a integração recusa identidades sem suporte antes dos hooks.
Um participante e uma máquina Windows 11; a versão aceita é somente Steam
1.06 / produto 1.6.0.0 / build 5794815, AppID 814380.
O cenário é local, sem transporte multiplayer; latência e perda de rede não se
aplicam a esta aceitação.

## Carregamento real já executado

Reutiliza-se o [teste do lançador de 23/10](core-launcher-2026-10-23.md), com
ações e confirmação humana de SanguineLoki700. Não foi aberta outra sessão
manual para repetir o mesmo cenário.

O executável AMD64 de 68.005.144 bytes foi medido pelo pai e mantido aberto
sem permitir escrita ou substituição. Seu SHA-256 é
`637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
A DLL carregada nessa sessão possui SHA-256
`7a5bacfb4513df04583015b4f621cb217b4b3cf5d476bb2dabf2556932bd5bf2`.

Na instalação independente, a enumeração de módulos confirmou essa DLL dentro
do processo correto. O bootstrap recebeu `READY` em fase 3, erro fatal 0 e
transação de hooks de saves instalada. A janela continuou respondendo, o
participante carregou o personagem e fechou normalmente. Saída 0, sem timeout
ou instância estrangeira; saves originais e configuração gráfica preservados.
O retorno ao jogo original sem a DLL também foi confirmado.

O código dessa execução é
`d48d46911f78c497a7bb4b050bdabed626a892f9`. A revisão deste critério não altera
o launcher, runtime, hooks, headers ou dependências nativas. Foram conferidos
novamente os hashes dos dois executáveis e da DLL preservada, além da ausência
de diferenças nas fontes nativas. Uma recompilação pode gerar outro hash de
artefato: ela não é apresentada como uma nova execução humana no jogo.

## Recusa antes dos hooks

O host exige a build conhecida e o hash completo do arquivo fixado antes de
criar o filho suspenso. A DLL exige payload/IPC válidos, caminho do módulo
atual, PE AMD64 com imagem esperada e digest conhecido atestado pelo host.
A comparação de identidade precede a abertura do armazenamento e a instalação
dos hooks. Não há fallback para a instalação solo.

Os testes anteriores rejeitavam uma fixture com PE e digest incompatíveis ao
mesmo tempo. Isso cobria a recusa de identidade, mas não isolava a comparação
do digest. Foram acrescentados três testes independentes:

| Caso | Campos mantidos compatíveis | Campo incompatível |
|---|---|---|
| Hash desconhecido | Caminho real da fixture e tamanho da imagem PE esperado | Digest conhecido com um byte alterado |
| PE desconhecido | Caminho real da fixture e digest declarado esperado | Tamanho da imagem do executável próprio |
| Caminho divergente | Tamanho PE e digest declarado esperados | Caminho do módulo fornecido no payload |

Cada caso exige `ERROR_INVALID_IMAGE_HASH`, saída exata `0xc0000142`, ausência
do marcador TLS do executável, ausência de `READY` e máscara de hooks zero.
Uma falha genérica ou crash não satisfaz essas asserções.

Somente uma cópia temporária do **próprio programa de testes** recebe o ajuste
limitado no cabeçalho PE para alcançar a comparação do digest. Um controle
separado demonstra que essa cópia ainda inicia e alcança TLS. Nenhum teste
combina todos os campos de identidade compatíveis, altera Sekiro ou adiciona
um hash de fixture à DLL. A produção permanece com a mesma versão suportada.

## Resultado e limites

Os 17 casos da guarda do runtime passaram contra a DLL preservada da sessão
real. Os 13 conjuntos CTest também passaram na compilação MSVC x64 Release,
assim como os 26 testes Python e a validação dos arquivos de progresso.
Os novos testes estão versionados no commit `533a5863e9afcc333215129dc1ee1ff420ca96dd`; registros
locais ficam em `local/core-native-runtime/`, sem publicar dados pessoais.

O critério está **aceito**: carregamento real demonstrado, com recusas de
compatibilidade exercitadas separadamente antes dos hooks. Somam-se apenas
seus 3 pontos: **10% verificado / 90% restante**. Os pontos anteriores de
lançador e saves não são contados novamente.

O digest dentro da DLL é uma atestação do host que mediu e fixou o arquivo;
isso não é autenticação criptográfica de qualquer injetor externo. As fixtures
comprovam as recusas, não compatibilidade com outra versão real do jogo.
Steam Cloud, outros mods/contas, callbacks de gameplay, atores remotos e
multiplayer continuam fora desta aceitação. A distribuição com abertura por
dois cliques permanece uma entrega de produto pendente.
