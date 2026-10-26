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
A defesa segurada permanece independente de eventos de movimento e ataque,
mesmo quando a entrada de defesa sai dos quatro registros retidos. O receptor
recusa alterações contraditórias dessa informação e mantém confirmação, escopo,
validade e consumo único separados da interpolação de posição.

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
trajeto de até 6 metros por no máximo 90 segundos. Dados vencidos suspendem
ofertas; voltar ao jogo retoma a mesma sessão sem reiniciar esse limite.
Os motivos de parada aparecem no assistente. As correções de pose incluem
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

Compilação e testes independentes passaram; a aparência de ataques e defesa
nesta integração C++ ainda precisa da confirmação do participante. Não há
aceitação de combate, dois jogos reais ou novos pontos; o progresso segue 10%.
