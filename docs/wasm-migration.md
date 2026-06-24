# WYD Web Port - Plano de Migração WASM

## Objetivo

Parar de duplicar lógica de jogo em JavaScript e reaproveitar o client C++ como núcleo comportamental, compilado para WASM.

## Princípio de arquitetura

- C++ (WASM): regras de parsing, estado e comportamento do client.
- JavaScript: host/browser glue (fetch/assets, input DOM, render backend, debug UI).
- Python/Flask: serving local de assets reais e endpoints auxiliares de index.

## Estado inicial já entregue

- toolchain Emscripten instalado e funcional.
- módulo `webclient/wasm/src/wyd_core.cpp` compilando para browser.
- bridge `webclient/app/src/wasmBridge.js` integrada ao app.
- parsers C++ já ativos no runtime:
  - `wyd_parse_msh_header`
  - `wyd_parse_trn_overview`

## Estado atual (2026-04-11)

- pipeline automatizada de varredura do projeto C++ completo (`TMProject.vcxproj`) ativa:
  - `webclient/client-wasm/tools/run_tmproject_wasm_scan.sh`
  - relatório: `webclient/client-wasm/build/reports/tmproject-wasm-scan.{json,md}`
- **scan sintático do client inteiro**: `109/109` TUs OK com `em++ -fsyntax-only`.
- pipeline de compilação para objetos WASM ativa:
  - `webclient/client-wasm/tools/run_tmproject_wasm_objects.sh`
  - relatório: `webclient/client-wasm/build/reports/tmproject-wasm-objects.{json,md}`
- **build de objetos do client inteiro**: `109/109` TUs OK com `em++ -c`.
- artefato monolítico de prova (sem entrypoint) gerado a partir dos 109 objetos:
  - `webclient/client-wasm/build/monolith/tmproject_monolith.js`
  - `webclient/client-wasm/build/monolith/tmproject_monolith.wasm`
- bootstrap de startup do client C++ adicionado:
  - entrypoint C exportado: `_wyd_start_client` (`compat/src/wyd_client_entry.cpp` chamando `wWinMain`);
  - exports cooperativos adicionados para browser loop:
    - `_wyd_boot_client`
    - `_wyd_tick_client`
    - `_wyd_shutdown_client`
  - linker strict de startup automatizado:
    - `webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh`
    - relatórios: `webclient/client-wasm/build/reports/startup-link.{json,md}`
- **startup link strict** estabilizado com `0` símbolos indefinidos.
- artefatos WASM de startup gerados:
  - `webclient/client-wasm/build/link/tmproject_startup.js`
  - `webclient/client-wasm/build/link/tmproject_startup.wasm`
- camada de compatibilidade expandida:
  - `compat/src/win32_emscripten_stubs.cpp` com Win32/MMIO/COM/Socket/WinInet stubs + base matemática D3DX (`matriz`, `vetor`, `quaternion`, `interseção`).
- harness de browser para validação de bootstrap:
  - `webclient/client-wasm/build/link/startup_harness.html`
  - runtime inicializa e carrega WASM no browser com sucesso.
- bootstrap cooperativo estabilizado no browser:
  - correção de loop infinito em `EnumDisplaySettings` no shim Win32;
  - `CreateConsole()` desativado em WASM para evitar redirecionamento inválido de `stdout/stdin`;
  - `InitMusicList()` corrigido para parsing seguro (sem acesso fora de faixa);
  - som/música temporariamente desativados no path WASM enquanto backend de áudio migra.
- preload mínimo de assets reais no startup link:
  - manifesto: `webclient/client-wasm/config/startup-preload-manifest.txt`;
  - artefato gerado: `webclient/client-wasm/build/link/tmproject_startup.data`.
- smoke test de runtime cooperativo adicionado:
  - script: `webclient/client-wasm/tools/playwright_startup_smoke.mjs`;
  - relatório: `webclient/client-wasm/build/reports/startup-smoke.json`;
  - resultado atual: `_wyd_boot_client=1`, `ticks>=0`, `_wyd_shutdown_client=1`.

## Fases seguintes (ordem de execução)

1. **Parsers de asset para WASM (prioridade alta)**
- portar `msh`, `msa`, `ani`, `bon`, `trn`, `dat` completos;
- manter saída de dados compatível com o pipeline JS atual para migração gradual;
- remover parser JS correspondente assim que a versão WASM estabilizar.

