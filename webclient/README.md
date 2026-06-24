# WYD Web Port - Fundação M4 Avançada

Este diretório contém a base inicial do port web com foco em:

- índice e manifest de assets reais de `v769ClientRelease/`
- resolução de caminhos case-insensitive para compatibilidade com o client legado
- direção WASM-first para reaproveitar código C++ do client
- app browser com WebGL2, probes de assets, viewer de mesh real (`.msh`) e skinning incremental (`.ani/.bon`)
- integração do catálogo real de animações do client (`BoneAni4.txt` + `ValidIndex.bin`) para seleção por perfil (`weapon/motion`)
- preview inicial de field real (`Env/*.trn` + `Env/*.dat`) com overlay de objetos
- minimap real por field (`UI/mXXXX.wyt|wys`) aplicado como textura de terreno
- runtime inicial de personagem em campo (WASD, click-to-move no minimapa, colisão de objetos, câmera follow/orbit)
- servidor local mínimo em Flask

## Estrutura

- `tools/generate_asset_manifest.py`: gera os manifests usados pelo web client
- `tools/build_wasm.sh`: compila o core C++ para `app/wasm/` via Emscripten
- `tools/playwright_runtime_probe.mjs`: smoke test E2E do runtime (layout + personagem + minimapa)
- `generated/`: saída dos manifests (`asset-manifest.json`, `asset-index.json`, `bootstrap-manifest.json`)
- `server/app.py`: servidor Flask com endpoints de manifest/resolve/assets
- `app/`: front-end estático (HTML/CSS/JS)
- `wasm/`: código C++ do core de migração para browser

## Rodando localmente

No root do pacote (`TMProject2GlobalClient-main`):

```bash
./webclient/run_dev.sh
```

Abra:

- `http://127.0.0.1:8765/`

Observação:

- na primeira execução, o `run_dev.sh` cria venv e instala dependências; o servidor pode demorar alguns segundos para abrir a porta.

Configuração opcional:

```bash
WYD_WEB_HOST=0.0.0.0 WYD_WEB_PORT=8765 ./webclient/run_dev.sh
```

## Build WASM (C++ reaproveitado)

No root do pacote:

```bash
brew install emscripten
./webclient/tools/build_wasm.sh
```

Artefatos gerados:

- `webclient/app/wasm/wyd_core.js`
- `webclient/app/wasm/wyd_core.wasm`

## Probe E2E (Playwright)

No diretório `webclient/`:

```bash
npm install
npx playwright install chromium
node ./tools/playwright_runtime_probe.mjs
```

Saída:

- JSON com métricas de layout e estado de runtime (`status`, `playerMeta`, `objectMeta`)
- screenshots em `output/playwright/` (`runtime-before-*.png`, `runtime-after-*.png`)

## Endpoints

- `GET /api/manifest`: resumo do inventário de assets
- `GET /api/manifest?full=1`: manifest completo
- `GET /api/manifest/bootstrap`: arquivos de bootstrap resolvidos
- `GET /api/assets/meshes`: catálogo de meshes (`.msh`) com flags `hasWys/hasWyt/hasAni`
- `GET /api/assets/fields`: catálogo de fields (`FieldXXXX`/`Character`) com flags `hasTrn/hasDat/hasMap/hasMinimap`
- `GET /api/assets/field-summary?field=<field>&objectLimit=<n>`: resumo parseado de terreno/objetos (`.trn/.dat`), metadados de `.map` e candidatos de minimap (`UI/mXXXX.wyt|wys`)
- `GET /api/assets/mesh-related?mesh=<mesh-path>`: candidatos relacionados de `ANI/BON/WYS/WYT` para uma mesh
- `GET /api/assets/bone-ani-catalog`: catálogo de animação por classe (`BoneAni4/ValidIndex`)
- `GET /api/assets/bone-ani-catalog?index=<id>&full=1`: detalhe completo da entry de animação
- `GET /api/assets/mesh-animation-profile?mesh=<mesh-path>`: perfil recomendado (`ANI/BON`) e mapa `weapon/motion` para uma mesh
- `GET /api/resolve?path=<path-legado>`: resolve caminho com semântica case-insensitive
- `GET /assets/<path>`: entrega asset real de `v769ClientRelease`
- `GET /healthz` e `GET /api/health`: healthcheck

