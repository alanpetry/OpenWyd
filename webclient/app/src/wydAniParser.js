function safeReadFloat32Array(view, offset, count) {
  const out = [];
  for (let i = 0; i < count; i += 1) {
    if (offset + (i * 4) + 4 > view.byteLength) {
      break;
    }
    out.push(view.getFloat32(offset + (i * 4), true));
  }
  return out;
}

export function parseWydAniBuffer(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  if (view.byteLength < 8) {
    throw new Error('ANI inválido: arquivo muito pequeno');
  }

  // Ordem validada no source legado (MeshManager.cpp):
  // uint32 numAniTick, uint32 numAniFrame, seguido de matrizes float4x4.
  const tickCount = view.getUint32(0, true);
  const bonesPerFrame = view.getUint32(4, true);
  const payloadBytes = view.byteLength - 8;
  const matrixBytes = 64;
  const totalMatricesInPayload = Math.floor(payloadBytes / matrixBytes);
  const expectedMatrixCount = tickCount > 0 && bonesPerFrame > 0
    ? tickCount * bonesPerFrame
    : totalMatricesInPayload;
  const matrixCount = Math.min(expectedMatrixCount, totalMatricesInPayload);

  const matrices = new Float32Array(matrixCount * 16);
  for (let i = 0; i < matrices.length; i += 1) {
    matrices[i] = view.getFloat32(8 + (i * 4), true);
  }

  const expectedPayloadBytes = expectedMatrixCount * matrixBytes;
  const firstMatrix = safeReadFloat32Array(view, 8, 16);

  return {
    tickCount,
    bonesPerFrame,
    payloadBytes,
    matrixCount,
    expectedMatrixCount,
    expectedPayloadBytes,
    totalMatricesInPayload,
    matrices,
    firstMatrix,
  };
}

export function getAniMatrixAt(ani, tickIndex, boneIndex, out = null) {
  if (!ani || !ani.matrices || ani.bonesPerFrame <= 0 || ani.tickCount <= 0) {
    return null;
  }

  const tick = Math.max(0, Math.min(ani.tickCount - 1, tickIndex | 0));
  const bone = Math.max(0, Math.min(ani.bonesPerFrame - 1, boneIndex | 0));
  const matrixIndex = (tick * ani.bonesPerFrame) + bone;
  if (matrixIndex >= ani.matrixCount) {
    return null;
  }

  const base = matrixIndex * 16;
  if (!out) {
    return ani.matrices.slice(base, base + 16);
  }
  for (let i = 0; i < 16; i += 1) {
    out[i] = ani.matrices[base + i];
  }
  return out;
}
