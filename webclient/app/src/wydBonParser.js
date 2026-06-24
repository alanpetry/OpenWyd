function readInt32LE(view, offset) {
  return view.getInt32(offset, true);
}

function readUint32LE(view, offset) {
  return view.getUint32(offset, true);
}

export function parseWydBonBuffer(arrayBuffer) {
  const view = new DataView(arrayBuffer);
  if (view.byteLength < 8 || (view.byteLength % 8) !== 0) {
    throw new Error('BON inválido: tamanho inesperado');
  }

  const pairCount = view.byteLength / 8;
  const pairs = new Array(pairCount);
  const parentByBoneId = new Map();
  let maxBoneId = 0;

  for (let i = 0; i < pairCount; i += 1) {
    const parentRaw = readInt32LE(view, i * 8);
    const boneId = readUint32LE(view, (i * 8) + 4);
    const parentId = parentRaw < 0 ? null : parentRaw;

    pairs[i] = {
      parentId,
      boneId,
    };

    parentByBoneId.set(boneId, parentId);
    if (boneId > maxBoneId) {
      maxBoneId = boneId;
    }
  }

  return {
    pairCount,
    pairs,
    parentByBoneId,
    maxBoneId,
  };
}
