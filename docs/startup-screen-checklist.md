# Startup Screen Checklist (WYD C++ WASM)

Last update: 2026-06-04
Owner: wasm render bridge (`webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp`)

Status legend:
- [x] done
- [~] in progress
- [ ] todo

## A. Reproducibility and instrumentation

- [x] State 7 (`TM_SELECTSERVER_STATE`) boots/ticks/shutdowns consistently in browser harness.
- [x] Startup smoke captures screenshot + JSON metrics per run.
- [x] FVF usage histogram exported (`fvfTop`, depth-write enabled/disabled top).
- [x] Shader runtime telemetry (create/bind/use/skip) exported for startup classification.
- [x] Shader file-open telemetry (`shader/*.bin`) exported.
- [x] WebGL runtime error telemetry exported (`glErrorTotal`, `glErrorDrawCalls`, `glErrorLast`).
- [x] Asset open + fallback telemetry exported (`assetFileOpen*`, `assetPathFallback*`).

## B. Initial-screen visual parity (critical path)

- [~] Remove catastrophic geometry artifacts/flicker in startup scene.
- [~] Isolate high-impact FVF path (`FVF 322`) and keep diagnosis reproducible (`--debug-skip-fvf`).
- [~] Keep depth pipeline deterministic while preserving readability (temporary guard active for FVF 322/594).
- [x] Enable guarded clip-W reject for unstable `FVF 322/594` startup families (default on, debug-overridable).
- [ ] Converge `FVF 322` indexed path to desktop-equivalent output (no diagonal overlays / no random solid polygons).
- [ ] Converge `FVF 594` + `FVF 322` combined frame with stable camera motion and no transient mesh popping.
- [~] Validate that startup logo + sky + ground + scene objects are simultaneously visible and stable.
- [ ] Replace the blanket `FVF 594` depth guard with a proven per-draw signature (not a family-wide relaxation).

## C. Fixed-function DX9 semantics still required for startup

- [x] Clip-space handling moved from pre-divided NDC to preserved clip-W path in CPU decode.
- [x] Diffuse-alpha fallback narrowed to alpha-test-active cases only.
- [~] Stage-chain semantics tightened (`COLOROP DISABLE` now disables higher stage in bridge path).
- [ ] Audit `DrawIndexedPrimitive` compatibility details for dynamic VB/IB workloads (base/index/offset behavior under real startup content).
- [ ] Audit texture-stage combine semantics on multi-pass startup draws (`COLOROP/ALPHAOP`, args, stage1 interplay).
- [ ] Confirm cull/depth/alpha ordering against original DX9 behavior in problematic draw groups.

## D. Verification gates before leaving startup phase

- [ ] Visual gate V1: no full-screen/diagonal corruption on 3 consecutive smoke runs.
- [ ] Visual gate V2: no noticeable object flicker during camera motion in startup loop.
- [ ] Telemetry gate T1: startup frame with `glErrorTotal == 0` on repeated runs.
- [ ] Telemetry gate T2: no need for `--debug-skip-fvf` to maintain readable initial scene.
- [ ] Telemetry gate T3: documented draw-family map for top startup draw groups (FVF + state profile).

## E. Next execution checklist (run every iteration)

- [ ] Re-link startup wasm artifact.
- [ ] Run `playwright_startup_smoke.mjs` baseline (`state 7`, 60 ticks) and save screenshot.
- [ ] Run at least one targeted diagnostic variant (e.g., `--debug-skip-fvf 322` or another active hypothesis).
- [ ] Update this checklist statuses based on measured deltas.
- [ ] Append findings to `docs/porting-notes.md` and `docs/wasm-migration.md`.

## F. Execution log (2026-05-29)

- [x] Re-link startup wasm artifact.
- [x] Baseline smoke executed with screenshot (`startup-canvas-state7-stagefix.png`).
- [x] Targeted diagnostic variants executed:
  - no fog (`debugFlags=32`) -> visual piora/sem ganho.
  - no blend (`debugFlags=64`) -> piora forte (quads pretos), descartado para uso normal.
  - texture R/B swap (`debugFlags=128`) -> cores erradas (logo azul), descartado para uso normal.
- [x] Ajustes mantidos no código:
  - blend state aplicado por draw para reduzir vazamento de estado entre passes;
  - semântica de stage chain ajustada (`stage0 disable` desativa stage1 no path do bridge);
  - fallback de argumento de textura sem texture bound ajustado para `DIFFUSE`.
