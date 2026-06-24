# WYD Web Port - Notas de Portabilidade

## Objetivo

Port incremental do client WYD v769 para navegador, maximizando reaproveitamento de:

- assets reais (`v769ClientRelease/`)
- regras de parsing e runtime existentes no source (`Projects/TMProject/`)

Sem big bang rewrite.

Detalhamento da virada WASM: `docs/wasm-migration.md`.

## Estratégia macro

Arquitetura em camadas para reduzir risco:

1. filesystem/asset loading
2. abstração de plataforma
3. backend de render
4. estratégia de shader
5. cenas/UI
6. mapa/objetos
7. personagens/skinning
8. rede (depois)

## Direção técnica atual (WASM-first)

Mudança de rumo aplicada:

- **não** expandir reimplementação de lógica de jogo em JS além do necessário para bootstrap;
- reutilizar o código C++ do client como fonte primária de comportamento;
- portar incrementalmente para **C++ -> WASM**, mantendo JS focado em:
  - host/browser platform glue;
  - integração de assets/IO;
  - camada de apresentação e debug;
  - backend de render WebGL/WebGPU.

Estratégia prática:

1. mover loaders/parsers críticos (`msh/msa/ani/bon/trn/dat`) para core C++ compilado em WASM;
2. encapsular chamadas por bridge JS fina (`wasmBridge`) com fallback controlado;
3. adaptar a camada DX9 para um backend browser (estado/matriz/material -> WebGL2), sem reescrever regras de gameplay;
4. usar os componentes JS atuais como **andaime transitório**, removendo duplicação conforme cada subsistema migra para WASM.

## Plano por marcos

### M1 (entregue nesta etapa)

- inventário técnico concluído
- manifest de assets gerado
- resolução case-insensitive funcional
- app web inicial executável com WebGL2

### M2

- render loop com fila mínima de draw commands
- primeira textura proprietária (`.wys/.wyt`) decodificada e enviada ao WebGL
- visualização de sprite/quad com asset real de jogo

Status atual:

- decoder de `WYT` (header `WT10` + payload TGA type 2) implementado no front-end;
- textura real `UI/Inventory2.wyt` já é decodificada para RGBA e enviada ao WebGL2.
- `WYS` com decode inicial implementado para DDS (DXT1/DXT3) e tentativa de upload comprimido no WebGL2.

### M3

- loader inicial de cena/UI binária (`UI/*.bin`) ou viewer simples de mapa/mesh
- render de entidades estáticas com dados reais

Status atual:

- viewer inicial de mesh real (`.msh`) implementado no browser com câmera orbital;
- parser usa header/stride/influência do arquivo original e já renderiza geometria de `Mesh\\CP010102.msh`.
- catálogo real de meshes exposto por `GET /api/assets/meshes` (1665 entradas no pacote atual) e integrado ao front-end;
- seletor de mesh + query string (`?mesh=...`) funcionando para escolher arquivo real sem hardcode;
- probe de `ANI` integrado (contagem de frames/matrizes por arquivo) no painel lateral.
- meshes com indício de skinning (`faceInfluence > 1` ou `paletteCount > 1`) são sinalizados no painel como "skinning pendente" (escopo de M4).
- monitor de viewport adicionado no painel (`css/buffer/DPR`) para validar runtime WebGL e detectar regressões de resize/layout rapidamente.
- eixo mapa/objetos iniciado com dados reais:
  - `GET /api/assets/fields` para catálogo de `FieldXXXX/Character`;
  - `GET /api/assets/field-summary?field=...` com parse de `Env/*.trn` (grid 64x64) e `Env/*.dat` (instâncias de objeto);
  - preview top-down no front-end com altura + overlay de objetos.
- cenário 3D incremental no WebGL:
  - terreno triangulado em runtime a partir do `heightGrid` do field ativo;
  - pontos de objetos `DAT` sobre o terreno para inspeção visual rápida;
  - controles de visibilidade/escala para calibração de cena sem quebrar o viewer de mesh/skinning.
  - textura real de minimap por field (`UI/mXXXX.wyt|wys`) aplicada no terreno com controle de blend;
  - picking de objetos `DAT` no canvas 3D (clique) com highlight e painel de inspeção.

### M4

- entrada de skinning/personagem
- tratamento inicial dos shaders/effects legados (substituição ou tradução parcial)

Status atual:

- parser web de mesh atualizado com dados de skinning: pesos, blend indices, bone matrices (paleta) e bone names;
- parser de `ANI` corrigido para semântica real do client (`tickCount`, `bonesPerFrame`, payload de matrizes);
- parser de `BON` implementado para hierarquia `parentId -> boneId`;
- endpoint `GET /api/assets/mesh-related?mesh=...` adiciona descoberta de bundles `ANI/BON` relacionados por prefixo;
- catálogo real `BoneAni4.txt` + `ValidIndex.bin` integrado no servidor:
  - `GET /api/assets/bone-ani-catalog`
  - `GET /api/assets/mesh-animation-profile?mesh=...`;
- seleção automática de animação por perfil (`weapon/motion`) no front-end, usando tabela real do client em vez de apenas heurística de prefixo;
- viewer web ganhou controles de bundle (`ANI/BON`), playback, frame manual e velocidade;
- skinning CPU incremental ativo no browser para meshes com paleta/influência válidas, com update dinâmico de VBO no WebGL.
- cache de parse de `ANI/BON` no front para reduzir custo de troca de animação e evitar reparse desnecessário;
- cache `ANI/BON` evoluído para LRU no browser para controlar memória em sessões longas;
- carregamento de mesh com token anti-race para evitar aplicar respostas atrasadas quando o usuário troca de mesh rapidamente;
- correção de estabilidade de layout/scroll: viewport de página travada e painéis com overflow controlado para eliminar crescimento progressivo da página.
- startup parametrizável por query string para smoke tests e reprodução:
  - `field`, `entry`, `weapon`, `motion`, `preload`, `seq`, `seqScope`, `seqInterval`;
  - `terrain`, `terrainTexture`, `terrainTextureBlend`, `objPoints`, `terrainScale`, `objectPointSize`.
- trilha de objetos estáticos reais de mapa iniciada com `.msa`:
  - endpoint `GET /api/assets/object-mesh-map` (a partir de `Mesh/MeshList.txt`) para mapear `objType -> mesh/effect`;
  - parser web de `MSA` (`fvf 274/322/18`) com ranges de atributo, refs de textura, índice e vértices;
  - renderer WebGL2 com `drawElementsInstanced` para grupos de objeto por `objType` (instancing por field);
  - UI com controles de budget (`objMeshBudget`) e tipos por field (`objMeshTypes`) para manter performance previsível.
  - evolução de fidelidade: materiais/submeshes por atributo (`D3DXATTRIBUTERANGE`) com textura por `textureRef` real do `MSA` + fallback compatível ao fluxo legado.
- runtime de personagem em campo avançou para uso contínuo (não apenas probe):
  - navegação por teclado (`WASD + Shift`) com snap de altura no `TRN`;
  - click-to-move pelo minimapa e teleport por `Shift+clique`;
  - spawn manual no objeto selecionado (`DAT`) para acelerar inspeção em campo real.
- colisão inicial de personagem com objetos do field:
  - geração de proxies a partir do `DAT` ativo;
  - raio base por objeto com hint de bounds real de `MSA` por `objType` (quando disponível);
  - resolução incremental por push-out para evitar atravessar blocos estáticos.