2. **Runtime de animação/personagem no core C++**
- mover seleção `BoneAni4 + ValidIndex` para C++;
- mover stepping de animação e blend para C++;
- JS passa a apenas enviar input e aplicar buffers de saída no render.

3. **Adaptação DX9 para backend browser**
- extrair semântica de estado/material/matriz no core;
- mapear para command stream render agnóstico;
- implementar executor WebGL2 no lado JS, com evolução posterior para WebGPU.

4. **Cena/UI/mapa com lógica central em C++**
- fluxo de update/scene graph primário no core WASM;
- JS mantém binding de assets e desenho.

5. **Rede (depois)**
- somente após pipeline offline (mapa+char+UI) estar estável no browser.

## Critério de aceitação por subsistema

- mesma entrada binária do client original;
- mesma decisão funcional de alto nível (seleção de clip, transforms, índices, etc.);
- fallback JS desativável por flag após estabilidade;
- teste automático de regressão com Playwright.

## Próximo bloqueio técnico imediato

- `_wyd_boot_client` e `_wyd_tick_client` já rodam sem travar, mas o caminho de render ainda usa shim DX9 não-funcional (sem rasterização WebGL real).
- `MeshManager` no tick foi temporariamente adiado no path WASM para manter o loop vivo; precisa voltar com loaders seguros e compatíveis com browser.
- próxima etapa é implementar o primeiro backend DX9->WebGL2 funcional (device state + textura/surface/buffer mínimo), removendo gates temporários de bootstrap.

## Marco novo alcançado (2026-04-11, tarde)

- preload de startup ampliado para pacote de cena real (`~500 MB`);
- correção de IO C-runtime no WASM:
  - `fopen_s` agora usa resolução case-insensitive com normalização de separador;
  - remove falhas de `LoadRC("UI\\*.bin")` em ambiente browser.
- transição automática para estado de jogo real reativada:
  - `TM_SELECTSERVER_STATE` após `InitMeshManager`.
- exports de inspeção de estado adicionados no runtime:
  - `_wyd_get_game_state`
  - `_wyd_set_game_state`
- smoke E2E atualizado e validado:
  - `boot=1`, `shutdown=1`, ticks estáveis;
  - `gameState=7` (select server);
  - `drawCalls>0` e `primitiveCount>0` com frame visível no canvas.

Novo bloqueio principal pós-marco:

- apesar de renderizar volume alto de primitivas da cena real, ainda faltam equivalências DX9 de maior fidelidade (FFP completo, materiais/luzes/fog/shaders legados `vs_1_1/ps_1_1`) para aproximar o visual/idêntico do desktop.

## Atualização de diagnóstico global (2026-04-12)

Após revalidação em browser do estado `TM_SELECTSERVER_STATE`:

- o runtime C++/WASM continua estável e acumulando draw calls reais da cena;
- o quadro inicial permaneceu divergente do desktop mesmo após desligar `alpha test`, descartando esse ramo como causa principal isolada;
- a revisão do bridge encontrou um erro estrutural na emulação FFP: o `CURRENT` usado por `ALPHAOP` era o resultado do `COLOROP` da mesma stage, quando o D3D9 usa o `CURRENT` da stage anterior para ambos.

Mudança aplicada no compat layer:

- stage0/stage1 agora usam snapshot consistente de `CURRENT` ao resolver `COLORARG/ALPHAARG`;
- a saída encadeada de `CURRENT` entre stages passou a ser `vec4(stageRgb, stageAlpha)`.

Essa correção não depende de protótipo JS e atua diretamente no pipeline do client C++ portado para WASM.

## Atualização de execução real (2026-04-11, noite)

Ajuste de direção aplicado no runtime web:

- Flask foi removido do caminho principal de execução;
- runtime C++ WASM passa a rodar pelo servidor estático em `:8877` (`build/link`), com entrada padrão em `/`.

Entrega técnica desta rodada:

- `index.html` de entrada em `webclient/client-wasm/build/link/` com redirecionamento para o harness;
- `startup_harness.html` em modo auto-run:
  - boot automático;
  - set de estado para `TM_SELECTSERVER_STATE`;
  - loop de tick contínuo via `requestAnimationFrame`.

