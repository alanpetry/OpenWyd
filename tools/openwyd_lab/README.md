# OpenWyd Lab

O Lab carrega diretamente uma scene real nos clientes Windows e WASM, injeta
os mesmos pacotes oficiais e captura o mesmo frame lógico sem login, servidor,
mouse ou automação de interface.

```powershell
.\tools\lab.ps1 build
.\tools\lab.ps1 start
.\tools\lab.ps1 show field_attack --frame 8
.\tools\lab.ps1 show field_move --frame 21
.\tools\lab.ps1 hot-swap-test --count 10
.\tools\lab.ps1 stop
```

Os clientes permanecem vivos entre comandos. Alterar um JSON em `scenarios`
não recompila código. O comando `build` para os runtimes automaticamente antes
do linker e preserva os objetos incrementais.

Cada execução salva em `artifacts/openwyd_lab/<scenario>/<run>/`:

- `native.png`: backbuffer DirectX antes de `Present`;
- `wasm.png`: canvas WebGL do mesmo frame lógico;
- `diff.png`: diferença absoluta amplificada quatro vezes;
- `scenario.json` e `scenario.owlb`;
- `manifest.json`: estado dos dois clientes e métricas de pixels.

O `pixel_check` usa dois limites simples: RMS máximo 12 e no máximo 3,5% dos
pixels com diferença superior a 32 níveis em qualquer canal. `show` marca
`PASS` ou `REVIEW` sem bloquear a captura. `hot-swap-test` exige `PASS`, pois é
o teste de regressão.

O cenário `field_move` usa o pacote oficial `MSG_Action` e rota ASCII
(`"route": "666666"`). O frame 21 cobre especificamente o blend
corrida-para-parado que já expôs uma divergência na conversão
matriz/quaternion da bridge D3DX.