- câmera de gameplay adicionada:
  - modo `follow` (3rd person) e `orbit` selecionáveis em runtime;
  - ajustes de distância/altura da câmera;
  - alvo de câmera sincronizado com o personagem para reduzir deriva visual.
- minimapa ganhou overlays de gameplay:
  - marcador da posição atual do personagem;
  - marcador de destino ativo para click-to-move.
- validação E2E com Playwright local:
  - interação real de minimapa + toggles de player/câmera/colisão;
  - comparação de métricas de layout antes/depois (`scrollHeight`/`clientHeight`) sem crescimento vertical progressivo.
- início formal da trilha C++ -> WASM:
  - toolchain Emscripten instalado e validado no ambiente de desenvolvimento;
  - módulo C++ `webclient/wasm/src/wyd_core.cpp` compilado para `app/wasm/wyd_core.{js,wasm}`;
  - bridge `app/src/wasmBridge.js` integrada no front-end;
  - parser nativo WASM já ativo em runtime para:
    - header real de `.msh` (`parentId`, `meshId`, `fvf`, `stride`, `counts`);
    - overview real de `.trn` (`tileCount`, min/max/avg de altura, offsets).

### M5

- fluxo de client próximo ao original (UI + mundo + personagem)
- integração de rede volta ao escopo

## Mapa de risco

Risco alto:

- tradução de estados fixed-function complexos
- skinning legado + bytecode `vs_1_1/ps_1_1`
- formatos binários proprietários sem documentação formal

Risco médio:

- sequência de inicialização de cenas/UI
- equivalência visual de blend/fog/alpha legacy

Risco baixo:

- entrega de assets, manifest, índices, utilitários de inspeção

## Diretriz de implementação contínua

- priorizar incrementos que já usem dados reais do `v769ClientRelease`
- manter utilitários de análise/versionamento em `webclient/tools/`
- documentar cada descoberta que impacta parser/render em `docs/`

## Reavaliação estrutural do render (2026-04-12)

Resultado da revisão global do frame inicial em WASM:

- o erro vermelho atual do browser não era do runtime do client, e sim `404` de `favicon.ico`;
- desabilitar `alpha test` globalmente não alterou a cena inicial;
- forçar alpha opaco de textura alterou imediatamente a composição visual, confirmando que o problema central ainda passa pela semântica de alpha/stages do fixed-function;
- o bridge tinha um erro estrutural: `ALPHAOP` da stage corrente estava lendo `CURRENT` já modificado pelo `COLOROP` da mesma stage.

Correção aplicada:

- o shader FFP passou a calcular `COLOROP` e `ALPHAOP` de cada stage a partir do mesmo snapshot de `CURRENT` vindo da stage anterior, como no D3D9;
- o `CURRENT` para a stage seguinte agora é composto por `rgb` da operação de cor e `a` da operação de alpha da stage concluída.

Impacto esperado:

- estabilizar pipelines multi-texture legados do client que dependem de `CURRENT.a` correto em stage1;
- reduzir desaparecimento/flicker de meshes em paths com `TEX1/TEX2`, principalmente no fluxo real da cena inicial.

## Atualização de paridade DX9 no startup (2026-06-01)

- bridge WASM recebeu side-effects corretos de `Draw*UP`:
  - `DrawPrimitiveUP` limpa stream 0 após draw;
  - `DrawIndexedPrimitiveUP` limpa stream 0 e índice após draw.
- heurística agressiva de replay screen-space para assinatura `FVF 322` foi desativada no path padrão (permanece apenas a telemetria da assinatura).
- clip reject por `clip.w` agora entra automaticamente para famílias `FVF 322/594` no startup (com opt-out por debug), reduzindo drasticamente os artefatos de polígonos gigantes/rabiscos.
- `startup_harness` passou a ter acesso direto por URL padrão no smoke e redirect no `index.html` raiz do workspace servido em `:8877`.

## Atualização de diagnóstico startup (2026-06-02)

Rodada focada em fidelidade da tela inicial e sanidade do mapa de debug flags.

Correção aplicada no compat layer:

- `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp` tinha colisão de bits:
  - `kDebugDisableFvf322ScreenlikeReplay = 1u << 15`
  - `kDebugClipTriangleRepair = 1u << 15`
- o bridge WASM agora separa as duas intenções:
  - `kDebugDisableFvf322ScreenlikeReplay = 1u << 15`
  - `kDebugClipTriangleRepair = 1u << 16`

Por que isso importava:

- o relatório mais recente `startup-smoke-20260602-043747-noscreenfix.json`
  foi executado com `debugFlags=32768`;
- na versão anterior, esse único bit simultaneamente:
  - desligava o replay screen-space de `FVF 322`;
  - ligava o fallback de clip-triangle repair.
- resultado: o run misturava duas mudanças incompatíveis e produzia uma tela com
  grandes planos diagonais preto/vermelhos, mas sem isolar a causa real.

Validação após relink:

- servidor local mantido acessível em `http://127.0.0.1:8877/`;
- startup relinkado com sucesso (`startup-link.md`: `link ok: true`, `undefined_total=0`);
- smoke baseline novo:
  - JSON: `webclient/client-wasm/build/reports/startup-smoke-20260602-flagfix-baseline.json`
  - screenshot: `webclient/client-wasm/build/reports/startup-canvas-20260602-flagfix-baseline.png`
- smoke clip-repair isolado:
  - JSON: `webclient/client-wasm/build/reports/startup-smoke-20260602-flagfix-cliprepair.json`
  - screenshot: `webclient/client-wasm/build/reports/startup-canvas-20260602-flagfix-cliprepair.png`

Métricas principais:

- baseline (`debugFlags=0`)
  - `drawCalls=61485`
  - `primitiveCount=5841500`
  - `fvf322ScreenlikeReplayDraws=4325`
  - `clipWRejectDraws=9960`
  - `glErrorTotal=0`
- clip repair isolado (`debugFlags=65536`)
  - `drawCalls=65176`
  - `primitiveCount=4183391`
  - `fvf322ScreenlikeReplayDraws=9815`
  - `clipWRejectDraws=32074`
  - `glErrorTotal=0`

Classificação visual:

- frente ao `noscreenfix` de 2026-06-02, a baseline corrigida é **progresso**:
  remove os planos diagonais dominantes e volta a mostrar uma composição mais
  legível de logo, painéis e terreno.
- o clip repair isolado continua **regressão**:
  reintroduz polígonos diagonais extensos e um preenchimento marrom/escuro no
  centro inferior da cena.

## Atualização de diagnóstico startup (2026-06-03)

Rodada focada em entender se o guard de depth da família `FVF 594` ainda estava
amplo demais no path padrão da tela inicial.

Diagnóstico pré-mudança:

- servidor local mantido acessível em `http://127.0.0.1:8877/` por static
  server na raiz do workspace;
- baseline curta capturada:
  - `webclient/client-wasm/build/reports/diag-20260603-baseline-t120.json`
  - `webclient/client-wasm/build/reports/diag-20260603-baseline-t120.png`
- variantes comparativas:
  - `disable stage1` (`debugFlags=512`):
    - `diag-20260603-nostage1-t120.{json,png}`
  - `guard 594 only` (`debugFlags=8192`):
    - `diag-20260603-guard594only-t120.{json,png}`
  - `guard 322 only` (`debugFlags=4096`):
    - `diag-20260603-guard322only-t120.{json,png}`