Bridge DX9/WASM avançado para textura real:

- decode de textura no próprio runtime C++ (`D3DXCreateTextureFromFileInMemoryEx`):
  - TGA (`WT10`), DDS DXT1/DXT3 (`WS10`);
- upload de textura em WebGL no compat layer com invalidação por lock/unlock.

Novos exports de diagnóstico no módulo:

- `_wyd_d3d9_tex_decode_success`
- `_wyd_d3d9_tex_decode_fail`
- `_wyd_d3d9_tex_uploads`
- `_wyd_d3d9_textured_draws`

Métricas atuais do fluxo C++ real em browser:

- `gameState=7`;
- `texDecodeSuccess=229`;
- `texDecodeFail=0`;
- `texUploads=103`;
- `texturedDraws=67986`.

Próximo bloco (sem voltar para protótipo JS):

1. fechar semântica de fixed-function avançada (alpha op, stage op completos, material/luz/fog);
2. integrar caminho de shader legado (`vs_1_1/ps_1_1`) para personagens/effects;
3. continuar convergência visual até o frame equivaler ao client desktop.

## Atualização - Migração focada em fidelidade DX9 (2026-04-11, fim do dia)

Mudanças estruturais da migração nesta etapa:

- bridge DX9 no C++ recebeu revisão de semântica de textura (ops/args/stage1) ao invés de ajustes no protótipo web;
- `d3d9.h` de compatibilidade atualizado para constantes reais de `D3DTEXTUREOP` (base obrigatória para convergência visual);
- runtime agora possui objetos COM reais para `VertexDeclaration`, `VertexShader`, `PixelShader` (ainda sem execução de bytecode DX9, mas com lifecycle/estado válidos para evolução incremental);
- scripts de validação Playwright atualizados com métricas de caminho de draw/FVF.

Resultado técnico medido:

- runtime estável em `boot -> tick -> shutdown` com estado `gameState=7`;
- draw pipeline ativo e carregando assets reais (`texDecodeSuccess>0`, `texturedDraws>0`);
- divergência visual ainda alta, com forte indicação de gap em stage1/multi-texture (`fvfTex2PlusDraws` elevado).

Direção da próxima virada (WASM C++ first, sem regressão para protótipo):

1. implementar semântica completa de stage1 (`TEXCOORDINDEX` gerado + transform flags);
2. em seguida atacar shader legado DX9 (`vs_1_1/ps_1_1`) no bridge para personagens/effects;
3. manter validação contínua por screenshot + telemetria exportada, sempre sobre assets reais de `v769ClientRelease`.

## Atualização - Foco em execução real do cliente C++ (2026-04-11, noite)

Mudança de prioridade técnica executada nesta etapa:

- correção de um bloqueador de fidelidade no próprio runtime C++: `ID3DXSprite` estava como no-op em compile-time no compat;
- migração do sprite para implementação funcional no bridge DX9/WASM (`Begin/Draw/End`), preservando fluxo de chamada original do client.

Complemento no backend de render:

- `SetTransform` agora aceita `D3DTS_TEXTURE0..7`;
- shader FFP recebe estado de `TEXCOORDINDEX` e `TEXTURETRANSFORMFLAGS` para stage0/stage1;
- base preparada para TCI gerado (camera normal/position/reflection) sem reescrever cenas/gameplay em JS.

Resultado da etapa:

- artefato liga e executa com assets reais, sem undefined symbols;
- ainda não houve convergência visual final da tela inicial, mas os pontos removidos nesta rodada eram estruturais e bloqueavam fidelidade futura do client real em browser.

## Atualização - Revisão técnica orientada por casos externos (2026-04-11, noite)

Decisão de execução após revisão de referências de migração DX9->WebGL/WASM:

- manter abordagem `C++ client first` (sem reconstrução de gameplay/UI em JS), com bridge DX9 incremental no compat layer;
- adotar WebGL2 como alvo principal do artefato (`MAX_WEBGL_VERSION=2`) para reduzir limitações de backend durante a convergência visual;
- tratar shader bytecode DX9 (`vs_1_1/ps_1_1`) como próximo bloco estrutural de fidelidade, em vez de tentar “compensar” por ajustes pontuais no canvas/harness.

