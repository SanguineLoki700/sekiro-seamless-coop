# Observação real das operações de save — 2026-10-21

Etapa de pesquisa concluída no jogo original, versão Steam 1.6.0.0,
build 5794815. Horários da sessão: 23:43:34–23:44:29,
America/Sao_Paulo. O código público de runtime não foi alterado.

## Resultado

Após a confirmação de coleta ativa, o responsável descansou uma vez no Ídolo
e saiu do menu. Uma janela de 55 segundos registrou 162 operações relacionadas
ao save, todas com pilha de chamadas dentro do executável selecionado:

| Operação | Quantidade |
|---|---:|
| Abertura de arquivo | 60 |
| Fechamento de handle | 60 |
| Leitura | 24 |
| Gravação | 6 |
| Exclusão do backup | 6 |
| Cópia para backup | 6 |

As chamadas observadas retornaram sucesso. Nas leituras, apareceram blocos
de 64 e 768 bytes; nas gravações, três de 1.048.592 e três de 393.232 bytes.
Essas medidas não descrevem o formato completo nem os offsets usados.
Em seis grupos, o jogo excluiu o backup existente antes de copiar o primário
para ele. Os grupos são operações observadas na janela, não seis descansos.

O observador terminou pelo limite de duração: zero erros de callback ou leitura
de caminho, nenhuma mensagem truncada e remoção dos próprios listeners.
O encerramento aguardado do REA confirmou liberação da sessão e nenhum erro
de limpeza. O processo do jogo continuou aberto. Isso não é um teste de
recuperação após crash nem de durabilidade de disco.

## Método e verificação

REA 6.7.0 com Frida 17.23.2, usando o provider em um processo novo.
O componente nativo ausente do Frida foi obtido na origem oficial e conferido
contra o digest publicado do asset antes da extração. A conexão MCP existente
ainda conservava uma falha anterior em cache; as sessões bem-sucedidas usaram
a CLI e a API do mesmo REA, sem alterar a configuração MCP.

Antes do jogo, um executável próprio criou, gravou, leu, copiou, moveu e excluiu
arquivos sintéticos. A rota final de prontidão/encerramento passou com 12 eventos,
12 pilhas, nenhum erro, resultado PASS e saída 0. O host só gravou seu aviso de
prontidão após receber `trace_ready` com o PID escolhido.

A identidade do processo, seu tempo de vida, caminho, tamanho da imagem e
SHA-256 do executável foram conferidos separadamente. Os listeners temporários
do Frida instrumentaram memória do processo, mas não alteraram argumentos ou
retornos, chamaram funções do jogo por endereço ou leram buffers de conteúdo
dos saves. Nenhum mod foi instalado. A gravação normal decorreu da sessão
controlada pelo responsável; não houve restauração automática do save.

A tentativa ETW anterior falhou na restrição ao processo e perdeu eventos.
Foi descartada como evidência de save e substituída por este método após testes
na fixture. Não repetimos a captura ETW ampliando seu alcance.

Evidência principal e limites técnicos estão em
[RESEARCH.md](../RESEARCH.md#executed-save-io-observation-2026-10-21).
Logs brutos, saves, caminhos pessoais e código proprietário permanecem privados.

## Verificações do repositório

- Build MSVC x64 Release passou.
- Sete suítes CTest passaram.
- 26 testes Python passaram.
- `tools/progress.py --check` confirmou 0% verificado e documentos consistentes.

## Próximo requisito

Comprovar a inicialização antes do primeiro acesso aos saves e conectar o
redirecionamento nativo a `.co2`, cobrindo consultas, abertura e backup em
conjunto. A API de armazenamento de blobs completos não deve receber cada
bloco de `WriteFile` como se fosse um arquivo completo.

Depois, validar criação, gravação, recarga, backup e recuperação com o mod em
instalação de teste e perfil isolados, preservando o save solo. Steam Cloud e
desinstalação também continuam pendentes. O critério de isolamento permanece
aberto, o lançamento de gameplay continua desabilitado e o progresso funcional
permanece **0%**.