Leitura desse diagnóstico:

- desligar stage1 quase não alterou o frame, então a hipótese principal deixou
  de ser `stage1/TEXCOORD1`;
- liberar depth write de `FVF 322` ou de `FVF 594` isoladamente piorou o frame,
  confirmando que as duas famílias ainda dependem do guard no estado atual do
  bridge.

Experimento de código testado:

- em `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp`, o guard de
  depth de `FVF 594` foi estreitado temporariamente para draws classificados
  como “order-sensitive”:
  - blend habilitado;
  - alpha test habilitado;
  - `D3DCULL_NONE`.

Validação após relink:

- link ok: `webclient/client-wasm/build/reports/startup-link.md`
- smoke experimental:
  - JSON: `webclient/client-wasm/build/reports/startup-smoke-20260603-fvf594-depthnarrow-t120.json`
  - screenshot: `webclient/client-wasm/build/reports/startup-canvas-20260603-fvf594-depthnarrow-t120.png`
- smoke pós-revert para restaurar o baseline servido:
  - JSON: `webclient/client-wasm/build/reports/startup-smoke-20260603-postrevert-baseline-t120.json`
  - screenshot: `webclient/client-wasm/build/reports/startup-canvas-20260603-postrevert-baseline-t120.png`

Métricas principais:

- baseline restaurada (`postrevert`)
  - `drawCalls=61205`
  - `primitiveCount=5833480`
  - `depthWriteDisabledDraws=60845`
  - `depthWriteGuardForcedDraws=54657`
  - `glErrorTotal=0`
- experimento `FVF 594` depth narrow
  - `drawCalls=66513`
  - `primitiveCount=5837623`
  - `depthWriteDisabledDraws=25554`
  - `depthWriteGuardForcedDraws=14878`
  - `glErrorTotal=0`

Classificação visual:

- **regressão**: o relaxamento amplo de depth em `FVF 594` reintroduziu
  fragmentos grandes vermelho/preto de mesh no lado direito da tela e piorou a
  legibilidade do cenário;
- a mudança não foi mantida no path padrão.

Decisão:

- o experimento foi revertido antes de encerrar a rodada;
- o artefato servido em `:8877` voltou ao baseline legível atual.

Próxima hipótese de trabalho:

- antes de qualquer nova flexibilização do guard de `FVF 594`, precisamos
  exportar uma assinatura por draw/subfamília (`blend`, `alpha`, `cull`,
  textura/material e talvez hash de ranges) para separar:
  - os draws `594` que podem escrever depth sem destruir a composição;
  - os draws `594` que voltam como fragmentos vermelho/preto quando o guard é
    relaxado.

Conclusão prática:

- a correção do mapa de flags era necessária para que os diagnósticos de startup
  voltassem a ser confiáveis;
- `kDebugClipTriangleRepair` deve continuar fora do path padrão;
- o próximo ganho de fidelidade precisa vir do path baseline, provavelmente
  refinando a família `FVF 322` em conjunto com estado de blend/cull/depth, e
  não de reconstrução ampla de triângulos clipados.

## Atualização de diagnóstico startup (2026-06-04)

Rodada focada em recuperar mais quads de startup que já são emitidos em
viewport space, mas escapavam do replay screen-space de `FVF 322` por exigir
match estrito de todos os vértices.

Correção aplicada no compat layer:

- em `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp`, o replay
  de `FVF 322` agora aceita draws near-screen quando:
  - pelo menos `75%` dos vértices passam na heurística screen-space estrita;
  - `100%` dos vértices continuam dentro de uma janela expandida de viewport;
  - `z` bruto permanece na faixa compatível de startup (`-0.25 .. 1.25`).

Por que isso importava:

- no baseline de `2026-06-03`, havia `14938` draws `FVF 322` com pelo menos
  algum vértice screen-like, mas só `4090` eram de fato replayados;
- isso deixava uma parcela relevante de batches de UI/tela inicial cair no path
  3D normal, o que achatava o fundo para cinza/preto e mantinha fragmentos
  residuais no cenário.

Validação após relink:

- servidor local mantido acessível em `http://127.0.0.1:8877/`;
- startup relinkado com sucesso (`startup-link.md`: `link ok: true`,
  `undefined_total=0`);
- smoke baseline novo:
  - JSON: `webclient/client-wasm/build/reports/startup-smoke-20260604-fvf322-nearscreen-t120.json`
  - screenshot: `webclient/client-wasm/build/reports/startup-canvas-20260604-fvf322-nearscreen-t120.png`

Métricas principais vs baseline anterior `2026-06-03`:

- `drawCalls`: `61205 -> 63456`
- `primitiveCount`: `5833480 -> 5866411`
- `fvf322ScreenlikeDraws`: `14938 -> 15733`
- `fvf322ScreenlikeReplayDraws`: `4090 -> 4769`
- `blendEnabledDraws`: `6809 -> 8902`
- `depthWriteGuardForcedDraws`: `54657 -> 54815`
- `glErrorTotal`: `0 -> 0`

Classificação visual:

- **progresso parcial**:
  - o fundo volta a exibir céu azul em vez do plano cinza/preto do baseline
    anterior;
  - logo, terreno e painéis continuam presentes e legíveis;
- bloqueio restante:
  - fragmentos vermelho/preto de mesh ainda aparecem no lado esquerdo da tela,
    então a heurística ampliada melhora a composição mas ainda não fecha a
    paridade da startup.

Próxima hipótese de trabalho:

- refinar o replay `FVF 322` por assinatura de draw para não depender apenas da
  geometria crua:
  - cruzar near-screen com `blend`, `cull`, `alpha test` e depth state;
  - separar os quads de UI que devem continuar em screen-space dos draws 3D da
    mesma família que ainda precisam do path clip-space normal.

## Marco técnico WASM (2026-04-11)

Virada C++-first consolidada nesta etapa:

- scan sintático do client C++ completo (`TMProject.vcxproj`) estabilizado em `109/109` TUs OK;
- compilação real para objetos WASM estabilizada em `109/109` TUs OK;
- geração de monolito WASM de prova (`--no-entry`) a partir de todos os objetos do client.

Artefatos e ferramentas:

- scan sintático:
  - `webclient/client-wasm/tools/run_tmproject_wasm_scan.sh`
  - `webclient/client-wasm/build/reports/tmproject-wasm-scan.md`
- objetos:
  - `webclient/client-wasm/tools/run_tmproject_wasm_objects.sh`
  - `webclient/client-wasm/build/reports/tmproject-wasm-objects.md`
  - `webclient/client-wasm/build/obj/`

## Atualização operacional (2026-06-01, 11:54)

Resumo da rodada:

- foco mantido em `C++ -> WASM` com `TMProject` real, sem expandir o protótipo Flask;
- startup relinkado com preload corrigido para `NUI/*.wyt`;
- compat layer recebeu fallback de path para meshes ausentes `or010[3-6]xx.msh` usando variantes reais existentes no pacote.

Validação objetiva (Playwright smoke, state 7, 120 ticks):