- [x] Progresso visual validado:
  - redução de overlays diagonais múltiplos;
  - tela ainda não equivalente ao client desktop (bloqueio principal segue no path FVF322).
- [x] Documentação de progresso atualizada.

## G. Execution log (2026-05-29, rodada culling/fvf322)

- [x] Re-link startup wasm artifact.
- [x] Baseline/variant smoke executados com 600 ticks:
  - `startup-smoke-state7-frontfacefix-baseline.json`
  - `startup-smoke-state7-frontfacefix-nocull.json`
- [x] Culling parity instrumentation adicionada:
  - exports de `clip_w_reject_*` conectados no linker/smoke;
  - novos contadores de cull: `cullMirrorWorldviewDraws`, `cullFrontfaceFlippedDraws`.
- [x] Hipótese de espelhamento testada e classificada:
  - `cullMirrorWorldviewDraws` > 0, porém `cullFrontfaceFlippedDraws = 0` nos runs testados;
  - conclusão: os draws espelhados observados nesta cena estão majoritariamente em `D3DCULL_NONE`, então o flip de front-face não foi o fator dominante do artefato.
- [x] Tentativa estrutural de reduzir guard de depth para passes translúcidos feita e revertida:
  - versão “blend/alpha-only” reduziu fortemente `depthWriteDisabled`, mas piorou visualmente (frame tomado por polígonos vermelhos);
  - guard voltou ao comportamento anterior para preservar estabilidade.
- [x] Nova telemetria focada em `FVF 322` adicionada:
  - `fvf322ScreenlikeVertices` e `fvf322ScreenlikeDraws`.
- [x] Resultado da rodada:
  - `--debug-skip-fvf 322` continua sendo o maior sinal de isolamento correto do problema visual;
  - bloqueio principal permanece na família `FVF 322` (combinada com estado legado), ainda sem convergência visual para desktop.

## H. Execution log (2026-05-30, rodada screen-space compat)

- [x] Re-link startup wasm artifact.
- [x] Servidor local mantido acessível em `127.0.0.1:8877`.
- [x] Smokes executados após a mudança:
  - `startup-smoke-state7-screencompat-60ticks.json`
  - `startup-smoke-state7-screencompat-600ticks.json`
- [x] Ajuste aplicado no bridge:
  - `FVF 322` com assinatura de quad em viewport (`screen_xy` + `z in [0,1]`) agora usa replay screen-space no decode;
  - lighting/fog legacy deixam de ser aplicados nesse subpath heurístico.
- [x] Resultado medido:
  - 60 ticks: `drawCalls` caiu de `35461` para `28317`; `fvf322DrawPrimitiveUp` caiu de `7365` para `463`; `fvf322ScreenlikeDraws` caiu de `12993` para `5808`.
  - 60 ticks visualmente: houve redução parcial de overlays `FVF 322`, mas a cena segue dominada por planos diagonais e fundo incorreto.
  - 600 ticks: a câmera continua degenerando para geometria de terreno/polígonos sólidos (`startup-canvas-state7-screencompat-600ticks.png`), sem atingir paridade.
- [x] Classificação da rodada:
  - progresso parcial no bootstrap curto;
  - regressão/sem ganho suficiente no loop longo;
  - mudança mantida apenas como passo de isolamento, não como convergência final.

## I. Execution log (2026-06-01, rodada clip-w/default parity)

- [x] Re-link startup wasm artifact.
- [x] Baseline smoke executado sem flags extras (`state 7`, 600 ticks):
  - `startup-smoke-state7-postpatch-baseline.json`
  - `startup-canvas-state7-postpatch-baseline-20260601-091257.png`
- [x] Correções estruturais no bridge:
  - `DrawPrimitiveUP/DrawIndexedPrimitiveUP` agora aplicam side-effects DX9 de pós-chamada (reset de stream0 e índice no path indexed UP);
  - assinatura "screen-like" em `FVF 322` voltou para telemetria-only (sem replay screen-space forçado no path padrão);
  - clip reject por `clip.w` virou **padrão** para famílias críticas (`FVF 322/594`), com opt-out por debug flag.
- [x] Resultado visual:
  - regressão clássica de "rabiscos/vermelhão" foi significativamente reduzida no baseline padrão;
  - validação A/B: ao desativar auto clip reject (`debugFlags=16384`), os artefatos voltam imediatamente.
- [x] Telemetria principal da baseline nova:
  - `clipWRejectDraws=60081`
  - `fvf322AutoClipWDraws=168853`
  - `fvf322AutoClipWRejectDraws=60081`
  - `glErrorTotal=0`.