Bloco técnico entregue nesta revisão:

- correção de mapeamento clip-space/viewport no backend C++ WASM (estabilidade de rasterização);
- semântica de `ID3DXSprite` aproximada de overlay DX9 para recuperar UI 2D do cliente real;
- runtime browser validado com conteúdo real da cena inicial (logo) e sem regressão de bootstrap.

Plano imediato (alinhado ao objetivo final de client idêntico):

1. fechar fidelidade de fixed-function faltante (material/light/fog e combinações de stage ainda divergentes);
2. integrar caminho real de shaders DX9 bytecode no bridge (`Create*Shader`/`Set*Shader`);
3. usar o harness somente como executor/inspeção de runtime C++ WASM, não como destino de produto.

Suporte já preparado para o item 2:

- inventário automatizado de shaders DX9 adicionado em:
  - `webclient/client-wasm/tools/catalog_dx9_shaders.py`
- relatório base de migração (assets reais):
  - `webclient/client-wasm/build/reports/dx9-shader-inventory.md`
  - confirma conjunto `vs_1_1/ps_1_1` e paths de carga em `TMPaths.h`.

## Atualização - Bridge FVF e estado da SelectChar (2026-04-11, noite)

Bloco entregue:

- suporte inicial real a `VertexDeclaration` no compat layer (base pronta para skinning futuro);
- telemetria adicional para distinguir draws FVF 3D, draws com `VertexDeclaration` e corrupção por índices;
- descarte explícito de draw indexado inválido para eliminar artefatos explosivos.

Validação em browser:

- `TM_SELECTCHAR_STATE` inicializa e mostra a moldura principal com assets reais;
- o triângulo preto diagonal sobre a UI foi removido;
- a geometria 3D FVF continua sendo desenhada e entrando no clip, mas ainda não converge visualmente, indicando bloqueio remanescente em texture-stage/alpha/depth do pipeline DX9 legado.

## Atualização - Próximo foco técnico após a reavaliação do startup (2026-04-16)

Decisão de execução atualizada:

- manter o client C++/WASM como fonte única de comportamento;
- usar o harness apenas como executor/inspeção do runtime real;
- tratar a divergência restante do startup como problema de fidelidade do pipeline FVF legado, não como falta de backend básico.

Próximo bloco priorizado:

1. instrumentar material/alpha/stage-combine por classes de mesh relevantes (`TMGround`, `TMMesh`, billboards);
2. confirmar quais objetos do `TMObjectContainer` e do demo humano entram ou não no frame real do startup;
3. corrigir a semântica restante do fixed-function antes de abrir novamente o bloco de shaders DX9 programáveis para skinning/personagens.

## Atualização - Revisão de rota e compat D3D9 material/luz (2026-04-23)

Reavaliação de estratégia:

- a rota correta continua sendo compilar o client C++ real para WASM e adaptar a camada Windows/D3D9, não evoluir o protótipo JS/Flask;
- projetos externos como `d3d9-webgl` confirmam que uma camada D3D9 FFP sobre WebGL2 é uma abordagem viável para jogos legados, mas o wrapper é FFP-only e não resolve os shaders programáveis `vs_1_1/ps_1_1` usados pelo WYD;
- `MojoShader` segue como candidato de referência para o bloco de shader bytecode, pois cobre bytecode Shader Model 1..3 e perfis GLSL, mas ainda precisa ser integrado ao nosso bridge e aos formatos reais do client;
- Emscripten recomenda mirar o subset WebGL-friendly de OpenGL ES 2/3 quando possível; por isso manteremos o bridge direto D3D9->WebGL no compat layer, sem depender de emulação GL legado como solução principal.

Correção aplicada no runtime C++/WASM:

- `IDirect3DDevice9::SetMaterial`, `GetMaterial`, `SetLight`, `GetLight`, `LightEnable` e `GetLightEnable` deixaram de ser no-op no header compat;
- o bridge agora preserva `D3DMATERIAL9`, até 8 `D3DLIGHT9` e fontes de material (`D3DRS_*MATERIALSOURCE`);
- enums de material source foram adicionados e `D3DRS_SPECULARMATERIALSOURCE` foi corrigido para o valor D3D9 real;
- foi adicionada iluminação fixed-function por vértice para o caminho FVF/declaração quando luzes D3D reais estiverem habilitadas;
- guardrail transitório: se `D3DRS_LIGHTING` está ligado mas nenhuma luz foi instalada por `LightEnable`, a cor de vértice/textura é preservada para evitar escurecer artificialmente a cena de startup.

