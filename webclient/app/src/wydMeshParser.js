function readUint32LE(view, offset) {
  return view.getUint32(offset, true);
}

function readFloat32LE(view, offset) {
  return view.getFloat32(offset, true);
}

function computeBounds(positions) {
  const min = [Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY];
  const max = [Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY];

  for (let i = 0; i < positions.length; i += 3) {
    const x = positions[i];
    const y = positions[i + 1];
    const z = positions[i + 2];

    if (x < min[0]) min[0] = x;
    if (y < min[1]) min[1] = y;
    if (z < min[2]) min[2] = z;

    if (x > max[0]) max[0] = x;
    if (y > max[1]) max[1] = y;
    if (z > max[2]) max[2] = z;
  }

  const center = [
    (min[0] + max[0]) * 0.5,
    (min[1] + max[1]) * 0.5,
    (min[2] + max[2]) * 0.5,
  ];

  let radius = 0;
  for (let i = 0; i < positions.length; i += 3) {
    const dx = positions[i] - center[0];
    const dy = positions[i + 1] - center[1];
    const dz = positions[i + 2] - center[2];
    const dist = Math.hypot(dx, dy, dz);
    if (dist > radius) {
      radius = dist;
    }
  }

  return {
    min,
    max,
    center,
    radius: Math.max(radius, 1),
  };
}

function generatePlanarUv(positions, uvs) {
  let minX = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let minZ = Number.POSITIVE_INFINITY;
  let maxZ = Number.NEGATIVE_INFINITY;

  for (let i = 0; i < positions.length; i += 3) {
    const x = positions[i];
    const z = positions[i + 2];
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (z < minZ) minZ = z;
    if (z > maxZ) maxZ = z;
  }

  const spanX = Math.max(1e-6, maxX - minX);
  const spanZ = Math.max(1e-6, maxZ - minZ);

  for (let i = 0, uvIndex = 0; i < positions.length; i += 3, uvIndex += 2) {
    const x = positions[i];
    const z = positions[i + 2];
    uvs[uvIndex] = (x - minX) / spanX;
    uvs[uvIndex + 1] = 1 - (z - minZ) / spanZ;
  }
}

export function parseWydMshBuffer(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  if (view.byteLength < 32) {
    throw new Error('MSH inválido: arquivo muito pequeno');
  }

  const header = {
    parentId: readUint32LE(view, 0),
    meshId: readUint32LE(view, 4),
    fvf: readUint32LE(view, 8),
    vertexSize: readUint32LE(view, 12),
    faceInfluence: readUint32LE(view, 16),
    paletteCount: readUint32LE(view, 20),
    vertexCount: readUint32LE(view, 24),
    indexCount: readUint32LE(view, 28),
  };

  if (header.vertexCount === 0 || header.indexCount === 0) {
    throw new Error('MSH inválido: vertexCount/indexCount zero');
  }

  let offset = 32;
  const paletteBytes = header.paletteCount * 64;
  const paletteNameBytes = header.paletteCount * 4;

  const boneMatrices = new Float32Array(header.paletteCount * 16);
  for (let paletteIndex = 0; paletteIndex < header.paletteCount; paletteIndex += 1) {
    const matrixBase = offset + (paletteIndex * 64);
    for (let i = 0; i < 16; i += 1) {
      boneMatrices[(paletteIndex * 16) + i] = readFloat32LE(view, matrixBase + (i * 4));
    }
  }
  offset += paletteBytes;

  const boneNames = new Uint32Array(header.paletteCount);
  for (let paletteIndex = 0; paletteIndex < header.paletteCount; paletteIndex += 1) {
    boneNames[paletteIndex] = readUint32LE(view, offset + (paletteIndex * 4));
  }
  offset += paletteNameBytes;

  const vertexBytes = header.vertexCount * header.vertexSize;
  const indexBytes = header.indexCount * 2;

  if (offset + vertexBytes + indexBytes > view.byteLength) {
    throw new Error('MSH inválido: tamanho inconsistente com header');
  }

  const positions = new Float32Array(header.vertexCount * 3);
  const normals = new Float32Array(header.vertexCount * 3);
  const uvs = new Float32Array(header.vertexCount * 2);
  const blendWeights = new Float32Array(header.vertexCount * 3);
  const blendIndices = new Uint8Array(header.vertexCount * 4);

  const weightsPerVertex = Math.max(0, Math.min(3, header.faceInfluence - 1));
  const blendIndicesOffset = 12 + (weightsPerVertex * 4);
  const normalOffset = blendIndicesOffset + 4;
  const uvOffset = normalOffset + 12;

  const hasBlendIndices = blendIndicesOffset + 4 <= header.vertexSize;
  const hasNormals = normalOffset + 12 <= header.vertexSize;
  const hasUV = uvOffset + 8 <= header.vertexSize;

  for (let i = 0; i < header.vertexCount; i += 1) {
    const base = offset + (i * header.vertexSize);

    positions[i * 3] = readFloat32LE(view, base);
    positions[i * 3 + 1] = readFloat32LE(view, base + 4);
    positions[i * 3 + 2] = readFloat32LE(view, base + 8);

    for (let j = 0; j < weightsPerVertex; j += 1) {
      blendWeights[(i * 3) + j] = readFloat32LE(view, base + 12 + (j * 4));
    }

    if (hasBlendIndices) {
      for (let j = 0; j < 4; j += 1) {
        blendIndices[(i * 4) + j] = view.getUint8(base + blendIndicesOffset + j);
      }
    } else {
      blendIndices[i * 4] = 0;
      blendIndices[(i * 4) + 1] = 0;
      blendIndices[(i * 4) + 2] = 0;
      blendIndices[(i * 4) + 3] = 0;
    }

    if (hasNormals) {
      normals[i * 3] = readFloat32LE(view, base + normalOffset);
      normals[i * 3 + 1] = readFloat32LE(view, base + normalOffset + 4);
      normals[i * 3 + 2] = readFloat32LE(view, base + normalOffset + 8);
    } else {
      normals[i * 3] = 0;
      normals[i * 3 + 1] = 1;
      normals[i * 3 + 2] = 0;
    }

    if (hasUV) {
      uvs[i * 2] = readFloat32LE(view, base + uvOffset);
      uvs[i * 2 + 1] = 1 - readFloat32LE(view, base + uvOffset + 4);
    }
  }

  if (!hasUV) {
    generatePlanarUv(positions, uvs);
  }

  offset += vertexBytes;
  const indices = new Uint16Array(header.indexCount);
  for (let i = 0; i < header.indexCount; i += 1) {
    indices[i] = view.getUint16(offset + (i * 2), true);
  }

  const bounds = computeBounds(positions);

  return {
    header,
    vertexLayout: {
      weightsPerVertex,
      blendIndicesOffset,
      normalOffset,
      uvOffset,
      hasBlendIndices,
      hasNormals,
      hasUV,
    },
    positions,
    normals,
    uvs,
    blendWeights,
    blendIndices,
    boneMatrices,
    boneNames,
    indices,
    bounds,
  };
}
