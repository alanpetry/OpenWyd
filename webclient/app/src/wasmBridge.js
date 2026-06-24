let bridgePromise = null;

class WydWasmBridge {
  constructor(moduleInstance) {
    this.module = moduleInstance;
  }

  _allocAndWriteBytes(arrayBuffer) {
    const bytes = arrayBuffer instanceof Uint8Array ? arrayBuffer : new Uint8Array(arrayBuffer);
    const ptr = this.module._malloc(bytes.length);
    this.module.HEAPU8.set(bytes, ptr);
    return { ptr, length: bytes.length };
  }

  parseMshHeader(arrayBuffer) {
    const { ptr, length } = this._allocAndWriteBytes(arrayBuffer);
    const outPtr = this.module._malloc(32);
    try {
      const ok = this.module._wyd_parse_msh_header(ptr, length, outPtr);
      if (!ok) {
        return null;
      }
      const view = new DataView(this.module.HEAPU8.buffer, outPtr, 32);
      return {
        parentId: view.getUint32(0, true),
        meshId: view.getUint32(4, true),
        fvf: view.getUint32(8, true),
        vertexSize: view.getUint32(12, true),
        faceInfluence: view.getUint32(16, true),
        paletteCount: view.getUint32(20, true),
        vertexCount: view.getUint32(24, true),
        indexCount: view.getUint32(28, true),
      };
    } finally {
      this.module._free(outPtr);
      this.module._free(ptr);
    }
  }

  parseTrnOverview(arrayBuffer) {
    const { ptr, length } = this._allocAndWriteBytes(arrayBuffer);
    const outPtr = this.module._malloc(28);
    try {
      const ok = this.module._wyd_parse_trn_overview(ptr, length, outPtr);
      if (!ok) {
        return null;
      }
      const view = new DataView(this.module.HEAPU8.buffer, outPtr, 28);
      return {
        tileCount: view.getInt32(0, true),
        heightMin: view.getInt32(4, true),
        heightMax: view.getInt32(8, true),
        heightAvg: view.getFloat32(12, true),
        offsetX: view.getInt32(16, true),
        offsetY: view.getInt32(20, true),
        mapNameLen: view.getInt32(24, true),
      };
    } finally {
      this.module._free(outPtr);
      this.module._free(ptr);
    }
  }
}

export async function createWasmBridge() {
  if (bridgePromise) {
    return bridgePromise;
  }

  bridgePromise = (async () => {
    const factoryModule = await import('/app/wasm/wyd_core.js');
    const createModule = factoryModule.default;
    if (typeof createModule !== 'function') {
      throw new Error('Fábrica WASM inválida em /app/wasm/wyd_core.js');
    }

    const instance = await createModule();
    return new WydWasmBridge(instance);
  })();

  try {
    return await bridgePromise;
  } catch (error) {
    bridgePromise = null;
    throw error;
  }
}
