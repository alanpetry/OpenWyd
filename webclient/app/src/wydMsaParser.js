function decodeAsciiFixed(bytes, offset, size) {
  const slice = bytes.subarray(offset, offset + size);
  let end = slice.indexOf(0);
  if (end < 0) {
    end = slice.length;
  }
  return String.fromCharCode(...slice.subarray(0, end)).trim();
}

function parseAttributeRanges(dataView, offset, count) {
  const ranges = [];
  let cursor = offset;
  for (let i = 0; i < count; i += 1) {
    const attribId = dataView.getUint32(cursor + 0, true);
    const faceStart = dataView.getUint32(cursor + 4, true);
    const faceCount = dataView.getUint32(cursor + 8, true);
    const vertexStart = dataView.getUint32(cursor + 12, true);
    const vertexCount = dataView.getUint32(cursor + 16, true);
    ranges.push({ attribId, faceStart, faceCount, vertexStart, vertexCount });
    cursor += 20;
  }
  return { ranges, nextOffset: cursor };
}

function parseIndices(bytes, offset, indexBufferSize) {
  if (indexBufferSize % 2 !== 0) {
    throw new Error(`MSA index buffer inválido: ${indexBufferSize}`);
  }

  const count = indexBufferSize / 2;
  const indices = new Uint16Array(count);
  const view = new DataView(bytes.buffer, bytes.byteOffset + offset, indexBufferSize);
  for (let i = 0; i < count; i += 1) {
    indices[i] = view.getUint16(i * 2, true);
  }
  return indices;
}

function deriveNormalsFromTriangles(positions, indices) {
  const normals = new Float32Array(positions.length);
  for (let i = 0; i + 2 < indices.length; i += 3) {
    const ia = indices[i + 0] * 3;
    const ib = indices[i + 1] * 3;
    const ic = indices[i + 2] * 3;
    if (ic + 2 >= positions.length || ib + 2 >= positions.length || ia + 2 >= positions.length) {
      continue;
    }

    const ax = positions[ia + 0];
    const ay = positions[ia + 1];
    const az = positions[ia + 2];
    const bx = positions[ib + 0];
    const by = positions[ib + 1];
    const bz = positions[ib + 2];
    const cx = positions[ic + 0];
    const cy = positions[ic + 1];
    const cz = positions[ic + 2];

    const abx = bx - ax;
    const aby = by - ay;
    const abz = bz - az;
    const acx = cx - ax;
    const acy = cy - ay;
    const acz = cz - az;

    const nx = (aby * acz) - (abz * acy);
    const ny = (abz * acx) - (abx * acz);
    const nz = (abx * acy) - (aby * acx);

    normals[ia + 0] += nx;
    normals[ia + 1] += ny;
    normals[ia + 2] += nz;
    normals[ib + 0] += nx;
    normals[ib + 1] += ny;
    normals[ib + 2] += nz;
    normals[ic + 0] += nx;
    normals[ic + 1] += ny;
    normals[ic + 2] += nz;
  }

  for (let i = 0; i + 2 < normals.length; i += 3) {
    const nx = normals[i + 0];
    const ny = normals[i + 1];
    const nz = normals[i + 2];
    const length = Math.hypot(nx, ny, nz) || 1;
    normals[i + 0] = nx / length;
    normals[i + 1] = ny / length;
    normals[i + 2] = nz / length;
  }

  return normals;
}

function computeBounds(positions) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i + 2 < positions.length; i += 3) {
    const x = positions[i + 0];
    const y = positions[i + 1];
    const z = positions[i + 2];
    min[0] = Math.min(min[0], x);
    min[1] = Math.min(min[1], y);
    min[2] = Math.min(min[2], z);
    max[0] = Math.max(max[0], x);
    max[1] = Math.max(max[1], y);
    max[2] = Math.max(max[2], z);
  }

  const center = [
    (min[0] + max[0]) * 0.5,
    (min[1] + max[1]) * 0.5,
    (min[2] + max[2]) * 0.5,
  ];
  let radius = 1;
  for (let i = 0; i + 2 < positions.length; i += 3) {
    const dx = positions[i + 0] - center[0];
    const dy = positions[i + 1] - center[1];
    const dz = positions[i + 2] - center[2];
    radius = Math.max(radius, Math.hypot(dx, dy, dz));
  }

  return { min, max, center, radius };
}

