# WYD v769 - Inventário Técnico Inicial

## Escopo analisado

Pacote único utilizado:

- `TMProject2GlobalClient-main.zip`

Árvore usada para o port web:

- source: `Projects/TMProject/`
- release/assets: `v769ClientRelease/`
- solução VS: `TM.sln`

## Estrutura geral do pacote

- source C++ client: `Projects/TMProject` (244 arquivos)
- assets/runtime release: `v769ClientRelease` (8.546 arquivos, 644.295.358 bytes)
- decompilações e materiais auxiliares: `Decompilation/`
- dependências DX legacy: `Dependencies/Directx/`

## Source client (C++/DX9)

Arquivos dominantes em `Projects/TMProject`:

- `109` arquivos `.cpp`
- `128` arquivos `.h`

Pontos de referência críticos:

- render pipeline: `RenderDevice.cpp`, `D3DDevice.cpp`, `CMesh.cpp`, `TextureManager.cpp`
- cenas/UI: `TMScene.cpp`, `TMFieldScene.cpp`, `TMSelectServerScene.cpp`, `SControl.cpp`
- loader de mesh/animação: `MeshManager.cpp`, `TMSkinMesh.cpp`, `TMObjectContainer.cpp`
- rede/socket legado: `CPSock.cpp`

Observações relevantes:

- versão/protocolo em uso no login: `1758` (`TMFieldScene.cpp`, `TMSelectServerScene.cpp`)
- alguns headers no repo estão placeholders (`13 bytes`, apenas `#pragma once`)

## Release real (`v769ClientRelease`) - categorias

Top-level por quantidade de arquivos:

- `Mesh`: 6.524
- `Env`: 537
- `Effect`: 533
- `UI`: 423
- `Sound`: 351
- `Lang`: 65
- `(root)`: 58
- `shader`: 18
- `music`: 13
- `NUI`: 12

Top-level por volume aproximado:

- `Mesh`: 327 MB
- `UI`: 85 MB
- `music`: 79 MB
- `Sound`: 37 MB
- `Adobe AIR`: 36 MB
- `Effect`: 18 MB
- `Env`: 15 MB
- `HeightMap.dat`: 16 MB (arquivo único)

Extensões mais frequentes:

- `wys`: 3006
- `msa`: 1778
- `msh`: 1665
- `ani`: 815
- `wav`: 350
- `wyt`: 263
- `dat`: 134
- `bin`: 105

## Inventário de shaders DX9

Pastas/arquivos:

- `v769ClientRelease/shader/`
  - `skinmesh1..8.bin`
  - `vseffect1..4.bin`
  - `pseffect1..6.bin`

Assinatura dos bytecodes:

- `0xFFFE0101` (`0101feff` little-endian): vertex shader (`vs_1_1`)
- `0xFFFF0101` (`0101ffff` little-endian): pixel shader (`ps_1_1`)

Uso no source:

- paths em `TMPaths.h`:
  - `Shader\skinmesh%d.bin`
  - `Shader\vseffect%d.bin`
  - `Shader\pseffect%d.bin`
- carga em `RenderDevice.cpp` via `_open` + `CreateVertexShader` / `CreatePixelShader`

## Binários de dados e runtime reutilizáveis

Exemplos (root e subpastas) já validados:

- root: `ItemList.bin`, `Itemname.bin`, `SkillData.bin`, `object.bin`, `serverlist.bin`, `Config.bin`, `cdata.bin`, `MountData.bin`
- UI: `UITextureListN.bin`, `strdef.bin`, `SelServerScene2.bin`, `SelCharScene2.bin`, `FieldScene2.bin`
- Mesh/Env/Effect: `MeshTextureList.bin`, `ValidIndex.bin`, `EnvTextureList3.bin`, `EffectTextureList.bin`

## Configs, executáveis e legados Windows

Arquivos principais em release:

- executáveis: `WYD.exe`, `WYDLauncher.exe`, `WydConfig.exe`
- DLLs DX/auxiliares: `d3dx9_42.dll`, `JPGLib.dll`, `JProtectDll.dll`
- config: `Graphics.ini`, `Music.ini`, `config_settings.json`, `settings.txt`

## Dependências fortes de Windows/DirectX

Dependências explícitas no projeto (`TMProject.vcxproj`):

- libs: `d3d9.lib`, `d3dx9.lib`, `dinput8.lib`, `dsound.lib`, `winmm.lib`, `wsock32.lib`, `imm32.lib`, `Wininet.lib`

APIs e subsistemas acoplados:

