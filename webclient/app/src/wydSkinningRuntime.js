function writeIdentityMatrix(out, offset = 0) {
  out[offset + 0] = 1; out[offset + 1] = 0; out[offset + 2] = 0; out[offset + 3] = 0;
  out[offset + 4] = 0; out[offset + 5] = 1; out[offset + 6] = 0; out[offset + 7] = 0;
  out[offset + 8] = 0; out[offset + 9] = 0; out[offset + 10] = 1; out[offset + 11] = 0;
  out[offset + 12] = 0; out[offset + 13] = 0; out[offset + 14] = 0; out[offset + 15] = 1;
}

function copyMatrix(out, outOffset, input, inputOffset) {
  for (let i = 0; i < 16; i += 1) {
    out[outOffset + i] = input[inputOffset + i];
  }
}

// Row-major matrix multiply compatible with D3DXMatrixMultiply(out, a, b): out = a * b
function multiplyRowMajor(out, outOffset, a, aOffset, b, bOffset) {
  for (let row = 0; row < 4; row += 1) {
    const r0 = a[aOffset + (row * 4)];
    const r1 = a[aOffset + (row * 4) + 1];
    const r2 = a[aOffset + (row * 4) + 2];
    const r3 = a[aOffset + (row * 4) + 3];

    out[outOffset + (row * 4)] = (r0 * b[bOffset]) + (r1 * b[bOffset + 4]) + (r2 * b[bOffset + 8]) + (r3 * b[bOffset + 12]);
    out[outOffset + (row * 4) + 1] = (r0 * b[bOffset + 1]) + (r1 * b[bOffset + 5]) + (r2 * b[bOffset + 9]) + (r3 * b[bOffset + 13]);
    out[outOffset + (row * 4) + 2] = (r0 * b[bOffset + 2]) + (r1 * b[bOffset + 6]) + (r2 * b[bOffset + 10]) + (r3 * b[bOffset + 14]);
    out[outOffset + (row * 4) + 3] = (r0 * b[bOffset + 3]) + (r1 * b[bOffset + 7]) + (r2 * b[bOffset + 11]) + (r3 * b[bOffset + 15]);
  }
}

function normalize3(x, y, z) {
  const length = Math.hypot(x, y, z);
  if (length <= 1e-6) {
    return [0, 1, 0];
  }
  return [x / length, y / length, z / length];
}

function transformPointRowMajor(matrix, offset, x, y, z) {
  return [
    (x * matrix[offset]) + (y * matrix[offset + 4]) + (z * matrix[offset + 8]) + matrix[offset + 12],
    (x * matrix[offset + 1]) + (y * matrix[offset + 5]) + (z * matrix[offset + 9]) + matrix[offset + 13],
    (x * matrix[offset + 2]) + (y * matrix[offset + 6]) + (z * matrix[offset + 10]) + matrix[offset + 14],
  ];
}

function transformNormalRowMajor(matrix, offset, x, y, z) {
  return [
    (x * matrix[offset]) + (y * matrix[offset + 4]) + (z * matrix[offset + 8]),
    (x * matrix[offset + 1]) + (y * matrix[offset + 5]) + (z * matrix[offset + 9]),
    (x * matrix[offset + 2]) + (y * matrix[offset + 6]) + (z * matrix[offset + 10]),
  ];
}

function wrapTick(ani, tickValue) {
  if (!ani || ani.tickCount <= 0) {
    return 0;
  }

  const ticks = ani.tickCount;
  const wrapped = tickValue % ticks;
  return wrapped < 0 ? wrapped + ticks : wrapped;
}

function writeLocalBoneMatrix(context, boneId, tickIndex, out, outOffset = 0) {
  const { ani } = context;
  if (!ani || ani.bonesPerFrame <= 0 || ani.tickCount <= 0) {
    writeIdentityMatrix(out, outOffset);
    return;
  }

  if (boneId < 0 || boneId >= ani.bonesPerFrame) {
    writeIdentityMatrix(out, outOffset);
    return;
  }

  const tick = wrapTick(ani, tickIndex);
  const matrixIndex = (tick * ani.bonesPerFrame) + boneId;
  const srcOffset = matrixIndex * 16;
  if ((srcOffset + 16) > ani.matrices.length) {
    writeIdentityMatrix(out, outOffset);
    return;
  }

  copyMatrix(out, outOffset, ani.matrices, srcOffset);
}