export function parseWydMsaBuffer(arrayBuffer, options = {}) {
  const bytes = new Uint8Array(arrayBuffer);
  if (bytes.length < 16) {
    throw new Error('MSA inválido: buffer muito pequeno');
  }

  const dataView = new DataView(arrayBuffer);
  let offset = 0;

  const fvf = dataView.getUint32(offset, true); offset += 4;
  const vertexStride = dataView.getUint32(offset, true); offset += 4;
  const attributeCount = dataView.getUint32(offset, true); offset += 4;

  if (attributeCount <= 0 || attributeCount > 64) {
    throw new Error(`MSA inválido: attributeCount=${attributeCount}`);
  }

  const parsedRanges = parseAttributeRanges(dataView, offset, attributeCount);
  offset = parsedRanges.nextOffset;
  if (offset > bytes.length) {
    throw new Error('MSA inválido: ranges fora do limite');
  }

  const textureRefs = [];
  for (let i = 0; i < attributeCount; i += 1) {
    if (offset + 11 > bytes.length) {
      throw new Error('MSA inválido: texture refs truncadas');
    }
    const raw = decodeAsciiFixed(bytes, offset, 11);
    textureRefs.push(raw);
    offset += 11;
  }

  if (offset + 4 > bytes.length) {
    throw new Error('MSA inválido: sem índice de buffer');
  }
  const indexBufferSize = dataView.getUint32(offset, true);
  offset += 4;
  if (offset + indexBufferSize > bytes.length) {
    throw new Error('MSA inválido: index buffer truncado');
  }
  const indices = parseIndices(bytes, offset, indexBufferSize);
  offset += indexBufferSize;

  if (offset + 4 > bytes.length) {
    throw new Error('MSA inválido: sem tamanho de vertex buffer');
  }
  const vertexBufferSize = dataView.getUint32(offset, true);
  offset += 4;
  if (vertexStride <= 0 || vertexBufferSize <= 0 || vertexBufferSize % vertexStride !== 0) {
    throw new Error(`MSA inválido: vertexBufferSize=${vertexBufferSize} stride=${vertexStride}`);
  }
  if (offset + vertexBufferSize > bytes.length) {
    throw new Error('MSA inválido: vertex buffer truncado');
  }

  const vertexCount = vertexBufferSize / vertexStride;
  const positions = new Float32Array(vertexCount * 3);
  let normals = new Float32Array(vertexCount * 3);
  const uvs = new Float32Array(vertexCount * 2);

  const readVertexView = new DataView(bytes.buffer, bytes.byteOffset + offset, vertexBufferSize);
  for (let i = 0; i < vertexCount; i += 1) {
    const base = i * vertexStride;
    const x = readVertexView.getFloat32(base + 0, true);
    const y = readVertexView.getFloat32(base + 4, true);
    const z = readVertexView.getFloat32(base + 8, true);

    positions[i * 3 + 0] = x;
    positions[i * 3 + 1] = y;
    positions[i * 3 + 2] = z;

    if (fvf === 274 && vertexStride >= 32) {
      normals[i * 3 + 0] = readVertexView.getFloat32(base + 12, true);
      normals[i * 3 + 1] = readVertexView.getFloat32(base + 16, true);
      normals[i * 3 + 2] = readVertexView.getFloat32(base + 20, true);
      uvs[i * 2 + 0] = readVertexView.getFloat32(base + 24, true);
      uvs[i * 2 + 1] = readVertexView.getFloat32(base + 28, true);
    } else if (fvf === 322 && vertexStride >= 24) {
      uvs[i * 2 + 0] = readVertexView.getFloat32(base + 16, true);
      uvs[i * 2 + 1] = readVertexView.getFloat32(base + 20, true);
    } else if (fvf === 18 && vertexStride >= 24) {
      normals[i * 3 + 0] = readVertexView.getFloat32(base + 12, true);
      normals[i * 3 + 1] = readVertexView.getFloat32(base + 16, true);
      normals[i * 3 + 2] = readVertexView.getFloat32(base + 20, true);
    } else {
      throw new Error(`MSA FVF não suportado: ${fvf} stride=${vertexStride}`);
    }
  }

  const hasNormalsInSource = fvf === 274 || fvf === 18;
  if (!hasNormalsInSource) {
    normals = deriveNormalsFromTriangles(positions, indices);
  }

  const bounds = computeBounds(positions);
  const sourcePath = String(options.sourcePath || '').replace(/\//g, '\\');
  const sourceStem = sourcePath.length > 0
    ? sourcePath.split('\\').pop().replace(/\.[^/.]+$/, '')
    : '';

  return {
    format: 'MSA',
    sourcePath,
    sourceStem,
    header: {
      fvf,
      vertexStride,
      attributeCount,
      indexBufferSize,
      vertexBufferSize,
      vertexCount,
      indexCount: indices.length,
    },
    attributes: parsedRanges.ranges,
    textureRefs,
    positions,
    normals,
    uvs,
    indices,
    bounds,
  };
}
