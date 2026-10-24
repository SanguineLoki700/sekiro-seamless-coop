# Experimentos nativos

Esta pasta registra código original executado em testes controlados, separado
do runtime distribuído. Não contém binários, opcodes ou recursos do jogo.

`local_player_spawn.template.js` é o corpo exato do primeiro teste de jogador
adicional visível. Não possui configuração executável por padrão e não é
instalado pelo CMake. Seu host privado validou a identidade, o processo, a cópia
independente, os saves protegidos e o perfil descartável antes de preencher
`__CONFIG__`. O template não substitui essas garantias externas.

Veja o [resultado e seus limites](../../docs/validation/local-player-2026-10-24.md).