Validação:

- `run_tmproject_wasm_startup_link.sh` concluiu com `undefined_total=0`;
- smoke Playwright em `TM_SELECTSERVER_STATE` passou com `60/60` ticks, `boot=1`, `shutdown=1`, `gameState=7`;
- métricas da rodada: `drawCalls=26689`, `primitiveCount=2619692`, `texDecodeSuccess=131`, `texUploads=43`, `texturedDraws=46598`;
- no estado de startup atual, `materialSetCalls=0` e `lightSetCalls=0`, então a mudança é fundação de compatibilidade e não deve ser vendida como convergência visual final desta tela.

Próximo bloco de maior retorno:

1. usar a cobertura de `d3d9-webgl` como checklist de gaps FFP ainda faltantes, especialmente fog, stage1 e render target;
2. mapear por telemetria quando `SetMaterial/SetLight` passam a ser chamados em `SelectChar`/personagem;
3. iniciar integração controlada de tradução de shader bytecode (`vs_1_1/ps_1_1`), preferencialmente avaliando `MojoShader` antes de reescrever shaders manualmente.

## 2026-04-24 - D3D9 compat coverage pass

Current direction remains C++ client first: the browser target is the Emscripten/WASM build of `Projects/TMProject`, with JavaScript/HTML used only as the loader and diagnostic harness.

Implemented this pass:

- Added an automated D3D9 compatibility audit at `webclient/client-wasm/tools/audit_d3d9_compat.py`.
- Generated `webclient/client-wasm/build/reports/d3d9-compat-coverage.md` and `.json`.
- The audit currently sees 32 render states used by the source and 32 handled by the WASM D3D9 bridge.
- Added explicit state handling for `FILLMODE`, `SHADEMODE`, `SPECULARENABLE`, `DITHERENABLE`, `STENCILENABLE`, `CLIPPING`, `MULTISAMPLEANTIALIAS`, `ANTIALIASEDLINEENABLE`, `VERTEXBLEND`, `INDEXEDVERTEXBLENDENABLE`, and `NORMALIZENORMALS`.
- `D3DFILL_WIREFRAME` now has a triangle-list wireframe fallback in the WebGL bridge.
- Vertex-shader indexed VB draws with a valid D3D vertex declaration are no longer skipped, because the existing CPU skinning/declaration replay path can process the skinmesh constants. Pixel-shader draws remain blocked until bytecode translation/rewrite is connected.

Mass-conversion assessment:

- `d3d9-webgl` is relevant as a reference/checklist for D3D9 fixed-function over WebGL2, but cannot be treated as a drop-in answer for this client because WYD also uses real `vs_1_1` and `ps_1_1` bytecode.
- `MojoShader` remains the most relevant candidate to investigate for the shader-bytecode phase.
- The pragmatic path is still incremental D3D9 compatibility inside the C++/WASM bridge, not a JS gameplay rewrite.

## 2026-05-06 - Shader path telemetry and state sweep

This pass focused on proving whether startup rendering issues were caused by missing DX9 shader execution.

Implemented:

- Added runtime shader telemetry in the D3D9 bridge:
  - create/bind/used/skipped counters for VS/PS objects;
  - active shader hash (hi/lo 32-bit split);
  - top-3 shader hash buckets by usage.
- Added runtime file-open telemetry for `shader/*.bin` in `_open`:
  - attempts/success/fail,
  - family counters (`skinmesh`, `vseffect`, `pseffect`).
- Added `GetDeviceCaps(D3DCAPS9*)` implementation on `IDirect3DDevice9` path with `vs_1_1` / `ps_1_1` advertised.
- Added offline matching tool for runtime shader hashes against real shader binaries:
  - `webclient/client-wasm/tools/match_dx9_shader_hashes.py`;
  - outputs `shader-runtime-match.json/.md`.
- Added state sweep batch run using the existing smoke harness:
  - per-state JSON and screenshots in `webclient/client-wasm/build/reports/state-sweep/`.