## J. Execution log (2026-06-01, rodada clip-triangle fallback)

- [x] Re-link startup wasm artifact.
- [x] Servidor local confirmado em `127.0.0.1:8877`.
- [x] Ajuste testado no bridge:
  - auto clip-w para `FVF 322/594` deixou de apenas descartar triângulos com `clip.w` instável;
  - o path agora usa `ClipTriangleToClipVolume` para gerar geometria recortada antes do upload WebGL.
- [x] Smoke collector endurecido para a rodada:
  - leitura de `assetFileOpenFailSamples` no `playwright_startup_smoke.mjs` agora tolera ausência de helpers Emscripten exportados (`UTF8ToString` / `HEAPU8`) sem abortar a coleta.
- [x] Smoke curto executado com screenshot:
  - `startup-canvas-state7-cliptri-60ticks-20260601-113723.png`
- [x] Resultado medido (60 ticks):
  - `drawCalls=38098`
  - `primitiveCount=2963891`
  - `clipWRejectDraws=20337`
  - `fvf322AutoClipWRejectDraws=14323`
  - `fvf594AutoClipWRejectDraws=6014`
  - `glErrorTotal=0`
- [x] Classificação visual:
  - regressão clara no bootstrap curto; grandes planos vermelho-escuros passam a cobrir a metade superior da cena;
  - o terreno continua visível, mas a composição ficou pior do que a baseline pós-patch (`startup-canvas-state7-postpatch-baseline-20260601-091257.png`).
- [x] Classificação operacional:
  - a tentativa de loop longo (`600` ticks) ficou lenta demais para uso como default e não produziu uma nova baseline útil nesta rodada;
  - conclusão: o fallback de clip-triangle não deve permanecer como caminho padrão sem uma heurística mais estreita por família/estado.
  - o código experimental foi preservado apenas como debug opt-in (`kDebugClipTriangleRepair`), e o artefato servido voltou ao path padrão anterior.

## K. Execution log (2026-06-01, rodada asset-parity/fallback)

- [x] Re-link startup wasm artifact.
- [x] Manifest de preload ampliado para UI da tela inicial:
  - `v769ClientRelease/NUI/*.wyt@/NUI/`.
- [x] Fallback de path aplicado no compat layer para pacote incompleto de meshes `or010[3-6]xx.msh`:
  - `or0103xx -> or0101xx`
  - `or0104xx -> or0102xx`
  - `or0105xx -> or0101xx`
  - `or0106xx -> or0102xx`
- [x] Smoke baseline executado (`state 7`, `120` ticks):
  - `startup-smoke-20260601-115427.json`
  - `startup-canvas-20260601-115427.png`
- [x] Resultado medido:
  - `assetFileOpenFail`: `303 -> 0`
  - `assetFileOpenFailTexture`: `299 -> 0`
  - `consoleErrorCount`: `0`
  - `glErrorTotal`: `0`
  - `assetPathFallbackHits`: `4` (samples de `or010301/401/501/601.msh`)
- [~] Estado visual:
  - houve avanço concreto (painel de seleção e terreno voltaram);
  - persiste artefato vermelho intermitente no lado direito (ainda bloqueando paridade total).

## L. Execution log (2026-06-02, rodada debug-flag parity)

- [x] Re-link startup wasm artifact.
- [x] Servidor local confirmado em `127.0.0.1:8877`.
- [x] Correção aplicada no bridge:
  - `kDebugClipTriangleRepair` foi movido para bit dedicado (`1u << 16`);
  - o bit `1u << 15` volta a significar apenas `kDebugDisableFvf322ScreenlikeReplay`.
- [x] Problema reproduzido e classificado nos relatórios mais recentes:
  - `startup-smoke-20260602-043747-noscreenfix.json` usava `debugFlags=32768`;
  - por colisão de bits, esse run desligava o replay `FVF 322` ao mesmo tempo em que tentava habilitar clip repair, invalidando a comparação diagnóstica.
- [x] Smokes executados após a correção:
  - baseline corrigida: `startup-smoke-20260602-flagfix-baseline.json`
  - clip-repair isolado: `startup-smoke-20260602-flagfix-cliprepair.json`
- [x] Resultado medido:
  - baseline (`debugFlags=0`): `drawCalls=61485`, `primitiveCount=5841500`, `fvf322ScreenlikeReplayDraws=4325`, `clipWRejectDraws=9960`, `glErrorTotal=0`.
  - clip repair isolado (`debugFlags=65536`): `drawCalls=65176`, `primitiveCount=4183391`, `fvf322ScreenlikeReplayDraws=9815`, `clipWRejectDraws=32074`, `glErrorTotal=0`.
