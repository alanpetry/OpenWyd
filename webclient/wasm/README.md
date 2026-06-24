# WYD C++ WASM Core (Início)

Este módulo inicia a virada de arquitetura para **reuso do client C++ em WASM**.

Escopo inicial entregue:

- parser C++ de header de mesh `.msh` (`wyd_parse_msh_header`);
- parser C++ de overview de terreno `.trn` (`wyd_parse_trn_overview`);
- build com Emscripten gerando `webclient/app/wasm/wyd_core.js` + `wyd_core.wasm`;
- bridge JS para consumo no front-end (`app/src/wasmBridge.js`).

## Build

No root do pacote:

```bash
./webclient/tools/build_wasm.sh
```

## Próximo passo planejado

- portar para esse core WASM os parsers completos hoje em JS (`msh/msa/ani/bon/trn/dat`);
- mover a seleção de animação (`BoneAni4 + ValidIndex`) para C++/WASM;
- iniciar camada de adaptação do render legado (DX9 state -> backend web).