Measured result (startup + state sweep):

- Startup state continues rendering through the C++ client path with real assets.
- `vsUniqueShaders=0`, `psUniqueShaders=0`, and shader file-open counters remain `0` across tested states.
- Therefore current visual corruption in startup is not from "shader bytecode currently skipped"; the scene is running on fixed-function-style path in this phase.
- State `8` (`TM_DEMO_STATE`) fails with runtime error: `null function or function signature mismatch` during tick, indicating a new concrete blocker for that state path.

## 2026-05-16 - Demo-state runtime trap guard

Follow-up execution pass focused on the `state 8` runtime trap (`null function or function signature mismatch`).

Implemented:

- Enabled `-sEMULATE_FUNCTION_POINTER_CASTS=1` in the startup wasm link configuration for broader legacy compatibility checks.
- Added a runtime guard in `wyd_set_game_state`:
  - requests for `TM_DEMO_STATE` are currently redirected to `TM_SELECTSERVER_STATE`.
  - rationale: demo-state path still triggers an indirect-call trap in wasm and would crash continuous execution.

Validation:

- Strict startup link remains clean (`undefined_total=0`).
- `state 8` no longer crashes runtime in smoke; it now stays alive and renders through redirected `state 7` path.
- guard summary report: `webclient/client-wasm/build/reports/state-sweep-guard-summary.md`.

Status interpretation:

- This is a stability guard, not final fidelity for demo scene.
- It keeps the C++ client loop alive while we continue porting missing demo-scene dependencies.

## Atualização - Startup fidelity (2026-05-16)

Objetivo da rodada:

- reduzir corrupção visual da tela inicial mantendo o client C++ real em WASM;
- transformar o diagnóstico em rotina reproduzível com métricas objetivas.

Entregas técnicas:

- decode de vértices não-`XYZRHW` passou a preservar clip-space (`x,y,z,w`) em vez de enviar NDC pré-dividido;
- VS do bridge passou a usar `gl_Position = aPos`;
- telemetria nova para erros GL por draw (`gl_error_total`, `gl_error_draw_calls`, `gl_error_last`);
- telemetria nova para guard de depth (`depth_write_guard_forced_draws`);
- smoke harness ampliado com:
  - `--debug-skip-fvf`;
  - top FVF e histograma depth-write;
  - contadores específicos do caminho FVF322.
- heurística de promoção de alpha difuso foi restringida para casos com alpha-test ativo.

Resultado observado:

- o estado `TM_SELECTSERVER_STATE` continua estável (boot/tick/shutdown ok);
- sem erros GL reportados no smoke (`glErrorTotal=0`);
- `FVF 322` foi isolado como principal origem da corrupção visual (skip de FVF322 limpa grande parte do frame);
- a fidelidade melhorou, mas a tela inicial ainda não está equivalente ao desktop.

Próxima fase obrigatória antes de avançar estados:

1. convergir path indexado FVF322 (sem overlays diagonais/mesh popping);
2. validar estabilidade visual em 3 execuções consecutivas sem `debug-skip-fvf`;
3. manter check formal em `docs/startup-screen-checklist.md` a cada execução.

## Atualização - rodada visual com validação de regressão (2026-05-29)

Nesta rodada foi aplicado um bloco de estabilidade no bridge DX9/WebGL e cada hipótese foi validada visualmente com screenshot comparativo.

Mudanças mantidas:

- aplicação explícita de blend state por draw no `UploadAndDraw`;
- ajuste de semântica de texture stages:
  - desativação em cadeia quando stage0 está disabled,
  - fallback de `D3DTA_TEXTURE` para `D3DTA_DIFFUSE` quando não há texture bound,
  - normalização de alpha-op em casos indefinidos.

Hipóteses testadas e descartadas (piora visual):

- desabilitar fog global no startup;
- desabilitar blend global no startup;
- swap global de canais R/B para formatos `A8R8G8B8`/`X8R8G8B8`.

Conclusão da rodada:

- houve melhora parcial (menos overlays diagonais simultâneos),
- porém a tela inicial ainda não está equivalente ao desktop;
- próximo foco continua no path FVF322/FVF594 indexado com semântica legacy DX9.