- [x] Classificação visual:
  - contra o run inválido `noscreenfix`, a baseline corrigida representa **progresso**: remove os grandes planos diagonais preto/vermelhos e recupera uma composição mais legível de logo + terreno;
  - o clip repair isolado permanece **regressão**: reintroduz planos diagonais largos e um vale/polígono marrom cobrindo o centro da cena.
- [x] Próximos passos:
  - manter `kDebugClipTriangleRepair` apenas como diagnóstico opt-in;
  - concentrar a próxima rodada no path padrão (`debugFlags=0`) para entender por que o fundo ainda cola em marrom sólido mesmo com replay `FVF 322` ativo;
  - correlacionar `FVF 322` replay draws com estado de blend/cull/depth antes de qualquer nova heurística de clip.

## M. Execution log (2026-06-03, rodada depth-guard narrowing em `FVF 594`)

- [x] Re-link startup wasm artifact.
- [x] Servidor local mantido acessível em `127.0.0.1:8877` via static server na raiz do workspace.
- [x] Baseline curto revalidado no path padrão (`debugFlags=0`, `120` ticks):
  - `startup-smoke-20260603-postrevert-baseline-t120.json`
  - `startup-canvas-20260603-postrevert-baseline-t120.png`
- [x] Diagnósticos comparativos executados antes da mudança:
  - baseline atual: `diag-20260603-baseline-t120.{json,png}`
  - `--debug-flags 512` (disable stage1): `diag-20260603-nostage1-t120.{json,png}`
  - `--debug-flags 8192` (guard `594` only): `diag-20260603-guard594only-t120.{json,png}`
  - `--debug-flags 4096` (guard `322` only): `diag-20260603-guard322only-t120.{json,png}`
- [x] Conclusão diagnóstica da rodada:
  - desligar stage1 mudou muito pouco a composição visual, então stage-chain/UV1 não é o principal bloqueio desta tela;
  - remover o guard de depth de `FVF 322` ou de `FVF 594` piora claramente a composição, então as duas famílias ainda precisam de proteção no path padrão atual.
- [x] Ajuste experimental testado no bridge:
  - o guard de depth de `FVF 594` foi estreitado temporariamente para draws considerados “order-sensitive” (`blend`, `alpha test` ou `cull none`).
- [x] Resultado medido do experimento estreito:
  - run experimental: `startup-smoke-20260603-fvf594-depthnarrow-t120.json`
  - screenshot experimental: `startup-canvas-20260603-fvf594-depthnarrow-t120.png`
  - baseline restaurada (`postrevert`): `drawCalls=61205`, `primitiveCount=5833480`, `depthWriteDisabledDraws=60845`, `depthWriteGuardForcedDraws=54657`, `glErrorTotal=0`.
  - experimento estreito: `drawCalls=66513`, `primitiveCount=5837623`, `depthWriteDisabledDraws=25554`, `depthWriteGuardForcedDraws=14878`, `glErrorTotal=0`.
- [x] Classificação visual:
  - **regressão**: ao reabilitar depth write para quase todo `FVF 594`, a cena volta a exibir fragmentos grandes vermelho/preto de mesh no lado direito e perde a leitura mais limpa do terreno/painéis.
- [x] Decisão operacional:
  - a mudança experimental foi revertida no código antes de encerrar a rodada;
  - o artefato servido em `:8877` voltou ao baseline legível do path padrão.
- [x] Próximos passos:
  - instrumentar assinatura por draw dentro de `FVF 594` (estado de blend/alpha/cull + textura/material) antes de qualquer nova flexibilização do guard;
  - comparar explicitamente os subconjuntos `FVF 594` que permanecem visíveis na baseline contra os que reaparecem como fragmentos vermelho/preto quando depth write é liberado.

## N. Execution log (2026-06-04, rodada `FVF 322` near-screen replay)

- [x] Re-link startup wasm artifact.
- [x] Servidor local mantido acessível em `127.0.0.1:8877`.
- [x] Ajuste aplicado no bridge:
  - replay screen-space de `FVF 322` deixou de exigir correspondência estrita em **todos** os vértices;
  - draws agora entram no replay quando são majoritariamente screen-space (`>= 75%` dos vértices estritos) e todos os vértices ficam próximos do viewport/faixa de depth de startup.