function resetCombinedCache(context) {
  context.combinedValid.fill(0);
}

function resolveCombinedBoneMatrix(context, boneId, tickIndex, visiting = null) {
  if (boneId < 0 || boneId > context.maxBoneId) {
    context.identityTmp ||= new Float32Array(16);
    writeIdentityMatrix(context.identityTmp, 0);
    return context.identityTmp;
  }

  const baseOffset = boneId * 16;
  if (context.combinedValid[boneId]) {
    return context.combined.subarray(baseOffset, baseOffset + 16);
  }

  const seen = visiting || new Set();
  if (seen.has(boneId)) {
    writeIdentityMatrix(context.combined, baseOffset);
    context.combinedValid[boneId] = 1;
    return context.combined.subarray(baseOffset, baseOffset + 16);
  }

  seen.add(boneId);

  context.localTmp ||= new Float32Array(16);
  writeLocalBoneMatrix(context, boneId, tickIndex, context.localTmp, 0);

  const parentId = context.parentByBoneId.get(boneId);
  if (parentId == null || parentId === boneId || parentId < 0) {
    copyMatrix(context.combined, baseOffset, context.localTmp, 0);
  } else {
    const parentMatrix = resolveCombinedBoneMatrix(context, parentId, tickIndex, seen);
    multiplyRowMajor(context.combined, baseOffset, context.localTmp, 0, parentMatrix, 0);
  }

  context.combinedValid[boneId] = 1;
  seen.delete(boneId);
  return context.combined.subarray(baseOffset, baseOffset + 16);
}

function buildPaletteMatricesForTick(context, tickIndex) {
  const { mesh } = context;
  const paletteCount = mesh.header.paletteCount;
  if (paletteCount <= 0) {
    writeIdentityMatrix(context.paletteMatrices, 0);
    return;
  }

  resetCombinedCache(context);

  for (let paletteIndex = 0; paletteIndex < paletteCount; paletteIndex += 1) {
    const boneId = mesh.boneNames[paletteIndex];
    const combined = resolveCombinedBoneMatrix(context, boneId, tickIndex);
    const bindOffset = paletteIndex * 16;
    const outOffset = paletteIndex * 16;
    multiplyRowMajor(
      context.paletteMatrices,
      outOffset,
      mesh.boneMatrices,
      bindOffset,
      combined,
      0,
    );
  }
}

function computeVertexInfluenceWeights(mesh, vertexIndex) {
  const influences = Math.max(1, Math.min(4, mesh.header.faceInfluence || 1));
  const wOffset = vertexIndex * 3;
  const w0 = mesh.blendWeights[wOffset] || 0;
  const w1 = mesh.blendWeights[wOffset + 1] || 0;
  const w2 = mesh.blendWeights[wOffset + 2] || 0;

  if (influences === 1) {
    return [1, 0, 0, 0];
  }
  if (influences === 2) {
    const a = Math.min(1, Math.max(0, w0));
    return [a, 1 - a, 0, 0];
  }
  if (influences === 3) {
    const a = Math.min(1, Math.max(0, w0));
    const b = Math.min(1, Math.max(0, w1));
    return [a, b, Math.max(0, 1 - a - b), 0];
  }

  const a = Math.min(1, Math.max(0, w0));
  const b = Math.min(1, Math.max(0, w1));
  const c = Math.min(1, Math.max(0, w2));
  return [a, b, c, Math.max(0, 1 - a - b - c)];
}

export function isMeshSkinnable(mesh) {
  return Boolean(
    mesh
    && mesh.header
    && mesh.header.paletteCount > 0
    && mesh.header.faceInfluence >= 1
    && mesh.boneMatrices
    && mesh.boneMatrices.length >= 16
    && mesh.boneNames
    && mesh.boneNames.length > 0
    && mesh.blendIndices
    && mesh.blendIndices.length > 0,
  );
}

