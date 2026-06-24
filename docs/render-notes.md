# WYD Web Port - Render Notes

## Estado de origem

- backend original: Direct3D9 + D3DX9
- uso forte de fixed-function (`SetTextureStageState`, `SetFVF`, `SetTransform`, `DrawPrimitiveUP`)
- uso adicional de shaders programáveis (`vs_1_1`, `ps_1_1`) para skin/effects

## Opções avaliadas

### A) Camada de compatibilidade DX9-like sobre WebGL2

Prós:

- reduz retrabalho inicial nos call sites
- acelera primeiros frames com geometrias legadas

Contras:

- risco de abstração pesada e difícil de manter
- não cobre automaticamente shaders bytecode antigos

### B) Reimplementar backend diretamente para WebGL2

Prós:

- arquitetura web-native mais limpa no longo prazo
- controle explícito de pipeline

Contras:

- custo inicial alto
- risco maior para convergir em comportamento legacy

## Referências externas (uso pontual)

- [LostMyCode/d3d9-webgl](https://github.com/LostMyCode/d3d9-webgl): referência prática de camada D3D9 FFP sobre WebGL2 para estudar mapeamento de estado.
- [google/angle](https://github.com/google/angle): referência de tradução de API gráfica (OpenGL ES para backends nativos) e estratégias de compatibilidade.

Diretriz de uso: apenas como referência técnica pontual; o código-fonte principal do port continua sendo o client C++ deste pacote.

## Escolha

**WASM-first + backend web incremental**:

- preservar a lógica C++ existente (inclusive decisões de render/gameplay) compilando para WASM;
- usar JS apenas como camada de host/browser e integração de assets;
- evoluir backend DX9 -> WebGL2 via camada de adaptação, sem duplicar regras centrais do client em JS.

Status de fundação C++ nesta etapa:

- `TMProject` inteiro validado com Emscripten (`109/109` TUs em `-fsyntax-only`);
- `TMProject` inteiro compilado para objetos WASM (`109/109` TUs em `-c`);
- monólito WASM de prova gerado com todos os objetos, para preparar o bootstrap do loop real do client.

## Estratégia de shader

1. Catalogar todos os shaders binários e call sites de uso.
2. Classificar por função: skinning, effects, água, blur, etc.
3. Para cada família:
   - tentar reproduzir comportamento com GLSL novo,
   - manter fallback visual temporário quando necessário,
   - evitar bloqueio por tradução automática total de bytecode.

## Entregas já iniciadas no M1

- validação de assinatura dos shaders reais (`vs_1_1`/`ps_1_1`)
- probe em runtime web carregando `Shader\\skinmesh1.bin`
- renderer WebGL2 inicial com loop, viewport e textura real (`Launcher.bmp`)

## Próximo foco de render (M2)

- converter primeiro formato de textura proprietário (`.wys`/`.wyt`) para RGBA no browser
- adicionar draw path mínimo orientado a sprite/quad com assets de UI
- iniciar mapeamento formal de estados DX9 usados nos primeiros cenários

Atualização:

- path inicial de `WYT` já convertido para RGBA (via parser TGA encapsulado em `WT10`) e integrado ao upload de textura WebGL2.
- `WYS` também está com decode inicial ativo (reconstrução para DDS + upload DXT1/DXT3 via `WEBGL_compressed_texture_s3tc`, com fallback para `WYT` quando a extensão não existe).
- renderer agora possui path de visualização de mesh (`.msh`) com atributos de posição/normal/uv e `drawElements` em WebGL2.
- viewer de mesh recebeu normalização de escala por raio do modelo (`drawScale`) para evitar meshes invisíveis por unidade muito pequena.
- no modo viewer inicial, culling foi desativado para reduzir risco de invisibilidade por winding legacy invertido.
- meshes com `paletteCount/faceInfluence` altos podem depender de skinning real de ossos; esses casos já são detectados no front e marcados como pendentes de M4.
- correção de estabilidade de viewport: desacoplado tamanho visual do canvas e buffer de render para eliminar crescimento progressivo de layout/scroll em loop de resize.
- M4 inicial entregue com skinning CPU no viewer:
  - build de matriz combinada por osso usando hierarquia de `.bon`;
  - aplicação de paleta (`boneMatrix * boneCombined`) por vértice usando pesos/índices do `.msh`;
  - upload de deformação em VBO dinâmico por frame para render WebGL.
- animação agora pode ser selecionada por perfil real do client (`BoneAni4` + `ValidIndex`) com mapeamento `weaponType/motionIndex -> arquivo .ani`.
- front-end passou a usar cache de parse para `ANI/BON`, reduzindo latência em trocas de motion/weapon.
- viewer recebeu proteção de layout para sessões longas (painéis com overflow e body sem crescimento vertical), eliminando regressão de scroll crescente.
- correção estrutural no shader FFP do bridge WASM:
  - `COLOROP` e `ALPHAOP` de cada stage agora leem o mesmo `CURRENT` da stage anterior, em vez de o `ALPHAOP` consumir o resultado recém-atualizado do `COLOROP` da mesma stage;
  - isso alinha a semântica multi-texture do compat layer com o pipeline D3D9 legado e remove uma fonte sistêmica de alpha incorreto em meshes do client real.
- mapeamento validado de layout de vértice legado (RenderDevice):
  - `faceInfluence=1` -> pos + blendIndex(ubyte4) + normal + uv
  - `faceInfluence=2` -> +1 blend weight
  - `faceInfluence=3` -> +2 blend weights
  - `faceInfluence=4` -> +3 blend weights
- M3/M4 integrado com terreno/objetos reais:
  - parse de `TRN` (`FileTileInfo` 12 bytes x 4096) no backend;
  - parse incremental de `DAT` por `ObjectFileItem` (28 bytes + bloco opcional de escala);
  - preview top-down de field no front para inspeção de heights e distribuição de objetos.
- viewer WebGL agora renderiza também:
  - malha de terreno gerada da grade de altura `TRN` (64x64, triangulada em runtime);
  - nuvem de pontos para objetos `DAT` (cor por `objType`);
  - controles de escala/visibilidade para inspeção incremental de cenário.
- shader de terreno evoluído para suportar UV + textura dedicada:
  - textura de minimap real por field (`UI/mXXXX.wyt|wys`) via sampler separado da textura de mesh;
  - blend ajustável entre coloração por altura e minimap para depuração visual.
- render recebeu picking CPU para objetos `DAT`:
  - projeção de pontos de objeto para canvas usando `viewProj` ativo;
  - seleção por raio em pixels com highlight no VBO de cores;
  - metadados do objeto selecionado expostos no painel para inspeção rápida.
- runtime de animação recebeu bootstrap por query string (`entry/weapon/motion/preload/seq`) para reproduzir cenários de perf sem ação manual.
- novo path de objetos estáticos do field com assets reais:
  - ingestão de `MeshList.txt` no backend para resolver `objType` com semântica do client legado;
  - parse de `.msa` no front-end (atributos/índices/vértices) com suporte aos FVF observados no release;
  - draw de objetos por instancing (`drawElementsInstanced`) no WebGL2, incluindo rotação por `angle` e escala do `DAT`;
  - budget configurável de instâncias/tipos para equilibrar fidelidade visual e custo de draw call por mapa.
  - renderer atualizado para usar submeshes reais por atributo (`faceStart/faceCount`) em vez de textura única por mesh;
  - resolução de textura por `textureRef` do `MSA` com fallback por stem do mesh (comportamento compatível com `LoadMsa` legado).
- runtime de câmera evoluiu para modo de gameplay:
  - `follow camera` (terceira pessoa) com parâmetros de distância/altura;
  - fallback `orbit` preservado para inspeção técnica;
  - troca dinâmica de modo sem reinicializar pipeline.
- loop de personagem integrado ao render:
  - transform world (`uModelPos/uModelYaw`) aplicado no mesh principal com escala de cena (`uSceneScale`);
  - alvo da câmera sincronizado com posição do personagem;
  - seleção de motion por perfil com fallback para `IDLE/WALK/RUN`.
- colisão inicial com cenário estático:
  - proxies de bloqueio derivados de objetos `DAT`;
  - raio por `objType` com refinamento por bounds de `MSA`;
  - resolução por push-out em múltiplas iterações para reduzir clipping em malhas estáticas.
- validação de regressão visual/viewport:
  - execução Playwright automatizada com interação de minimapa (click-move + teleport);
  - `scrollHeight` e `bodyScrollHeight` estáveis antes/depois (sem “descida infinita” de tela).
- fundação de parser nativo em WASM já conectada ao render runtime:
  - `webclient/wasm/src/wyd_core.cpp` compilado com Emscripten para browser;
  - bridge `app/src/wasmBridge.js` carregando `app/wasm/wyd_core.{js,wasm}`;
  - parsing de `.msh`/`.trn` validado em runtime com log de telemetria (`[WASM MSH]`, `[WASM TRN]`).

## Atualização de render WASM-first (2026-04-11)

- o link de startup do client C++ (`_wyd_start_client`) passou no modo strict (`0` símbolos indefinidos);
- exports de controle de runtime adicionados (`_wyd_boot_client`, `_wyd_tick_client`, `_wyd_shutdown_client`) para preparar loop cooperativo no browser;
- a camada `win32_emscripten_stubs.cpp` agora inclui base matemática D3DX usada pelo runtime legado:
  - `D3DXMatrix*`, `D3DXVec*`, `D3DXQuaternion*`, `D3DXIntersectTri`, `D3DXVec3Project`;
- `D3DXCreateBuffer` passou a alocar buffer real para suportar leitura de bytecode shader no caminho legado (`RenderDevice`).

Situação atual de render após o bootstrap:

- o client já carrega no ambiente WASM/browser e inicializa runtime;
- `NewApp` já tem `RunTick(MSG*)` separado de `Run()`, e o ciclo cooperativo `boot -> tick -> shutdown` está estável;
- preload mínimo de assets reais no startup (`tmproject_startup.data`) habilita inicialização de base/UI sem falha imediata;
- o caminho de render ainda está em stubs para device/texture/surface/swap, então o loop completo visual ainda não avança como desktop;
- `MeshManager` no tick foi temporariamente adiado no path WASM para manter estabilidade enquanto a trilha de render real é implementada.

Próxima etapa de render para virar de bootstrap para execução real:

1. substituir stubs críticos de `IDirect3D9/IDirect3DDevice9` por backend WebGL2 funcional;
2. implementar caminho mínimo de textura/surface/buffer com semântica DX9 suficiente para `RenderDevice` inicial;
3. remover gate temporário de `MeshManager` no tick e religar carregamento real de mesh;
4. avançar do startup harness para frame loop real com saída visual consistente.

## Atualização - Startup artifact reduction (2026-06-01)

- correção aplicada no bridge WASM para refletir semântica DX9 em draws UP:
  - `DrawPrimitiveUP`: stream 0 é resetado após a chamada;
  - `DrawIndexedPrimitiveUP`: stream 0 e índice são resetados após a chamada.
- `FVF 322` "screen-like signature" foi mantida apenas para telemetria, sem forçar replay em screen-space por padrão.
- clip reject por `clip.w` foi promovido para comportamento padrão nas famílias mais instáveis de startup (`FVF 322/594`), mantendo override por debug flag para comparação A/B.
- validação visual local (Playwright, state 7, 600 ticks):
  - baseline atual removeu a maior parte dos planos diagonais/vermelhos que dominavam a tela;
  - ao desativar auto clip reject, a regressão reaparece, confirmando causalidade.

## Atualização - Asset parity na tela inicial (2026-06-01, 11:54)

- preload do startup foi ampliado com `NUI/*.wyt`, removendo ausência estrutural da UI da seleção de servidor no WASM FS;
- `ResolveCaseInsensitivePath` ganhou fallback explícito para a família ausente `mesh/or010[3-6]xx.msh`, reapontando para variantes existentes no pacote:
  - `or0103xx -> or0101xx`
  - `or0104xx -> or0102xx`
  - `or0105xx -> or0101xx`
  - `or0106xx -> or0102xx`
- nova telemetria de fallback exportada (`assetPathFallbackAttempts/Hits/Samples`) para validar causalidade e evitar hipótese cega.

Resultado da baseline (`startup-smoke-20260601-115427.json`):

- `assetFileOpenFail = 0` (antes: `303`);
- `assetFileOpenFailTexture = 0` (antes: `299`);
- `consoleErrorCount = 0`;
- `glErrorTotal = 0`;
- screenshot com UI de seleção visível: `startup-canvas-20260601-115427.png`.

Conclusão:

- o bloqueio de carga de assets da tela inicial foi eliminado nesta rodada;
- o artefato vermelho residual passa a ser tratado como problema puro de pipeline DX9->WebGL (estado/shader/ordem de draw), não de ausência de arquivo.

## Atualização - Backend WebGL2 mínimo ativo (2026-04-11)

Passo concluído nesta iteração:

- `IDirect3DDevice9` no compat layer passou a delegar para helpers WebGL2 em:
  - `TestCooperativeLevel`, `Reset`, `Present`,
  - `BeginScene`, `EndScene`,
  - `Clear`, `SetViewport`, `SetRenderState`.
- no stub WASM (`win32_emscripten_stubs.cpp`), o estado gráfico mínimo agora cobre:
  - criação de contexto WebGL no canvas principal;
  - clear de cor/profundidade;
  - viewport dinâmico;
  - estados básicos de depth/blend/cull.

Validação prática:

- harness visual com `Module.canvas` ativo;
- smoke Playwright com leitura de pixel no canvas:
  - RGBA centro: `(32, 48, 64, 255)`;
  - `renderVisible=true`;
  - screenshot: `webclient/client-wasm/build/reports/startup-canvas.png`.

Conclusão técnica deste bloco:

- o port WASM saiu de “runtime sem saída visual” para “render visível no navegador”, mantendo a estratégia C++-first.

## Atualização - Compat DX9 ampliada (2026-04-11)

Mudanças de render entregues nesta iteração:

- camada FFP no shim WASM avançou de clear-only para pipeline de draw real:
  - suporte a `SetTransform` (world/view/proj), `SetFVF`, `SetStreamSource`, `SetIndices`;
  - implementação de `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP` e `DrawIndexedPrimitive`;
  - upload dinâmico para WebGL (`ARRAY_BUFFER` / `ELEMENT_ARRAY_BUFFER`) e draw por primitiva.
- recursos básicos de device/texture/surface foram materializados:
  - `CreateVertexBuffer/CreateIndexBuffer` + lock/unlock;
  - `D3DXCreateTexture*` funcional com textura alocada em memória;
  - `GetSurfaceLevel/LockRect/ColorFill` funcionais para fluxo legado de `TextureManager`.
- instrumentação de render no WASM:
  - exports `_wyd_d3d9_draw_calls` e `_wyd_d3d9_primitives` adicionados para telemetria de draw no smoke.

Resultado de runtime no estado atual:

- `MeshManager` agora inicializa no fluxo WASM com assets reais pré-carregados;
- loop cooperativo permanece estável com render visível;
- contadores de draw ainda em `0` no smoke atual porque a transição para `TM_SELECTSERVER_STATE` ainda provoca OOB e foi temporariamente revertida para preservar estabilidade.

## Atualização - Draw real em Select Server (2026-04-11)

- bloqueio de `LoadRC("UI\\SelServerScene2.bin")` no WASM foi removido com `fopen_s` compatível (path case-insensitive + `\\` normalizado);
- preload da cena foi expandido para incluir payload real de `UI/Env/Effect/Mesh` e áudio de bootstrap;
- `NewApp` no path WASM voltou a chamar `SetCurrentState(TM_SELECTSERVER_STATE)` logo após `MeshManager` inicializar;
- novo smoke coleta também `gameState` via export `_wyd_get_game_state`.

Validação de render:

- `drawCalls=131362`;
- `primitiveCount=8876353`;
- `gameState=7` (select server);
- canvas com frame visível (pixel center `34,34,34,255`), screenshot atualizada em
  `webclient/client-wasm/build/reports/startup-canvas.png`.

## Atualização - Textura real no bridge FFP (2026-04-11, noite)

Bloco de render entregue:

- `D3DXCreateTextureFromFileInMemoryEx` deixou de criar placeholder e passou a carregar textura real:
  - decode TGA (incluindo RLE) para payload `WT10` reconstruído;
  - decode DDS DXT1/DXT3 para payload `WS10` reconstruído;
- pack de pixels respeitando `D3DFORMAT` legado (`A8R8G8B8`, `X8R8G8B8`, `A1R5G5B5`, `A4R4G4B4`, `R5G6B5`);
- upload GL por textura com cache e invalidação por `LockRect/UnlockRect/ColorFill`.

FFP shader path incremental:

- vértice FVF agora carrega UV stage0 além de posição/cor;
- shader WebGL aplica textura stage0 com operações básicas de `COLOROP`;
- `SetTextureStageState`/`SetSamplerState` passaram a impactar o draw stage0 no backend.

Telemetria de textura adicionada ao runtime:

- `_wyd_d3d9_tex_decode_success`
- `_wyd_d3d9_tex_decode_fail`
- `_wyd_d3d9_tex_uploads`
- `_wyd_d3d9_textured_draws`

Métrica atual observada no select server:

- `texDecodeSuccess=229`
- `texDecodeFail=0`
- `texUploads=103`
- `texturedDraws=67986`

Estado visual atual:

- o client C++ em WASM já produz geometria/textura no canvas em auto-run;
- ainda há divergência visual relevante em relação ao desktop (tint/combinação de material/effect),
  indicando que a próxima prioridade é fechar semântica DX9 restante:
  - textura stage avançada e alpha op completos;
  - material/luz/fog;
  - caminho de shader legado `vs_1_1/ps_1_1` para skin/effects.

## Atualização - Stage1 e ops DX9 reais (2026-04-11, fim do dia)

Entregue nesta rodada no backend WASM/WebGL2:

- ajuste de enum `D3DTEXTUREOP` para valores corretos de DX9 (corrige interpretação errada de opcodes de textura);
- shader FFP ampliado para dois samplers (`uSampler0/uSampler1`) com pipeline de composição por estágio;
- decode de FVF agora extrai `UV0` e `UV1` quando `TEX2+` está presente;
- estado de material de fallback adicionado (`uTextureFactor`, `uAmbient`, `uLightingEnable`) para manter compatibilidade mínima;
- alpha path revisado (alpha-test ativo + fallback para vertex alpha zerado em malhas legadas).

Instrumentação adicionada ao módulo:

- `_wyd_d3d9_shader_draws_skipped`
- `_wyd_d3d9_fvf_xyzrhw_draws`
- `_wyd_d3d9_fvf_weighted_draws`
- `_wyd_d3d9_fvf_tex2plus_draws`

Diagnóstico atual do render:

- cena continua com artefatos fortes (blocos escuros) apesar de draw/texture ativos;
- `shader_draws_skipped=0` no estado testado (não é gargalo dominante do frame atual);
- `fvf_tex2plus_draws` alto (ordem de milhares) confirma que a semântica de multi-textura/stage1 ainda é o principal gap visual.

Próximo bloco objetivo:

- fechar `D3DTSS_TEXCOORDINDEX` e `D3DTSS_TEXTURETRANSFORMFLAGS` no stage1;
- aplicar geração/transform de coordenadas do stage1 conforme caminho legado do `RenderDevice/CMesh`;
- revalidar frame com screenshot e comparar contra baseline desktop da mesma cena.

## Atualização - Sprite DX9 real + base TCI/transform (2026-04-11, noite)

Bloco estrutural aplicado no bridge C++/WASM (sem voltar para protótipo JS):

- `ID3DXSprite` deixou de ser stub template `E_NOTIMPL` e passou a interface concreta no compat header;
- `DummyD3DXSprite` implementado com `Begin/Draw/End` reais, emitindo quad XYZRHW via `DrawPrimitiveUP` no device DX9 compat;
- `Draw` do sprite agora configura stage0/stage1 explicitamente (desabilita stage1 no pass 2D), evitando vazamento de estado multi-textura da cena 3D para UI.

Também foi entregue um bloco grande no FFP path:

- pipeline de vértice estendido para carregar dados de câmera (`cam pos/normal`) visando `TEXCOORDINDEX` gerado;
- uniforms de stage0/stage1 para `TEXCOORDINDEX` + `TEXTURETRANSFORMFLAGS` completos;
- upload de matriz `D3DTS_TEXTUREn` para shader e suporte em `SetTransform(D3DTS_TEXTURE0..7)`.

Status visual atual:

- runtime C++ continua estável em browser (`boot/tick/shutdown`, estado 7);
- ainda há divergência visual relevante na cena inicial (escura/artefatos), indicando que próximos blocos devem atacar fidelidade de estado de render (especialmente material/light/fog e caminho de shader legado DX9).

## Atualização - Estabilização pós-regressão visual (2026-04-11, noite)

Correções aplicadas no bridge C++/WASM para reduzir flicker/artefatos fortes:

- caminho de vértice não-`XYZRHW` voltou para saída NDC estável no CPU:
  - conversão explícita de clip-space (`x/w`, `y/w`, `z_d3d -> z_gl`) antes do upload;
  - `w` fixado em `1.0` no buffer para evitar interpolação instável por `w` inválido.
- `ApplyViewport` corrigido para semântica D3D9 em WebGL:
  - `viewport.Y` convertido de top-left (D3D) para bottom-left (GL);
  - `MinZ/MaxZ` agora aplicados com `glDepthRangef`.
- `Clear` voltou para semântica DX9 real (sem clear color forçado de debug quando `color==0` e sem clear implícito quando `flags==0`).
- `ID3DXSprite::Draw` ganhou estado de overlay 2D mínimo:
  - desativa `ZENABLE/ZWRITE`,
  - habilita alpha blend (`SRCALPHA/INVSRCALPHA`),
  - desativa `ALPHATEST` e `LIGHTING` no pass de sprite.

Ajuste de toolchain:

- link do startup WASM agora inclui:
  - `-sMIN_WEBGL_VERSION=1`
  - `-sMAX_WEBGL_VERSION=2`
- remove aviso de runtime sobre limitação de WebGL1 e mantém fallback quando necessário.

Validação desta rodada:

- startup link strict segue com `0` símbolos indefinidos;
- runtime mantém `gameState=7` com draw/texture ativos;
- screenshot de runtime em browser voltou a exibir conteúdo real da cena (`logo WYD`) em vez de frame totalmente escuro.

Próximo bloqueio visual ativo:

- apesar da estabilização, a tela inicial ainda está incompleta e com divergência relevante para desktop;
- próximo foco é fidelidade de pipeline legado (material/light/fog + shader bytecode `vs_1_1/ps_1_1`), não expansão de protótipo JS.

Inventário de shader legado entregue para suportar a próxima etapa:

- novo script: `webclient/client-wasm/tools/catalog_dx9_shaders.py`;
- relatórios gerados:
  - `webclient/client-wasm/build/reports/dx9-shader-inventory.json`
  - `webclient/client-wasm/build/reports/dx9-shader-inventory.md`
- diagnóstico atual do pacote real:
  - `18` shaders (`12` VS + `6` PS), todos `shader model 1.1`;
  - path de carregamento confirmado no source (`TMPaths.h`):
    - `shader/skinmesh%d.bin`
    - `shader/vseffect%d.bin`
    - `shader/pseffect%d.bin`

## Atualização - Seleção de personagem e limpeza de draw corrompido (2026-04-11, noite)

Diagnóstico novo obtido em browser real:

- o estado `TM_SELECTCHAR_STATE` já consegue inicializar a cena e desenhar a moldura principal da seleção de personagem com assets reais;
- o triângulo preto gigante sobre a UI não vinha de câmera/projeção fora do lugar, mas de draw corrompido no bridge;
- a hipótese de que o frame atual dependia de `VertexDeclaration/skinning` foi reavaliada para esta cena específica:
  - telemetria mostrou `declDrawCalls=0`;
  - o render dominante nesta tela ainda é FVF legado.

Correção aplicada:

- draws indexados com índice fora de faixa não são mais “colados” no vértice `0`;
- o bridge agora contabiliza esse caso e simplesmente descarta o draw inválido, evitando explosão visual por triângulos degenerados gigantes.

Resultado visível:

- a UI principal de `SelectChar` ficou limpa, sem a faixa/triângulo preto diagonal que atravessava o painel;
- isso confirma que parte da regressão visual vinha de corrupção no path de índices, não apenas de shader/state.

Telemetria nova desta rodada:

- `declDrawCalls=0` no estado reproduzido do harness;
- `invalidDraws=0` e `invalidIndices=0` após a correção, ou seja, o artefato removido não dependia mais de índices ruins persistentes na cena final;
- `fvf3dVerticesInClip` permaneceu alto, indicando:
  - a geometria 3D FVF está sendo transformada para dentro do frustum;
  - o próximo bloqueio visual não é mais projeção/clip, e sim pipeline FVF legado restante:
    - texture stages,
    - alpha test/blend,
    - depth/state ordering.

Conclusão operacional:

- a próxima rodada deve atacar fidelidade de texture-stage/alpha/depth do FVF legado;
- `VertexDeclaration/skinning` continua necessário para personagens completos, mas não é o gargalo primário da cena atualmente reproduzida no harness.

## Atualização - Reavaliação global do startup WASM (2026-04-16)

Resumo da rodada:

- o servidor auxiliar do harness foi estabilizado em sessão persistente para evitar falso negativo de `ERR_CONNECTION_REFUSED` durante a validação do runtime;
- o bridge recebeu um guardrail transitório que força `ZWRITE=0` para `FVF 322/594` no startup path atual;
- foi adicionada telemetria nova no backend C++/WASM:
  - histograma geral por `FVF`;
  - histograma por `FVF` com `depth write on/off`;
  - contadores de `cull none/cw/ccw`;
  - novo debug flag `kDebugDisableCull`.

Achados fechados desta rodada:

- no build atual, `depth write` deixou de ser o principal bloqueio visível da cena de startup;
- a nova telemetria mostrou que os únicos `FVF` ainda escrevendo depth no frame reproduzido são:
  - `324` (sol/UI pretransformado);
  - `578` (`TMSea`);
- `FVF 594` (ground principal) e `FVF 322` já aparecem predominantemente com `depth write off` no frame atual;
- `debugFlags=4` (desligar depth write global) já não abre a cena como abria nas rodadas anteriores;
- `disable cull` global também não destrava a composição.

Mapeamento prático dos FVF no startup:

- `324`: `TMSun` e paths 2D/overlay do `RenderDevice`;
- `578`: `TMSea`;
- `594`: tiles principais do `TMGround`;
- `322`: billboards/efeitos/shade/snow/rain.

Conclusão operacional nova:

- o problema remanescente do startup deixou de ser prioritariamente `depth/cull`;
- o gargalo passou para fidelidade do pipeline FVF legado em mesh/material/alpha:
  - composição por texture stages em `TMMesh`/`TMGround`;
  - alpha/máscaras de billboards e efeitos;
  - possível ausência de parte do cenário por path de mesh/object em vez de simples oclusão.

Evidência visual importante:

- ao forçar opacidade (`debugFlags=9`), aparecem quads pretos grandes onde antes havia invisibilidade;
- isso indica que parte dos objetos/efeitos mascarados existe no frame, mas a semântica de alpha/material ainda diverge do client DX9 original.

## Atualização - Revisão de rota DX9->WebGL e material/luz (2026-04-23)

Revisão externa aplicada como referência pontual:

- `d3d9-webgl` confirma o padrão de implementação que devemos perseguir no compat layer: FVF parsing, texture stages, render states, materiais e luzes em uma camada D3D9 sobre WebGL2;
- a mesma referência explicita uma limitação crítica para o WYD: o wrapper é FFP-only, enquanto nosso client também usa shaders programáveis (`skinmesh`, `vseffect`, `pseffect`);
- `MojoShader` é referência mais adequada para a próxima etapa de shader bytecode porque suporta Shader Model 1..3 e saída GLSL;
- a documentação do Emscripten reforça o uso do subset WebGL-friendly/OpenGL ES 2/3, que é compatível com o caminho atual do bridge.

Mudança implementada no bridge:

- chamadas D3D9 antes ignoradas no header (`SetMaterial/GetMaterial/SetLight/GetLight/LightEnable`) agora preservam estado real;
- `GetMaterial` passa a devolver material válido, evitando restauração incorreta em fluxos como `TMMesh::RenderPick`;
- `D3DRS_COLORVERTEX`, `D3DRS_DIFFUSEMATERIALSOURCE`, `D3DRS_AMBIENTMATERIALSOURCE`, `D3DRS_EMISSIVEMATERIALSOURCE` e `D3DRS_SPECULARMATERIALSOURCE` agora são rastreados;
- o caminho FVF/declaração recebeu iluminação fixed-function CPU-side quando houver luzes habilitadas, mantendo o shader FFP como compositor de texture stages;
- quando `D3DRS_LIGHTING=1` mas nenhuma luz foi instalada no estado atual, o bridge preserva a cor existente para evitar regressão visual do startup.

Resultado medido:

- build/link strict OK (`0` undefined);
- smoke Playwright OK no browser com screenshot `webclient/client-wasm/build/reports/startup-canvas-light-guard.png`;
- telemetria nova confirma que a cena `TM_SELECTSERVER_STATE` atual ainda não chama `SetMaterial/SetLight`, então a divergência visual remanescente permanece em `stage/alpha/fog/object path`, não nessa chamada específica.

Decisão:

- não substituir o bridge por `d3d9-webgl` de forma wholesale neste momento;
- usar o projeto como checklist e referência de semântica FFP;
- manter o nosso bridge porque já está acoplado ao bootstrap real do WYD, ao preload de assets, aos formatos proprietários e ao futuro path de shaders DX9.

## 2026-04-24 - Render-state and fog compatibility pass

Changes:

- Moved legacy fog application out of the GLSL FFP shader and into CPU-side vertex color processing in the D3D9 compatibility bridge.
- This avoids destabilizing the baseline WebGL shader while preserving `D3DRS_FOGENABLE`, `D3DRS_FOGCOLOR`, `D3DRS_FOGVERTEXMODE`, `D3DRS_FOGSTART`, `D3DRS_FOGEND`, `D3DRS_FOGDENSITY`, and `D3DRS_RANGEFOGENABLE` semantics at vertex level.
- Added coverage for all render states currently used by `Projects/TMProject` according to `webclient/client-wasm/build/reports/d3d9-compat-coverage.md`.
- Added telemetry for `fogEnabledDraws` and `wireframeDraws` in the browser smoke harness.

Validation:

- Strict startup link: success, undefined symbols: 0.
- Browser smoke through Playwright: success.
- Latest 60-tick run: `drawCalls=26645`, `shaderDrawsSkipped=0`, `texDecodeFail=0`, `texUploads=43`, `fogEnabledDraws=20495`.

Known render issues still visible:

- Startup scene still has incorrect visual composition: oversized/dark geometry and incomplete original presentation.
- Pixel shader bytecode is still not translated/executed.
- Effect vertex shaders (`vseffect*.bin`) are catalogued but not semantically translated yet.
- Some render states are currently stored/acknowledged rather than fully reproduced. The audit now tracks their presence so semantic gaps can be closed deliberately.

## 2026-05-06 - Runtime telemetry findings

Added telemetry dimensions:

- VS/PS object lifecycle and draw usage counters.
- Shader file open counters by family (`skinmesh`, `vseffect`, `pseffect`).
- Batch state sweep reports with per-state screenshots.

Findings:

- No DX9 shader objects are created or bound in tested startup/select states (`0..7`, `9` smoke variants).
- Shader binaries are not opened in these runs (`shaderFileOpenAttempts=0`).
- Visual artifacts in startup scene therefore remain attributable to fixed-function / mesh / transform / depth-stage behavior, not missing VS/PS bytecode execution for this entry path.
- `TM_DEMO_STATE` (`state 8`) currently trips a wasm runtime signature mismatch during tick and is a hard blocker for progressing demo-state rendering.

## 2026-05-29 - Culling parity + FVF322 diagnostics

Changes:

- Added culling parity counters to startup telemetry:
  - `cullMirrorWorldviewDraws`
  - `cullFrontfaceFlippedDraws`
- Added missing clip-W counters to exported startup symbols:
  - `clip_w_reject_draws`
  - `clip_w_reject_triangles`
  - `clip_w_keep_triangles`
- Added FVF322 classification counters:
  - `fvf322ScreenlikeVertices`
  - `fvf322ScreenlikeDraws`

Validation findings:

- Startup runs do observe mirrored transforms, but most of them are under `D3DCULL_NONE` in this scene profile (`cullFrontfaceFlippedDraws` stayed `0`).
- A depth-guard narrowing attempt (only blend/alpha passes) was tested and reverted because it visually regressed the frame (large red polygon takeover), despite reducing depth-write suppression counts.
- `--debug-skip-fvf 322` remains the strongest isolation signal for startup corruption.

Current blocker snapshot:

- parity is still blocked in the fixed-function startup path, concentrated in `FVF 322` behavior/state interactions;
- cull/front-face parity alone is not sufficient to clear this scene.