## Viewer de mesh e skinning (M4 inicial)

Por padrão o app abre um mesh real (`Mesh\\CP010102.msh`).

Você pode trocar o mesh pela query string:

```text
http://127.0.0.1:8765/?mesh=Mesh\\KK01.msh
```

Controles adicionais no painel:

- seleção de `ANI` e `BON` candidatos;
- seleção de perfil `BoneAni` com `Weapon Group` e `Motion`;
- aplicação automática/manual de ANI por perfil (`Aplicar ANI por Perfil`);
- aplicação manual de bundle (`Aplicar Bundle ANI/BON`);
- play/pause de animação;
- controle de frame e velocidade;
- telemetria de viewport (`css/buffer/DPR`).
- seleção de `Field` (`TRN/DAT/MAP`) com preview top-down de altura + pontos de objetos reais.
- overlay 3D de terreno no canvas principal (malha 64x64 do `TRN`) com:
  - toggle `Terreno 3D`;
  - escala de terreno (`Escala Terreno`);
  - textura de minimap real (`Textura Minimap`) com blend ajustável;
  - nuvem de pontos de objetos do `DAT` com toggle e controle de tamanho.
- picking de objetos `DAT` por clique no canvas 3D com painel de inspeção (`objType`, posição, ângulo, `textureSetIndex`, `maskIndex`, escala).
- runtime de personagem:
  - `Modo Personagem` (on/off);
  - `WASD + Shift` para movimento manual;
  - click no minimapa para destino (`click-to-move`);
  - `Shift+clique` no minimapa para teleport;
  - `Spawn no Objeto Selecionado` para reposicionar player em item escolhido do `DAT`;
  - toggles de `Click-to-Move`, `Colisão de Objetos` e `Auto Run`;
  - câmera `Follow`/`Orbit` com sliders de distância e altura.

Comportamentos adicionais desta fase:

- auto-bind de ANI/BON por perfil real quando a mesh suporta skinning;
- cache de parse de `ANI/BON` para trocas rápidas de motion/weapon;
- cache com política LRU para `ANI/BON` em sessões longas;
- layout estabilizado para evitar crescimento de scroll em sessões longas.
- suporte de startup via query string para testes automatizados:
  - `mesh=Mesh\\...`
  - `field=Field0815`
  - `entry=<boneAniIndex>`, `weapon=<id>`, `motion=<id>`
  - `preload=1`
  - `seq=1`, `seqScope=weapon|all`, `seqInterval=<segundos>`
  - `terrain=1|0`, `terrainTexture=1|0`, `terrainTextureBlend=<0..1>`
  - `objPoints=1|0`, `terrainScale=<0.02..0.20>`, `objectPointSize=<1..8>`.
  - `player=1|0`, `walkSpeed=<1.5..8.0>`, `runSpeed=<2.0..12.0>`
  - `clickMove=1|0`, `collision=1|0`, `autoRun=1|0`
  - `camera=follow|orbit`, `cameraDistance=<3.0..15.0>`, `cameraHeight=<1.2..8.0>`.
- bridge WASM ativa no front-end:
  - carrega `app/wasm/wyd_core.{js,wasm}` no bootstrap;
  - já usa parser C++ em runtime para probes de `.msh` e `.trn`;
  - fallback automático para parser JS quando WASM indisponível.

## Observação de escopo

Esta fundação ainda não converte render DX9 completo nem formatos proprietários para WebGL. O foco é garantir base técnica para evolução incremental dos marcos seguintes.