export function createSkinningContext(mesh, ani, bon = null) {
  if (!isMeshSkinnable(mesh)) {
    throw new Error('Mesh não contém dados de skinning suficientes');
  }

  const parentByBoneId = bon?.parentByBoneId || new Map();
  let maxBoneId = Math.max((ani?.bonesPerFrame || 0) - 1, bon?.maxBoneId || 0);
  for (let i = 0; i < mesh.boneNames.length; i += 1) {
    if (mesh.boneNames[i] > maxBoneId) {
      maxBoneId = mesh.boneNames[i];
    }
  }

  const paletteCount = Math.max(1, mesh.header.paletteCount);
  const context = {
    mesh,
    ani,
    bon,
    parentByBoneId,
    maxBoneId: Math.max(0, maxBoneId),
    combined: new Float32Array((Math.max(0, maxBoneId) + 1) * 16),
    combinedValid: new Uint8Array(Math.max(1, Math.max(0, maxBoneId) + 1)),
    paletteMatrices: new Float32Array(Math.max(1, paletteCount) * 16),
    deformedPositions: new Float32Array(mesh.positions.length),
    deformedNormals: new Float32Array(mesh.normals.length),
    currentTick: 0,
  };

  writeIdentityMatrix(context.paletteMatrices, 0);
  return context;
}

export function applySkinningAtTick(context, tickIndex) {
  if (!context || !context.mesh) {
    throw new Error('Contexto de skinning inválido');
  }

  const mesh = context.mesh;
  const vertexCount = mesh.header.vertexCount;
  const paletteCount = Math.max(1, mesh.header.paletteCount);
  context.currentTick = wrapTick(context.ani, tickIndex);

  buildPaletteMatricesForTick(context, context.currentTick);

  for (let vertex = 0; vertex < vertexCount; vertex += 1) {
    const pOffset = vertex * 3;
    const nOffset = vertex * 3;
    const iOffset = vertex * 4;

    const x = mesh.positions[pOffset];
    const y = mesh.positions[pOffset + 1];
    const z = mesh.positions[pOffset + 2];

    const nx = mesh.normals[nOffset];
    const ny = mesh.normals[nOffset + 1];
    const nz = mesh.normals[nOffset + 2];

    const weights = computeVertexInfluenceWeights(mesh, vertex);
    const influences = Math.max(1, Math.min(4, mesh.header.faceInfluence || 1));

    let sx = 0;
    let sy = 0;
    let sz = 0;
    let snx = 0;
    let sny = 0;
    let snz = 0;
    let weightSum = 0;

    for (let influence = 0; influence < influences; influence += 1) {
      const weight = weights[influence];
      if (weight <= 1e-6) {
        continue;
      }

      const paletteIndex = mesh.blendIndices[iOffset + influence];
      const matrixIndex = paletteIndex >= paletteCount ? 0 : paletteIndex;
      const matrixOffset = matrixIndex * 16;

      const tp = transformPointRowMajor(context.paletteMatrices, matrixOffset, x, y, z);
      const tn = transformNormalRowMajor(context.paletteMatrices, matrixOffset, nx, ny, nz);

      sx += tp[0] * weight;
      sy += tp[1] * weight;
      sz += tp[2] * weight;
      snx += tn[0] * weight;
      sny += tn[1] * weight;
      snz += tn[2] * weight;
      weightSum += weight;
    }

    if (weightSum <= 1e-6) {
      sx = x;
      sy = y;
      sz = z;
      snx = nx;
      sny = ny;
      snz = nz;
    }

    const normalized = normalize3(snx, sny, snz);
    context.deformedPositions[pOffset] = sx;
    context.deformedPositions[pOffset + 1] = sy;
    context.deformedPositions[pOffset + 2] = sz;
    context.deformedNormals[nOffset] = normalized[0];
    context.deformedNormals[nOffset + 1] = normalized[1];
    context.deformedNormals[nOffset + 2] = normalized[2];
  }

  return {
    tick: context.currentTick,
    positions: context.deformedPositions,
    normals: context.deformedNormals,
  };
}