- [x] Smoke baseline executado no path padrão (`debugFlags=0`, `120` ticks):
  - `startup-smoke-20260604-fvf322-nearscreen-t120.json`
  - `startup-canvas-20260604-fvf322-nearscreen-t120.png`
- [x] Resultado medido vs baseline anterior `startup-smoke-20260603-postrevert-baseline-t120.json`:
  - `drawCalls`: `61205 -> 63456`
  - `primitiveCount`: `5833480 -> 5866411`
  - `fvf322ScreenlikeDraws`: `14938 -> 15733`
  - `fvf322ScreenlikeReplayDraws`: `4090 -> 4769`
  - `blendEnabledDraws`: `6809 -> 8902`
  - `glErrorTotal`: `0 -> 0`
- [x] Classificação visual:
  - **progresso parcial**: o fundo voltou a mostrar céu azul enquanto logo, terreno e painéis permanecem legíveis;
  - bloqueio remanescente: fragmentos vermelho/preto de mesh continuam no lado esquerdo, então a família `FVF 322` ainda não convergiu totalmente.
- [x] Próximo passo:
  - estreitar o replay `FVF 322` por assinatura de draw/estado (ex.: blend, cull, alpha/depth) para separar quads de UI near-screen dos meshes 3D que ainda vazam como fragmentos.

## O. Execution log (2026-06-05, rodada `FVF 322` replay narrowing audit)

- [x] Re-link startup wasm artifact.
- [x] Servidor local mantido acessível em `127.0.0.1:8877`.
- [x] Experimentos aplicados e medidos no bridge:
  - variação `overlaygate`: fallback near-screen de `FVF 322` passou a exigir assinatura “overlay-like” (`cull none` + `blend/alpha`);
  - variação `smallbatch`: fallback near-screen de `FVF 322` ficou restrito a batches pequenos (`vertex_count <= 8`);
  - ambas as variações foram revertidas após validação porque pioraram a composição.
- [x] Smokes executados nesta rodada (`state 7`, `120` ticks):
  - experimento 1: `startup-smoke-20260605-fvf322-overlaygate-t120.json`
  - experimento 2: `startup-smoke-20260605-fvf322-smallbatch-t120.json`
  - reconfirmação pós-revert do artefato servido: `startup-smoke-20260605-fvf322-nearscreen-reconfirm-t120.json`
- [x] Resultado medido vs melhor frame anterior de referência (`startup-smoke-20260604-fvf322-nearscreen-t120.json`):
  - `overlaygate`: `drawCalls 63456 -> 81821`, `fvf322ScreenlikeReplayDraws 4769 -> 26499`, `blendEnabledDraws 8902 -> 26537`, `glErrorTotal 0 -> 0`;
  - `smallbatch`: `drawCalls 63456 -> 81318`, `fvf322ScreenlikeReplayDraws 4769 -> 23321`, `blendEnabledDraws 8902 -> 26262`, `glErrorTotal 0 -> 0`;
  - pós-revert revalidado: `drawCalls 63456 -> 82566`, `fvf322ScreenlikeReplayDraws 4769 -> 28375`, `blendEnabledDraws 8902 -> 27209`, `glErrorTotal 0 -> 0`.
- [x] Classificação visual:
  - `overlaygate`: **regressão forte**; o fundo azul desaparece, o céu volta a cinza/preto e grandes fragmentos vermelho/preto reaparecem na direita;
  - `smallbatch`: **regressão forte**; mantém fundo cinza e amplia ainda mais os fragmentos residuais;
  - pós-revert: o código volta ao heurístico de `2026-06-04`, mas o artefato relinkado de hoje continua visualmente pior que a captura azul de `2026-06-04`, sugerindo drift adicional fora do gate testado.
- [x] Decisão operacional:
  - as duas variantes de narrowing foram removidas do código antes de encerrar a rodada;
  - o artefato servido em `:8877` permanece no path near-screen original, agora revalidado por `startup-smoke-20260605-fvf322-nearscreen-reconfirm-t120.json`.
- [x] Próximos passos:
  - auditar por que o mesmo heurístico near-screen hoje reemite muito mais `FVF 322` (`39637` draws totais, `28375` replayados) do que no snapshot de `2026-06-04`;
  - correlacionar a deriva com ordem/tipo de draw (`DrawPrimitiveUP` saltou para `23409`) antes de mexer de novo no gate `FVF 322`;
  - comparar explicitamente os artefatos `20260604-fvf322-nearscreen-t120` vs `20260605-fvf322-nearscreen-reconfirm-t120` para isolar se o desvio veio de asset order, startup state ou build/link drift.