- janela/mensagem Win32: `CreateWindowEx`, `PeekMessage`, `DispatchMessage`, `WM_*` (`NewApp.cpp`)
- input: DirectInput + IME Win32 (`EventTranslator.cpp`)
- áudio: DirectSound / PlaySound (`dsutil.cpp`, `DirShow.cpp`)
- rede: Winsock (`CPSock.cpp`)
- render: D3D9 + D3DX9 (`pch.h`, `RenderDevice.cpp`, `TextureManager.cpp`)

## Dependência de paths case-insensitive

Source usa caminhos com casing misto/legado:

- `Shader\`, `sound\`, `mesh\`, `Env\`, `UI\`

Release contém diretórios com nomes variados (`shader`, `Sound`, `Mesh`, `Env`, `UI`).

Conclusão: resolver path por índice case-insensitive é obrigatório para web/macOS/browser.

## Partes com maior reaproveitamento imediato

1. Dados e assets de `v769ClientRelease` (sem recriação manual).
2. Semântica de carregamento e formatos, extraída do source:
   - textura custom (`.wys/.wyt`) em `TextureManager.cpp`
   - mesh/animação (`.msh/.bon/.ani`) em `CMesh.cpp` e `MeshManager.cpp`
   - UI binária (`UIBinary.h` + leitores em cenas/UI)
3. Estruturas de domínio/protocolo (`Structures.h`, `Basedef.*`) para futuras etapas.

## Diretriz atual de reaproveitamento (C++ -> WASM)

- Prioridade: portar parsers e regras do client C++ para módulo WASM, evitando manter lógica duplicada em JS.
- JS permanece como camada de integração do browser (IO, canvas, input, debug panel).
- Primeira fundação já ativa:
  - `webclient/wasm/src/wyd_core.cpp` com parser de header `.msh`;
  - parser de overview `.trn` no mesmo core;
  - build via Emscripten para `webclient/app/wasm/wyd_core.{js,wasm}`;
  - consumo no front por `webclient/app/src/wasmBridge.js`.

Evolução da fundação (2026-04-11):

- pipeline de compilação do `TMProject` inteiro em Emscripten operacional;
- relatórios de scan sintático e build de objetos disponíveis em `webclient/client-wasm/build/reports/`;
- artefato monolítico de prova do client C++ em:
  - `webclient/client-wasm/build/monolith/tmproject_monolith.js`
  - `webclient/client-wasm/build/monolith/tmproject_monolith.wasm`

## Riscos técnicos imediatos

- fixed-function DX9 (`SetTextureStageState`, `SetFVF`, `SetTransform`, `DrawPrimitiveUP`) não mapeia 1:1 para WebGL2.
- shaders DX9 (`vs_1_1/ps_1_1`) exigirão tradução/rewrite/substituição gradual.
- formatos proprietários (`.wys/.wyt/.msh`) precisam loaders Web próprios para render real.

## Descoberta já validada para loaders web

- `*.wyt` possui prefixo `WT10` (4 bytes) seguido de payload TGA.
- Com remoção do prefixo, o TGA pode ser decodificado no browser para gerar textura RGBA.
- `*.wys` possui prefixo `WS10` e é reconstruível para DDS no client legado (drop do 1º byte + ajuste de assinatura/fourCC), permitindo caminho inicial de upload DXT em WebGL2.
- `*.msh` inicia com header de 8 `uint32` (id/fvf/stride/palette/vertexCount/indexCount), seguido por blocos de paleta + vértices + índices `uint16`.
- `*.ani` (legado): `uint32 numAniTick` + `uint32 numAniFrame` + payload de matrizes `float4x4`.
  - no source, `numAniTick` é usado como quantidade de ticks/frames da animação;
  - `numAniFrame` é usado como quantidade de ossos/matrizes por tick.
- `*.bon`: pares de 8 bytes (`int32 parentId`, `uint32 boneId`) usados para montar hierarquia de frame/bone.
- vértice skinned (`msh`) usa `blend indices` em `UBYTE4` e `blend weights` variáveis por `faceInfluence` (1..4).
- `Env/*.trn` (terreno) validado via source `TMGround::LoadTileMap`:
  - header: `uint8 nameLen`, `char[nameLen] mapName`, `uint8 posX`, `uint8 posY`;
  - payload: `4096` registros de `12` bytes (`FileTileInfo` alinhado):
    - `int8 cHeight`
    - `uint8 byTileIndex`
    - `uint8 byTileCoord`
    - `uint8 byBackTileIndex`
    - `uint8 byBackTileCoord`
    - `3` bytes de padding
    - `uint32 dwColor`.
- `Env/*.dat` (objetos) validado via source `TMObjectContainer::Load`:
  - base por objeto: `28` bytes (`ObjectFileItem` parcial):
    - `uint32 dwObjType`
    - `float x`, `float y`, `float height`, `float angle`
    - `int32 textureSetIndex`, `int32 maskIndex`
  - tipos de efeito usam bloco extra opcional de `8` bytes (`float scaleH`, `float scaleV`), conforme regras de `dwObjType` no loader legado.
- `Mesh/BoneAni4.txt`: índice de classes de animação (`boneAniIndex`) com formato:
  - `<index> <aniTypeCount> <parts> <basePath>`
  - exemplo: `1 149 8 mesh\\ch02`
- `Mesh/ValidIndex.bin`:
  - tabela `int32` com shape `100 x 186` (`MAX_BONE_ANIMATION_LIST x numAniCut`);
  - para cada linha `index` do `BoneAni4`, os primeiros `aniTypeCount` valores definem os IDs lógicos de animação (`nI`);
  - arquivo final da animação: `<basePath><(nI+1):04d>.ani`;
  - decomposição usada no source:
    - `arrayIndex = nI + 1`
    - `weaponType = arrayIndex / 100 - 1`
    - `motionIndex = arrayIndex % 100 - 1`.
- `Mesh/MeshList.txt`:
  - mapeia índice de mesh comum usado por objetos estáticos (`objType`) para arquivo real (`.msa/.msh`);
  - no fluxo legado (`TMObjectContainer` + `TMObject` + `MeshManager`), `dwObjType` do `DAT` é usado como lookup direto em mesh comum.
- `*.msa` (static mesh):
  - header observado: `uint32 fvf`, `uint32 vertexStride`, `uint32 attributeCount`;
  - sequência: `attributeCount x D3DXATTRIBUTERANGE` (`20 bytes` cada), refs de textura (`11 bytes` por atributo), índice (`uint32 size` + payload), vértice (`uint32 size` + payload);
  - distribuição real no release: predominância de `fvf=274` (stride 32), além de `322` (stride 24) e `18` (stride 24).
  - cada atributo define submesh (`faceStart/faceCount`) e pode usar textura distinta; o client legado resolve `textureRef` para `mesh\\<stem>.wys` / `effect\\<stem>.wys` com fallback para stem do próprio mesh.

### Amostras reais de mesh parseadas

- `Mesh/CP010102.msh`:
  - `fvf=0x1116`, `vertexSize=36`, `faceInfluence=1`, `paletteCount=1`, `vertexCount=734`, `indexCount=3210`
  - bounds aproximados: min `(-1.28, -0.67, -1.69)` max `(1.28, 1.70, 1.28)`
- `Mesh/ag010101.msh`:
  - `fvf=0x111A`, `vertexSize=44`, `faceInfluence=3`, `paletteCount=10`, `vertexCount=722`, `indexCount=2328`
  - bounds aproximados: min `(-0.06, -0.05, -0.01)` max `(0.05, 0.04, 0.01)` (malha em escala muito pequena para viewer sem normalização)

## Preload mínimo para startup WASM

- manifesto atual: `webclient/client-wasm/config/startup-preload-manifest.txt`.
- esse manifesto alimenta o link de startup e gera `webclient/client-wasm/build/link/tmproject_startup.data`.
- objetivo: garantir entrada cooperativa do client C++ no browser com arquivos reais mínimos e bootstrap de mesh/animação.
- atualização desta etapa:
  - suporte a glob no manifesto (`*.bon`, `*.ani`) no linker de startup;
  - preload de `object.bin` + `Mesh/*.bon` + `Mesh/*.ani` habilitado;
  - pacote `tmproject_startup.data` passou para ~63 MB.
- status atual:
  - `_wyd_boot_client` conclui com sucesso;
  - `_wyd_tick_client` executa de forma estável com inicialização real do `MeshManager` no path WASM;
  - transição para `TM_SELECTSERVER_STATE` está ativa e validada;
  - smoke automatizado em `webclient/client-wasm/tools/playwright_startup_smoke.mjs`.

## Atualização do preload startup (2026-04-11)

- manifesto expandido para suporte de cena real:
  - `UI/*.bin|*.txt|*.wyt|*.wys` + subpastas `UI/Image`, `UI/Panel`, `UI/Potal`;
  - `Env/*.trn|*.dat|*.map|*.wys|*.bin`;
  - `Effect/*.wys|*.msa|*.bin`;
  - `Mesh/*.msh|*.msa|*.wys|*.ani|*.bon|*.bin|*.txt|*.dat|*.mshb`;
  - `Sound/*`, `music/*` e metadados raiz (`sound.txt`, `Music.txt`).
- pacote de startup gerado no link:
  - `webclient/client-wasm/build/link/tmproject_startup.data` ~`500 MB`.

Observação crítica de compatibilidade:

- sem interceptar `fopen_s` para resolver caminho Windows-like (`UI\\...`), o client não encontra arquivos mesmo quando presentes no preload;
- o shim agora aplica normalização e busca case-insensitive também em `fopen_s`.
