# OpenWyd

> Portuguese below | Português abaixo

## English

OpenWyd is an ongoing port of the original WYD client runtime from its
Windows/DirectX environment to WebAssembly and WebGL. The purpose is not to
rebuild the game as a browser imitation: it is to retain the original C++
client code, data formats, scene behavior, and render pipeline while replacing
only the platform layer required to run it in modern environments.

### Try the current build

The public build starts directly in the real local Field scene:

<https://alanpetry.github.io/OpenWyd/>

The first load is intentionally large because this first public preview uses
the complete currently required client data bundle. It is a graphics/runtime
preview only: it does not connect to a game server and must not be used for
accounts, passwords, or sensitive data.

### Current progress

- The original C++ client is compiled to WASM with an Emscripten compatibility
  layer for the required Win32, DirectInput, Direct3D 9, and D3DX surfaces.
- The Direct3D fixed-function bridge renders through WebGL, including the
  scene paths needed by terrain, water, sky, fog, static objects, characters,
  textures, lighting, and a growing set of effects.
- The Field scene runs with real client assets and local movement. The startup,
  select-server, select-character, and Field states are available for runtime
  investigation.
- Browser mouse input flows into the original event and DirectInput paths.
- Automated visual/runtime smoke tests cover startup, game states, input,
  WebGL errors, and Field performance probes.

This is active preservation and porting work. A visible scene is not yet a
claim of complete visual or behavioral parity with the Windows client.

### What remains

- Finish parity of all 3D rendering paths, UI artwork/layout, fonts, keyboard,
  audio, effects, and every official game state.
- Build a Windows comparison environment that runs the original DirectX client
  beside the Web/WASM build. A debug coordinator will capture and compare
  runtime state and rendering tick by tick, making behavioral and visual
  regressions materially easier to identify during the port.
- Complete browser-compatible networking and validate the original login and
  multiplayer flows against a compatible server.
- Audit and port the original `TMSrv` and `DBSrv` sources when they are added
  to this repository. The current checkout contains server data only, not
  those server source bases.
- Make the server stack cross-platform, migrate Microsoft SQL Server data to
  PostgreSQL, and provide Docker images and Docker Compose for client
  services, game services, and database deployment.
- Ship a desktop distribution based on Electron or a similar host. It will
  keep the WASM client and web-delivered assets, while adding a native bridge
  for background work and capabilities that browsers intentionally do not
  provide.

### Local development

The client asset tree is required for the current full-data build. With the
Emscripten SDK active:

```bash
bash webclient/client-wasm/tools/run_tmproject_wasm_objects.sh
bash webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh
```

Then serve the repository over HTTP and open
`webclient/client-wasm/build/link/startup_harness.html?state=0`.

### Contributing

Keep changes faithful to the original client. Fix the platform bridge,
compatibility layer, asset loading, or original pipeline; do not replace
official scenes, assets, positions, text, or gameplay with manual
approximations. Validate visual changes against the original client whenever a
reference is available, and keep `glErrorTotal` at zero.

### Rights and notice

OpenWyd is an independent preservation and interoperability project. WYD,
With Your Destiny, the original client code, names, artwork, audio, and other
associated assets remain the property of their respective rightsholders.
Nothing here claims affiliation with, endorsement by, or ownership of those
rightsholders. The repository includes historical client material for
research, compatibility, and preservation; contributors are responsible for
observing applicable rights and distribution rules.

The OpenWyd-authored source changes are intended to be available under GPL-3.0
or later. That grant does not grant rights over third-party game assets.

---

## Português

OpenWyd é uma migração em andamento do runtime do cliente original de WYD do
ambiente Windows/DirectX para WebAssembly e WebGL. O objetivo não é recriar o
jogo como uma imitação no navegador: é preservar o código C++ original, os
formatos de dados, o comportamento das cenas e o pipeline de renderização,
trocando apenas a camada de plataforma necessária para executar em ambientes
modernos.

### Teste a versão atual