- relatório: `webclient/client-wasm/build/reports/startup-smoke-20260601-115427.json`
- screenshot: `webclient/client-wasm/build/reports/startup-canvas-20260601-115427.png`
- `assetFileOpenFail`: `0` (antes `303`);
- `assetFileOpenFailTexture`: `0` (antes `299`);
- `assetPathFallbackHits`: `4`;
- `consoleErrorCount`: `0`;
- `glErrorTotal`: `0`.

Conclusão técnica da etapa:

- bloqueios de carregamento de asset da tela inicial foram removidos;
- a diferença restante para paridade visual está concentrada no backend de render DX9/WebGL (artefato vermelho residual), não em I/O.
- monólito de prova:
  - `webclient/client-wasm/build/monolith/tmproject_monolith.js`
  - `webclient/client-wasm/build/monolith/tmproject_monolith.wasm`

Próxima fronteira:

1. transformar o monólito de prova em runtime carregável no host web (exports/imports controlados).
2. substituir progressivamente stubs Win32/DX por implementações browser-facing reais (platform/render/audio/input).
3. começar o bootstrap de loop real do client (`NewApp/RenderDevice/TMScene`) sobre a camada WASM.

## Marco técnico WASM - Startup Link (2026-04-11)

Entrega desta iteração (C++-first, sem expandir protótipo JS):

- entrypoint C exportado para bootstrap do client real:
  - `webclient/client-wasm/compat/src/wyd_client_entry.cpp` (`_wyd_start_client -> wWinMain`).
  - exports cooperativos de runtime adicionados:
    - `_wyd_boot_client`
    - `_wyd_tick_client`
    - `_wyd_shutdown_client`
- loop principal preparado para modo cooperativo:
  - `Projects/TMProject/NewApp.cpp` agora possui `RunTick(MSG*)` (extraído de `Run()`), habilitando direção por tick no host browser.
- camada de compatibilidade consolidada em arquivo único:
  - `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp`;
  - cobre Win32 base, MMIO, COM, WinInet, sockets, DirectInput, DirectSound e matemática D3DX.
- pipeline de link strict do startup automatizada:
  - `webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh`
  - `webclient/client-wasm/tools/link_tmproject_wasm_startup.py`
- resultado do link de startup:
  - `webclient/client-wasm/build/reports/startup-link.md`
  - `0` símbolos indefinidos (`unique=0`, `total=0`).
- artefatos gerados:
  - `webclient/client-wasm/build/link/tmproject_startup.js`
  - `webclient/client-wasm/build/link/tmproject_startup.wasm`

Validação de browser:

- harness de bootstrap criado em `webclient/client-wasm/build/link/startup_harness.html`;
- carregamento do WASM e `onRuntimeInitialized` confirmados em browser real via Playwright local.
- bootstrap cooperativo validado com assets reais mínimos:
  - preload manifesto `webclient/client-wasm/config/startup-preload-manifest.txt`;
  - pacote `tmproject_startup.data` gerado no link;
  - smoke `webclient/client-wasm/tools/playwright_startup_smoke.mjs` retornando:
    - `boot=1`
    - `tick_client` estável (10 ticks sequenciais com retorno `1`)
    - `shutdown=1`
  - relatório: `webclient/client-wasm/build/reports/startup-smoke.json`.

Bloqueio atual após esse marco:

- path de render ainda depende de shim DX9 não-funcional (sem saída visual equivalente ao desktop);
- `MeshManager` foi temporariamente adiado no path WASM para evitar crash de memória durante `tick`;
- a virada para client funcional agora é substituir esse gate temporário por inicialização real de mesh/texture/shader sobre backend DX9->WebGL2.

## Atualização - Render visível no browser (2026-04-11)

Entrega deste bloco:

- camada mínima DX9->WebGL2 conectada no device stub WASM:
  - `Clear`, `Present`, `SetViewport`, `SetRenderState`, `BeginScene`, `EndScene`, `Reset`, `TestCooperativeLevel`;
  - contexto WebGL criado no canvas `#canvas` com viewport sincronizado ao tamanho real.
- `startup_harness` atualizado para modo visual de runtime:
  - canvas dedicado ao render (`Module.canvas`);
  - layout com overflow controlado para eliminar “descida infinita” da página;
  - log com limite de linhas para evitar crescimento contínuo de DOM.
- validação automatizada com Playwright reforçada:
  - leitura de pixel do canvas após `boot + ticks`;
  - screenshot de evidência salvo em `webclient/client-wasm/build/reports/startup-canvas.png`;
  - relatório com `renderVisible=true` em `webclient/client-wasm/build/reports/startup-smoke.json`.

Resultado objetivo:

- render do client no browser já produz frame visível (clear color real via backend WebGL2), mantendo o ciclo cooperativo `boot -> tick -> shutdown` estável.

## Atualização - Bloco profundo WASM/DX9 (2026-04-11)

Evolução executada nesta rodada:

- expansão forte da camada de compatibilidade DX9 no WASM:
  - `IDirect3DDevice9`: `SetTransform`, `SetTexture`, `SetTextureStageState`, `SetSamplerState`,
    `SetFVF`, `SetStreamSource`, `SetIndices`, `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP`,
    `DrawIndexedPrimitive`, `CreateVertexBuffer`, `CreateIndexBuffer`,
    `GetRenderTarget`, `GetDepthStencilSurface`, `CreateRenderTarget`, `ColorFill`;
  - `IDirect3DVertexBuffer9`/`IDirect3DIndexBuffer9` com `GetDesc/Lock/Unlock` reais;
  - `IDirect3DTexture9`/`IDirect3DSurface9` com `GetLevelDesc/GetSurfaceLevel/LockRect/UnlockRect` reais.
- `D3DXCreateTexture` e `D3DXCreateTextureFromFileInMemoryEx` deixaram de ser `E_NOTIMPL` e passaram a criar texturas válidas no runtime WASM (placeholder funcional para continuidade do pipeline legado).
- resolução de path Win32 no runtime WASM reforçada:
  - normalização de `\\` para `/`;
  - busca case-insensitive por componente de diretório;
  - aplicada em `_open`, `CreateFileA`, `_stat64i32`, `mmioOpenA`, `InternetOpenUrlA` e rotinas de arquivo relacionadas.
- pipeline de preload evoluído:
  - `link_tmproject_wasm_startup.py` agora expande globs no manifesto (`*.ani`, `*.bon`);
  - manifesto de startup passou a carregar `object.bin` + bulk de `Mesh/*.bon` e `Mesh/*.ani`;
  - pacote `tmproject_startup.data` subiu para ~63 MB para suportar bootstrap real do `MeshManager`.
- `MeshManager` no path WASM:
  - inicialização real reativada (sem gate de defer), com sucesso em runtime.

Validação atual:

- `boot -> tick(120) -> shutdown` segue estável no browser;
- render continua visível no canvas (`renderVisible=true`);
- `MeshManager` sobe com sucesso no tick inicial (`[newapp:tick] wasm mesh manager initialized`).

Limite encontrado (já isolado):

- ao aplicar imediatamente `SetCurrentState(TM_SELECTSERVER_STATE)` após o init do `MeshManager`, ocorre `memory access out of bounds` em WASM;
- alteração foi revertida para manter estabilidade do loop principal;
- próximo passo é instrumentar e corrigir esse path de transição de estado (não a fundação de render/asset, que já está ativa).

## Atualização - Transição real para Select Server no WASM (2026-04-11)

