# Teste local de ataques e defesa

O botão **Testar ataques e defesa neste PC** abre a instalação protegida com
o perfil de teste existente e um parceiro simulado integrado no assistente.
Não abre outro jogo, não requer notebook, códigos ou opções da Steam.
Esta versão é um candidato para conferir comandos e animações no personagem
adicional; acertos, dano e postura compartilhados ainda não foram implementados.

## Evidência usada

Em 2026-10-26, o participante executou golpes separados, uma sequência de golpes,
defesa segurada e toques de defesa no jogador original. Uma observação via REA
registrou 22 eventos desse jogador na build Steam 5794815 / 1.06, com executável
SHA-256 `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
O processo foi correlacionado ao recibo do host protegido e à identidade do
módulo. A leitura filtrou dono do grafo e mapa de eventos; não chamou funções
do jogo nem escreveu dados de gameplay. A instrumentação temporária foi removida.

Foram observados seis eventos distintos de combo/início/liberação e quatro
eventos de entrada/saída de defesa. Os nomes e IDs foram obtidos do alvo; a DLL
resolve e confere novamente cada ligação no próprio grafo do destino antes
de enfileirar comandos. Os registros brutos, endereços e saves ficam privados.
O jogo saiu com código zero e os saves pessoais foram rechecados e preservados.
Essa coleta comprova a origem dos comandos, não sua aparência no ator adicional.

## Comportamento implementado

O teste acrescenta comandos discretos de combo e defesa ao histórico existente.
A informação de defesa segurada permanece independente de eventos de movimento e ataque,
mesmo quando a entrada de defesa sai dos quatro registros retidos. O receptor
recusa alterações contraditórias dessa informação e mantém confirmação, escopo,
validade e consumo único separados da interpolação de posição. Essa informação
no protocolo não comprova que o motor mantém a postura de defesa no destino.

Não existe comando neutro de restauração de defesa verificado nesta etapa.
Depois de uma pausa ou perda de dados, o código não repete uma entrada antiga
que poderia abrir outra janela de deflexão: o participante deve soltar e apertar
a defesa novamente. O assistente apresenta essa instrução quando necessário.
Os dois primeiros combos têm comandos de liberação observados; não se afirma
suporte completo a ataques carregados, cancelamentos ou janelas de deflexão.

O perfil `combat-preview` exige anfitrião e endereço `127.0.0.1`, além de um
cenário declarado próprio. O socket nativo recebe apenas pela interface local.
Os modos normais Criar/Entrar continuam com movimento e recusam comandos de
combate. O preview não é oferecido como combate entre computadores: o motor
local ainda pode produzir suas consequências normais, sem autoridade de dano
compartilhado ou desativação de hitboxes verificada.

O auxiliar integrado conserva o par exato de pose/histórico recebido. Usa um
ponto inicial observado, espera afastamento entre 1,25 e 6 metros e espelha um
trajeto de até 6 metros por no máximo 90 segundos. Sair da área suspende as
ofertas de pose; voltar para perto da posição do jogador quando o parceiro
apareceu permite retomar na mesma abertura. A pausa respeita um intervalo
mínimo para expirar os comandos anteriores. Nenhum golpe acumulado durante
a pausa é reproduzido na volta, e os limites de tempo continuam correndo.
Dados vencidos também suspendem
ofertas; voltar ao jogo retoma a mesma sessão sem reiniciar esse limite.
Os motivos aparecem no assistente e em um registro auxiliar vinculado ao
recibo exato da abertura, sem dados da sala. As correções de pose incluem
altura; o teste não comprova física independente nem navegação.

## Como testar

1. Com Steam aberta, clique em **Testar ataques e defesa neste PC** e use
   **Continuar** no perfil existente.
2. Escolha um trecho plano e vazio, sem inimigos próximos. Pressione F8,
   espere dois segundos e ande poucos passos para separar os personagens.
3. Quando o segundo aparecer, espere três segundos. Mantendo-o visível, faça
   golpes separados, uma sequência curta, segure a defesa e depois solte.
4. Feche o jogo pelo menu ao terminar. O perfil de teste continua preservado.

O bootstrap passa a 12, o protocolo LAN a 3 e o adaptador a 4; os frames continuam
com 244 bytes, incluindo HMAC. Um par entre versões incompatíveis é recusado.
O perfil de save existente não muda de formato nem é substituído.

No candidato `766a598`, o participante confirmou os ataques no personagem
adicional, mas relatou apenas um toque de defesa, sem sustentação. O recibo
registrou oito comandos de ataque aplicados e duas entradas e duas saídas de
defesa, sem erro de ações. O auxiliar encerrou o espelhamento após cerca de
14 segundos; a mensagem do assistente confirmou o limite de trajeto. Portanto,
essa parada não demonstra falha no terceiro golpe, e a defesa sustentada ainda
precisa ser investigada e verificada.

Uma tentativa nativa limitada manteve o Booleano `IsGuard` ativo por cinco
segundos no ator adicional e enviou uma única entrada de defesa. O participante
ainda observou apenas um toque. O valor anterior foi restaurado e a saída de
defesa enviada; essa tentativa não foi incorporada ao código do mod. O processo
saiu normalmente, com código zero, e os saves pessoais e o backup foram
rechecados de forma independente e permaneceram intactos.

A retomada após sair da área passou nos testes de lógica e de sockets locais,
incluindo descarte de mais ações do que cabem no histórico retido. Sua aparência
no jogo ainda precisa de validação com a versão atualizada.

Não há aceitação de combate, dois jogos reais ou novos pontos; o progresso
segue 10%. A contagem de comandos aplicados não substitui a observação da
animação, a duração da defesa ou a futura validação de acertos e postura.