A versão pública inicia diretamente no Field local real:

<https://alanpetry.github.io/OpenWyd/>

O primeiro carregamento é propositalmente grande porque esta primeira prévia
pública usa o bundle completo de dados do cliente requerido hoje. Ela é apenas
uma demonstração gráfica/de runtime: não conecta a um servidor de jogo e não
deve ser usada com contas, senhas ou dados sensíveis.

### O que já foi feito

- O cliente C++ original compila para WASM com uma camada de compatibilidade
  para as superfícies Win32, DirectInput, Direct3D 9 e D3DX utilizadas.
- A bridge de funções fixas do Direct3D renderiza em WebGL, incluindo os
  caminhos de terreno, água, céu, névoa, objetos estáticos, personagens,
  texturas, iluminação e uma quantidade crescente de efeitos.
- O Field executa com assets reais do cliente e movimento local. Os estados de
  startup, seleção de servidor, seleção de personagem e Field estão
  disponíveis para investigação do runtime.
- O mouse do navegador alimenta os caminhos originais de eventos e DirectInput.
- Smokes automatizados cobrem startup, game states, input, erros WebGL e probes
  de performance do Field.

Este é um trabalho ativo de preservação e portabilidade. Uma cena visível não
significa que a paridade visual ou comportamental com o cliente Windows já
esteja completa.

### O que falta

- Fechar a paridade de todos os caminhos 3D, arte/layout de interface, fontes,
  teclado, áudio, efeitos e todos os states oficiais.
- Implementar um ambiente de comparação no Windows que execute, lado a lado, o
  cliente original em DirectX e a versão Web/WASM. Um coordenador de debug
  deverá capturar e comparar estado de runtime e renderização tick a tick,
  acelerando a identificação de divergências visuais e comportamentais durante
  a migração.
- Concluir a rede compatível com navegador e validar login e multiplayer do
  fluxo original contra um servidor compatível.
- Auditar e portar os fontes originais de `TMSrv` e `DBSrv` quando forem
  adicionados ao repositório. O checkout atual contém apenas dados do servidor,
  não essas bases de código.
- Tornar a pilha de servidor multiplataforma, migrar os dados de Microsoft SQL
  Server para PostgreSQL e fornecer imagens Docker/Docker Compose para cliente,
  serviços de jogo e banco de dados.
- Distribuir uma versão desktop com Electron ou equivalente. Ela manterá o
  cliente WASM e os assets entregues pela web, adicionando uma ponte nativa
  para tarefas em segundo plano e capacidades que o navegador não oferece.

### Desenvolvimento local

A árvore de assets do cliente é necessária para o build completo atual. Com o
Emscripten ativo:

```bash
bash webclient/client-wasm/tools/run_tmproject_wasm_objects.sh
bash webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh
```

Depois sirva o repositório por HTTP e abra
`webclient/client-wasm/build/link/startup_harness.html?state=0`.

### Como contribuir

Mantenha as mudanças fiéis ao cliente original. Corrija a bridge de plataforma,
a camada de compatibilidade, o carregamento de assets ou o pipeline original;
não substitua cenas, assets, posições, textos ou gameplay oficiais por
aproximações manuais. Valide alterações visuais contra o cliente original
sempre que houver referência e mantenha `glErrorTotal` em zero.

### Direitos e aviso

OpenWyd é um projeto independente de preservação e interoperabilidade. WYD,
With Your Destiny, o código original do cliente, nomes, arte, áudio e demais
assets associados pertencem aos respectivos titulares. Nada neste projeto
reivindica afiliação, endosso ou propriedade sobre esses titulares. O
repositório inclui material histórico do cliente para pesquisa,
compatibilidade e preservação; cada contribuinte é responsável por observar os
direitos e regras de distribuição aplicáveis.

As alterações de código escritas para OpenWyd pretendem estar disponíveis sob
GPL-3.0 ou posterior. Essa concessão não concede direitos sobre assets de
terceiros.