Correções e avanço do bloco crítico:

- preload de startup ampliado para pacote de cena real (UI/Env/Effect/Mesh/Sound/music) com `tmproject_startup.data` ~`500 MB`;
- linker de preload recebeu melhoria de glob com preservação de subcaminhos para diretórios (`UI/Image`, `UI/Panel`, `UI/Potal`);
- `fopen_s` no compat layer passou a usar resolução case-insensitive + normalização `\\` -> `/` (causa raiz do `LoadRC` falhar mesmo com asset presente);
- transição automática WASM para `TM_SELECTSERVER_STATE` foi reativada após `InitMeshManager`;
- `ObjectManager::SetCurrentState` e `TMSelectServerScene::InitializeScene` receberam telemetria e guardrails para falhas de recurso no bootstrap.

Resultado validado em browser real (Playwright):

- `boot=1`, `ticks=160x(1)`, `shutdown=1`;
- `gameState=7` (`TM_SELECTSERVER_STATE`);
- draw pipeline ativo com carga real do client:
  - `drawCalls=131362`
  - `primitiveCount=8876353`
- `renderVisible=true` com screenshot em:
  - `webclient/client-wasm/build/reports/startup-canvas.png`.

## Atualização - Runtime C++ padrão no navegador (2026-04-11, noite)

Mudanças aplicadas para sair do fluxo manual/protótipo e manter o foco no client C++:

- servidor Flask auxiliar desligado do fluxo principal;
- execução padrão consolidada em servidor estático na pasta de link WASM:
  - `python -m http.server 8877` em `webclient/client-wasm/build/link`;
- `index.html` adicionado em `build/link` redirecionando para `startup_harness.html` (entrada direta em `http://127.0.0.1:8877/`).

Bootstrap web do runtime C++:

- `startup_harness.html` atualizado para:
  - auto-boot (`_wyd_boot_client`) ao inicializar runtime;
  - auto-transição para `TM_SELECTSERVER_STATE` (`_wyd_set_game_state(7)`);
  - auto-tick contínuo (`requestAnimationFrame`) sem depender de clique manual;
  - botão de controle para pausar/retomar auto-tick.

Avanço de render no bridge DX9->WebGL2:

- `D3DXCreateTextureFromFileInMemoryEx` passou a decodificar textura real em WASM:
  - TGA (`WT10` reconstruído) e DDS (`WS10` reconstruído, DXT1/DXT3);
- upload real de textura para GL e cache por textura;
- marcação de `dirty` em `LockRect/UnlockRect` e `ColorFill` para sincronizar updates;
- shader FFP incremental atualizado para usar:
  - posição + cor + UV;
  - textura stage0 com `COLOROP` básico (`SELECTARG`, `MODULATE`, `ADD`, `ADDSMOOTH`, `MODULATE2X/4X`).

Telemetria nova exportada no runtime:

- `_wyd_d3d9_tex_decode_success`
- `_wyd_d3d9_tex_decode_fail`
- `_wyd_d3d9_tex_uploads`
- `_wyd_d3d9_textured_draws`

Validação atual (runtime C++ real):

- `gameState=7` (select server);
- `texDecodeSuccess=229`, `texDecodeFail=0`;
- `texUploads=103`;
- `texturedDraws=67986`.

Observação de fidelidade:

- já existe geometria/textura visível no canvas no fluxo auto-run (`startup-canvas-live.png`),
  mas o frame ainda não está visualmente idêntico ao desktop.
- próximos blocos continuam no bridge DX9 (material/luz/fog e path de shader legado `vs_1_1/ps_1_1`).

## Atualização - FFP DX9 fidelity pass + instrumentação de vértice (2026-04-11, fim do dia)

Bloco implementado no runtime C++/WASM (`compat/src/win32_emscripten_stubs.cpp`):

- correção dos valores reais de `D3DTEXTUREOP` (DX9) em `compat/include/d3d9.h`;
- shader FFP WebGL expandido para **2 estágios de textura** (`stage0 + stage1`), com suporte a `D3DTA_TFACTOR`, `CURRENT`, `DIFFUSE`, complement/alpha-replicate;
- cobertura adicional de `COLOROP/ALPHAOP` (incluindo `MODULATE2X/4X`, `ADDSIGNED`, `SUBTRACT`, `BLEND*`, `DOTPRODUCT3`);
- alpha-test reativado no caminho FFP (sem o bypass global), com normalização de `ALPHAREF` ajustada para casos 4-bit e ARGB packed;
- leitura de `TEX2` no decode de FVF (UV1 separado para stage1);
- estado de render ampliado: `LIGHTING`, `AMBIENT`, `TEXTUREFACTOR`, `FOGENABLE` armazenados no bridge;
- criação/binding real de objetos `VertexDeclaration`, `VertexShader`, `PixelShader` no compat layer (base para próxima etapa de shader legado);
- export de telemetria adicional para diagnóstico de path de draw.

Telemetria observada no smoke atual (select server):

- `drawCalls=15648`
- `primitiveCount=1657559`
- `texturedDraws=26236`
- `shaderDrawsSkipped=0`
- `fvfXyzrhwDraws=480`
- `fvfTex2PlusDraws=9942`
- `fvfWeightedDraws=0`

Leitura técnica do estado atual:

- o frame ainda está majoritariamente escuro/artefatado (não equivalente ao desktop);
- o dado crítico é o volume alto de draw com `TEX2+`, indicando que a próxima correção deve focar na semântica completa de stage1 (principalmente `TEXCOORDINDEX` gerado/transform flags e combinações multi-textura do client).

## Atualização - Bloqueador estrutural removido (`ID3DXSprite`) (2026-04-11)

Descoberta importante:

- no compat atual, `ID3DXSprite` estava declarado com métodos template que retornavam `E_NOTIMPL`;
- isso eliminava draw 2D em compile-time para chamadas como `m_pSprite->Begin/Draw/End` no código C++ original.

Ação aplicada:

- `compat/include/d3dx9.h`: `ID3DXSprite` convertido para interface virtual concreta com assinatura usada no client;
- `compat/src/win32_emscripten_stubs.cpp`: `DummyD3DXSprite` implementado com emissão de quad XYZRHW + textura real no pipeline DX9 compat.

Impacto:

- remove um gargalo estrutural para telas/UI legadas do cliente no navegador;
- mantém a estratégia correta do projeto: portar execução do cliente C++ real para WASM, e não recriar interface/jogabilidade em JS.

## Atualização - Correção de regressão visual forte (2026-04-11, noite)

Sintoma observado após rodada anterior:

- tela inicial com frame quase todo escuro e flicker/artefatos;
- percepção de deslocamento vertical incorreto em parte da renderização.

Causa técnica identificada no bridge DX9/WASM:

- caminho não-`XYZRHW` estava carregando coordenadas em clip-space para um VS que passou a forçar `w=1`, gerando projeção inconsistente;
- `SetViewport` não estava convertendo corretamente `viewport.Y` de top-left (D3D9) para bottom-left (GL/WebGL);
- `Clear` ainda carregava fallback de debug, destoando da semântica real do client.

Correções aplicadas:

- conversão explícita clip->NDC no decode de vértice não-`XYZRHW` (`x/w`, `y/w`, `z_d3d->z_gl`) com `w=1` estável;
- `ApplyViewport` corrigido com inversão de eixo Y e `glDepthRangef(MinZ, MaxZ)`;
- `Clear` alinhado ao DX9 (sem cor forçada e sem clear implícito quando `flags==0`);
- pass `ID3DXSprite` reforçado como overlay 2D (desativa Z/ZWRITE, ativa alpha blend, desativa alpha-test/lighting).

Ajuste de runtime/build:

- link WASM atualizado para `-sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=2` (removido warning de runtime sobre limitação em WebGL1).

Validação desta rodada:

- startup strict permanece com `undefined_total=0`;
- runtime segue estável (`boot/tick/shutdown`, `gameState=7`);
- frame agora volta a exibir conteúdo real (logo WYD) no canvas, embora ainda incompleto em relação ao desktop.

Infra de próximos blocos (shader legado):

- adicionado `webclient/client-wasm/tools/catalog_dx9_shaders.py` para inventariar bytecode DX9 real do pacote;
- relatório gerado em `webclient/client-wasm/build/reports/dx9-shader-inventory.md`:
  - `18` shaders, `12x vs_1_1` e `6x ps_1_1`;
  - famílias de carregamento confirmadas no source (`TMPaths.h`): `skinmesh`, `vseffect`, `pseffect`.

## Atualização - Estado reproduzível e gargalo real (2026-04-11, noite)

- `TM_SELECTCHAR_STATE` já abre no artefato WASM com assets reais e UI nativa do client;
- o artefato visual mais agressivo desta tela era um triângulo preto diagonal sobre a moldura principal;
- o bridge de índices foi endurecido para descartar draw com índice fora de faixa em vez de forçar índice `0`.

Achado mais importante desta rodada:

- no estado atualmente reproduzido pelo harness, o gargalo visual principal não é `VertexDeclaration/skinning`;
- a telemetria mostrou `declDrawCalls=0` e alta quantidade de vértices FVF 3D já dentro do clip.

Implicação de execução:

- próximo bloco deve focar em fidelidade de `SetTextureStageState`/alpha/depth no path FVF legado;
- o suporte a `VertexDeclaration` segue necessário, mas para cenas futuras/personagens, não como bloqueio imediato da tela hoje reproduzida.

## Atualização - Startup scene: bloqueio remanescente após correção de depth (2026-04-16)

Bloco entregue nesta rodada:

- instrumentação por `FVF` no compat layer C++/WASM;
- contadores por `depth write on/off` e por cull mode;
- debug flag novo para desativar cull no browser durante diagnóstico real;
- export dos novos helpers no linker do artefato startup.

Diagnóstico consolidado:

- a cena inicial reproduzida em `TM_SELECTSERVER_STATE` continua incompleta visualmente, mas o bloqueio dominante mudou;
- `depth write` global e `cull` global já não explicam o restante da divergência;
- o foco correto agora é `TMMesh`/`TMGround`/billboards do path FVF legado, principalmente alpha/material/stage-combine.

Leitura estrutural do source confirmada nesta rodada:

- `TMSelectServerScene` carrega bem mais que logo+chão:
  - `TMGround`
  - `TMObjectContainer`
  - `TMSun`
  - `TMSky`
  - `TMSnow`
  - demo humans via `ResetDemoPlayer()`
- `TMMesh::LoadMsa()` duplica UV e promove muitos meshes para `TEX2` somando `+256` no `FVF` em runtime;
- isso reforça que a fidelidade de multi-stage no bridge precisa seguir a semântica real do client, não apenas a forma do asset em disco.

## Atualização - Auditoria de rota e no-ops D3D9 removidos (2026-04-23)

Auditoria de rota:

- a implementação continua centrada no client C++ real em WASM;
- o harness/browser é apenas executor e ferramenta de inspeção;
- referências externas podem acelerar a cobertura de semântica D3D9, mas não substituem o código fonte do pacote como base principal.

Achado principal no compat layer:

- `IDirect3DDevice9::SetMaterial`, `GetMaterial`, `SetLight`, `GetLight` e `LightEnable` ainda estavam como templates no-op no `d3d9.h`;
- o source do client usa essas chamadas em `TMMesh`, `TMGround`, `TMSea`, `TMSky`, `TMObject`, `TMLight` e `RenderDevice`;
- manter essas chamadas como no-op fazia o port se afastar do comportamento real do desktop mesmo com draw calls e assets reais carregando.

Correção entregue:

- métodos tipados adicionados ao header compat;
- estado real de material/luz implementado no bridge C++/WASM;
- fontes de material D3D9 rastreadas como render state;
- telemetria exportada para material/luz (`materialSetCalls`, `lightSetCalls`, `lightEnableCalls`, `lightedVertices`);
- validação automatizada passou em browser com o servidor local estático em `127.0.0.1:8877`.

Nota importante:

- no estado `TM_SELECTSERVER_STATE` reproduzido hoje, os contadores de `SetMaterial/SetLight` ficaram zerados, então essa correção é fundacional e prepara os próximos estados/caminhos;
- a tela inicial ainda exige correções em alpha/stage/fog e provavelmente shader bytecode para convergência com o client original.

## 2026-04-24 - Course correction reinforced

The implementation focus is the original C++ client compiled to WASM. The web page, harness, and local server are only support infrastructure.

Completed in this implementation pass:

- Repaired the fog shader regression by moving fog into CPU-side fixed-function vertex processing.
- Added `audit_d3d9_compat.py` to make D3D9 render-state coverage measurable instead of manually guessed.
- Closed the current used-render-state coverage gap: 32 states used, 32 states handled by the bridge.
- Allowed indexed vertex-buffer draws with active vertex shader and active declaration to continue through the CPU declaration/skinning replay path.
- Kept pixel shader draws as the remaining shader-bytecode blocker.

External route assessment:

- A ready-made one-shot conversion still does not appear realistic for this client.
- `d3d9-webgl` is useful as a fixed-function WebGL2 reference, but importing it blindly would duplicate/replace the bridge already tailored around this source tree.
- Shader bytecode needs a separate track. Investigate MojoShader-style bytecode disassembly/GLSL generation for the 18 real DX9 shader binaries in `v769ClientRelease/shader`.

## 2026-05-06 - Directional checkpoint

The work remained C++ client-first and avoided new JS gameplay logic.

What changed materially:

- Implemented real `GetDeviceCaps` path in the D3D9 device bridge instead of template no-op.
- Added shader object + shader file-open telemetry exported to smoke harness.
- Added state sweep artifacts to classify which `TM_GAME_STATE` paths are currently stable in wasm startup runs.

What the data says now:

- Startup and adjacent tested states are not invoking `CreateVertexShader`/`CreatePixelShader` yet.
- The immediate next value is not bytecode translation in startup state; it is fixing fixed-function scene fidelity and resolving state 8 runtime mismatch.

## 2026-05-16 - Stability-first handling for TM_DEMO_STATE

Observed behavior before this pass:

- Entering game state `8` (`TM_DEMO_STATE`) could crash tick with wasm runtime error (`null function or function signature mismatch`).

Changes made:

- Added Emscripten pointer-cast compatibility flag in startup link.
- Added explicit guard in `wyd_set_game_state` to redirect `TM_DEMO_STATE` to `TM_SELECTSERVER_STATE` until demo path is port-safe.

Result:

- runtime no longer hard-crashes when requesting state 8 in smoke runs;
- state 8 is intentionally non-functional for now (redirect), preserving execution continuity for remaining client states.

## 2026-05-16 - Startup-screen convergence checkpoint (FVF 322 focus)

Implemented this pass:

- Switched CPU vertex decode output from pre-divided NDC to preserved clip-space (`x,y,z,w`) for non-`XYZRHW` paths.
  - Applied in both FVF decode and declaration decode.
  - Vertex shader now forwards `gl_Position = aPos` directly.
- Added GL runtime error telemetry at draw boundary:
  - `wyd_d3d9_gl_error_total`
  - `wyd_d3d9_gl_error_draw_calls`
  - `wyd_d3d9_gl_error_last`
- Added depth-guard telemetry for startup path:
  - `wyd_d3d9_depth_write_guard_forced_draws`
- Added targeted startup diagnostics to smoke harness:
  - `--debug-skip-fvf <code>` support,
  - top FVF histograms and FVF322 path counters,
  - screenshot + JSON reports for baseline and diagnostic variants.
- Narrowed diffuse-alpha promotion heuristic:
  - alpha `0 -> 255` promotion now occurs only when alpha-test is active.

Measured startup state (`TM_SELECTSERVER_STATE`, 60 ticks):

- baseline artifact (`startup-smoke-state7-clipspace.json`):
  - `drawCalls=28765`, `depthWriteGuardForcedDraws=25994`
  - `fvfTop`: `594(19577)`, `322(7480)`, `324(1654)`
  - `glErrorTotal=0`
- FVF isolation:
  - with `--debug-skip-fvf 322`, frame becomes significantly cleaner and stable (`startup-canvas-state7-skip322.png`), confirming FVF322 as dominant corruption contributor.
- alpha-gated run (`startup-canvas-state7-alpha-gated.png`):
  - large red overlay area reduced materially versus prior baseline; scene still not fully converged to desktop.

Current interpretation:

- startup rendering is alive and deterministic in wasm, but visual parity is still blocked by FVF322-heavy draw groups.
- this is now a narrow compatibility problem in legacy indexed fixed-function path, not a broad "WebGL down" or shader-bytecode startup blocker.

Next immediate block:

1. converge FVF322 indexed draw semantics to desktop-equivalent output;
2. keep startup visual gate tracked via `docs/startup-screen-checklist.md`;
3. only after stable startup frame, continue to next scene/state parity.

## 2026-05-29 - Startup round with visual regression checks

Round objective:

- execute another startup fidelity pass with strict visual before/after checks;
- keep only changes that improve or stabilize the real C++ WASM client scene.

Implemented this pass:

- enforced blend state per draw in `UploadAndDraw` (prevent stale blend state carry between passes);
- added debug probes for targeted validation:
  - `kDebugDisableFog` (`1<<5`),
  - `kDebugDisableBlend` (`1<<6`),
  - `kDebugSwapTextureRB` (`1<<7`);
- tightened texture-stage semantics in bridge path:
  - `ApplyStageUniforms` now returns stage-active status,
  - stage1 is force-disabled when stage0 color-op is disabled,
  - when no texture is bound for a stage, `D3DTA_TEXTURE` fallback maps to `D3DTA_DIFFUSE`,
  - when color-op is enabled but alpha-op is disabled, alpha-op is normalized to `SELECTARG1` for deterministic behavior.

Visual verification matrix (state 7, 60 ticks):

- baseline before this pass: heavy red/green diagonal overlays still dominant;
- `debugFlags=32` (disable fog): no useful gain, scene remained corrupted;
- `debugFlags=64` (disable blend): severe regression (black quads / overlay artifacts), rejected;
- `debugFlags=128` (swap texture R/B): severe color mismatch (logo turns blue), rejected;
- kept path after stage-semantics fix (`startup-canvas-state7-stagefix.png`):
  - reduced number/intensity of diagonal overlays compared to immediate baseline,
  - still not parity with desktop.

## 2026-05-30 - Guarded screen-space replay for `FVF 322`

Objective of this pass:

- test whether the startup corruption is partly caused by a subset of `FVF 322`
  draws being replayed as world-space meshes when they actually behave like
  viewport-space quads.

Implemented this pass:

- added a guarded compatibility branch in
  `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp`:
  - when an `FVF 322` vertex falls inside a relaxed viewport-space envelope
    (`x/y` near the current viewport and `z` in `[0,1]`), the bridge now maps
    it through the same screen-space projection used for `XYZRHW`;
  - legacy lighting/fog are skipped on that heuristic path so those quads do
    not get reinterpreted as world geometry.
- re-linked the startup artifact and re-ran smoke with the local static server
  still served on `http://127.0.0.1:8877/`.

Measured result:

- 60 ticks comparison against `startup-smoke-state7-fvf322telemetry-60ticks.json`:
  - `drawCalls`: `35461 -> 28317`
  - `blendEnabledDraws`: `9233 -> 2331`
  - `fvf322DrawPrimitiveUp`: `7365 -> 463`
  - `fvf322DrawIndexedPrimitive`: `6989 -> 6707`
  - `fvf322ScreenlikeDraws`: `12993 -> 5808`
- 600 ticks comparison against `startup-smoke-state7-fvf322telemetry-600ticks.json`:
  - `drawCalls`: `372521 -> 274218`
  - `fvf322DrawPrimitiveUp`: `120803 -> 17562`
  - `fvf322DrawIndexedPrimitive`: `33471 -> 40485`
  - `fvf322ScreenlikeDraws`: `147239 -> 49365`

Visual classification:

- short run (`startup-canvas-state7-screencompat-60ticks.png`): partial progress.
  The logo remains readable and some `FVF 322` overlay pressure is reduced, but
  the frame is still dominated by large diagonal planes and the background is
  still incorrect.
- long run (`startup-canvas-state7-screencompat-600ticks.png`): regression / no
  acceptable gain. Terrain splitting and solid red polygons still take over the
  scene during camera motion.

Current conclusion:

- the startup corruption does include a real viewport-space subfamily inside
  `FVF 322`, and the heuristic replay meaningfully changes the telemetry;
- however, that heuristic alone is not sufficient for startup parity and does
  not survive the longer motion loop;
- the next pass should instrument the remaining `FVF 322` indexed path more
  tightly, especially separating `DrawIndexedPrimitive` from `DrawPrimitiveUP`
  state families and checking which stage/blend combinations still generate the
  large diagonal solids.

Metrics snapshot (`startup-smoke-state7-stagefix.json`):

- `drawCalls=35378`
- `texturedDraws=55594`
- `blendEnabledDraws=9156`
- `depthWriteGuardForcedDraws=26483`
- `glErrorTotal=0`

Status:

- progression observed, but startup remains incomplete;
- dominant blocker remains fixed-function indexed scene path around FVF322/FVF594 behavior.

## 2026-05-29 - Culling parity + FVF322 telemetry pass

What changed:

- Added culling parity instrumentation in the WASM D3D9 bridge:
  - dynamic front-face selection path supports mirrored winding input;
  - new counters exported to smoke:
    - `cullMirrorWorldviewDraws`
    - `cullFrontfaceFlippedDraws`.
- Connected missing clip-w counters in startup linker exports:
  - `clip_w_reject_draws`
  - `clip_w_reject_triangles`
  - `clip_w_keep_triangles`.
- Extended smoke collector with browser-console summary and culling counters.
- Added FVF322-focused telemetry:
  - `fvf322ScreenlikeVertices`
  - `fvf322ScreenlikeDraws`.

Measured outcomes:

- Mirror-winding draws are present, but in tested startup runs they did not coincide with active cull modes:
  - `cullMirrorWorldviewDraws > 0`
  - `cullFrontfaceFlippedDraws = 0`.
- Depth-guard narrowing experiment (only blend/alpha) was tested and reverted:
  - it reduced depth-write suppression counts,
  - but produced worse visual output (large red polygon takeover).
- FVF322 remains the dominant startup blocker:
  - `--debug-skip-fvf 322` still produces the clearest frame among tested variants.

Current interpretation:

- this startup issue is not yet solved by front-face/cull inversion parity;
- the highest-value next work remains inside the FVF322 render family (state/profile-level compatibility), not a broad WebGL runtime failure.

## 2026-06-01 - Clip-triangle fallback trial + smoke collector hardening

What changed:

- Tried a stricter compatibility path in
  `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp` for the
  existing auto clip-w startup guard:
  - instead of dropping any `FVF 322/594` triangle whose vertices fail the
    `clip.w` stability gate, the bridge now routes those primitives through
    `ClipTriangleToClipVolume` and emits the clipped polygon back as triangles.
- Hardened `webclient/client-wasm/tools/playwright_startup_smoke.mjs` so the
  new asset-failure sample readout does not abort when optional Emscripten
  runtime helpers (`UTF8ToString`, `HEAPU8`) are not exported by the startup
  artifact.
- Kept the local static server reachable on `http://127.0.0.1:8877/`.

Measured result:

- 60-tick smoke completed with screenshot
  `startup-canvas-state7-cliptri-60ticks-20260601-113723.png`.
- 60-tick counters:
  - `drawCalls=38098`
  - `primitiveCount=2963891`
  - `blendEnabledDraws=11279`
  - `depthWriteGuardForcedDraws=27080`
  - `clipWRejectDraws=20337`
  - `fvf322AutoClipWRejectDraws=14323`
  - `fvf594AutoClipWRejectDraws=6014`
  - `glErrorTotal=0`
- The long-loop (`600` ticks) smoke became markedly slower under this path and
  did not produce a useful replacement baseline within this run's practical
  window, which is itself a regression signal for default startup use.

Visual classification:

- Regression.
- The clipped-triangle fallback reintroduced large dark-red planes across the
  upper half of the startup frame, obscuring the scene more aggressively than
  the prior default baseline
  (`startup-canvas-state7-postpatch-baseline-20260601-091257.png`).
- Terrain/ground remained partially visible, but the result moved away from
  startup fidelity rather than toward it.

Current conclusion:

- Fully replaying clipped polygons for the current `FVF 322/594` startup
  families is too broad for the default bridge path.
- The fallback may still be useful later behind a narrower state/profile gate,
  but as tested here it increases overdraw and long-loop cost.
- Kept the polygon-repair branch only as an opt-in debug path
  (`kDebugClipTriangleRepair`) and re-linked the default startup artifact back
  to the prior reject-only clip-w behavior after the measurement run.
- Next work should stay focused on targeted state-family narrowing:
  - distinguish which `FVF 594` draw subsets actually benefit from clip repair,
  - correlate those subsets with blend/depth/cull state before enabling any
    polygon reconstruction by default,
  - keep the startup smoke collector defensive so new telemetry cannot abort the
    run.

## 2026-06-05 - FVF322 replay narrowing audit (regression + revert)

Correção experimental testada no compat layer:

- em `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp`, duas
  tentativas de estreitar o replay near-screen de `FVF 322` foram aplicadas e
  medidas:
  - variante `overlaygate`: só aceitar o fallback near-screen quando o draw
    parecesse overlay (`cull none` combinado com `blend` ou `alpha test`);
  - variante `smallbatch`: só aceitar o fallback near-screen para batches
    pequenos (`vertex_count <= 8`).
- ambas foram revertidas no mesmo run depois do smoke porque pioraram a
  composição visual da startup.

Validação após relinks:

- servidor local mantido acessível em `http://127.0.0.1:8877/`;
- startup relinkado com sucesso em cada iteração (`startup-link.md` continuou
  com `returncode=0`, `undefined_total=0`);
- smokes produzidos nesta rodada:
  - `webclient/client-wasm/build/reports/startup-smoke-20260605-fvf322-overlaygate-t120.json`
  - `webclient/client-wasm/build/reports/startup-smoke-20260605-fvf322-smallbatch-t120.json`
  - `webclient/client-wasm/build/reports/startup-smoke-20260605-fvf322-nearscreen-reconfirm-t120.json`

Métricas principais vs referência azul anterior
`startup-smoke-20260604-fvf322-nearscreen-t120.json`:

- `overlaygate`:
  - `drawCalls: 63456 -> 81821`
  - `blendEnabledDraws: 8902 -> 26537`
  - `fvf322ScreenlikeReplayDraws: 4769 -> 26499`
  - `glErrorTotal: 0 -> 0`
- `smallbatch`:
  - `drawCalls: 63456 -> 81318`
  - `blendEnabledDraws: 8902 -> 26262`
  - `fvf322ScreenlikeReplayDraws: 4769 -> 23321`
  - `glErrorTotal: 0 -> 0`
- `near-screen` revalidado após revert:
  - `drawCalls: 63456 -> 82566`
  - `blendEnabledDraws: 8902 -> 27209`
  - `fvf322ScreenlikeReplayDraws: 4769 -> 28375`
  - `glErrorTotal: 0 -> 0`

Classificação visual:

- `overlaygate`: regressão forte; o fundo azul desaparece e a cena volta a um
  fundo cinza com fragmentos vermelho/preto largos no lado direito.
- `smallbatch`: regressão forte; reduz alguns vazamentos no miolo, mas mantém o
  fundo cinza e amplia a massa de mesh quebrada à direita.
- pós-revert: o código volta ao heurístico near-screen de `2026-06-04`, mas a
  captura revalidada de hoje continua pior do que o frame azul do dia
  `2026-06-04`, então o problema atual não está explicado apenas pelo gate
  recém-testado.

Interpretação atual:

- o narrowing por estado ou por tamanho do batch, na forma testada, não separa
  corretamente quads de UI dos draws 3D legados e empurra batches demais para o
  replay 2D;
- há drift mensurável entre a captura de `2026-06-04` e a reconfirmação de
  `2026-06-05` mesmo com o mesmo heurístico near-screen no código;
- o próximo passo precisa sair da heurística puramente geométrica e auditar por
  origem/tipo do draw:
  - `fvf322DrawPrimitiveUp` subiu para `23409`;
  - `fvf322WithStage0Texture` subiu para `39637`;
  - `fvf322ScreenlikeReplayDraws` subiu para `28375`.

Próxima hipótese de trabalho:

- instrumentar assinatura por origem do draw `FVF 322` (UP vs indexed, count de
  vértices/primitivos, estado de textura/blend) antes de qualquer novo gate;
- comparar diretamente a ordem dos draws entre os artefatos `20260604` e
  `20260605` para descobrir se o drift veio de link/build drift, asset order ou
  startup state divergence;
- manter o artefato servido em `:8877` no heurístico near-screen original até
  existir uma assinatura reproduzível para os batches que geram os fragmentos.
