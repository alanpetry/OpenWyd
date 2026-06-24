function readUint16LE(bytes, offset) {
  return bytes[offset] | (bytes[offset + 1] << 8);
}

function readAscii(bytes, offset, length) {
  return String.fromCharCode(...bytes.subarray(offset, offset + length));
}

function parseDdsBuffer(ddsBytes) {
  if (ddsBytes.length < 128) {
    throw new Error('DDS buffer muito pequeno');
  }

  const magic = readAscii(ddsBytes, 0, 4);
  if (magic !== 'DDS ') {
    throw new Error(`Header DDS inválido: ${magic}`);
  }

  const header = new DataView(
    ddsBytes.buffer,
    ddsBytes.byteOffset,
    ddsBytes.byteLength,
  );

  const height = header.getUint32(12, true);
  const width = header.getUint32(16, true);
  const mipMapCount = Math.max(1, header.getUint32(28, true) || 1);
  const fourCC = readAscii(ddsBytes, 84, 4);

  return {
    width,
    height,
    mipMapCount,
    fourCC,
    bytes: ddsBytes,
    format: 'DDS',
  };
}

function decodeTga(tgaBytes) {
  if (tgaBytes.length < 18) {
    throw new Error('TGA buffer muito pequeno');
  }

  const idLength = tgaBytes[0];
  const colorMapType = tgaBytes[1];
  const imageType = tgaBytes[2];
  const width = readUint16LE(tgaBytes, 12);
  const height = readUint16LE(tgaBytes, 14);
  const pixelDepth = tgaBytes[16];
  const descriptor = tgaBytes[17];

  if (colorMapType !== 0) {
    throw new Error(`TGA colormap não suportado: ${colorMapType}`);
  }

  if (imageType !== 2) {
    throw new Error(`TGA imageType não suportado: ${imageType} (esperado 2)`);
  }

  if (pixelDepth !== 24 && pixelDepth !== 32) {
    throw new Error(`TGA pixelDepth não suportado: ${pixelDepth}`);
  }

  const bytesPerPixel = pixelDepth / 8;
  const dataOffset = 18 + idLength;
  const expectedLength = width * height * bytesPerPixel;
  if (tgaBytes.length < dataOffset + expectedLength) {
    throw new Error('TGA truncado');
  }

  const sourceTopLeft = (descriptor & 0x20) !== 0;
  const rgba = new Uint8Array(width * height * 4);
  const imageData = tgaBytes.subarray(dataOffset, dataOffset + expectedLength);

  for (let y = 0; y < height; y += 1) {
    const srcY = sourceTopLeft ? y : (height - 1 - y);

    for (let x = 0; x < width; x += 1) {
      const srcIndex = (srcY * width + x) * bytesPerPixel;
      const dstIndex = (y * width + x) * 4;

      const b = imageData[srcIndex];
      const g = imageData[srcIndex + 1];
      const r = imageData[srcIndex + 2];
      const a = bytesPerPixel === 4 ? imageData[srcIndex + 3] : 255;

      rgba[dstIndex] = r;
      rgba[dstIndex + 1] = g;
      rgba[dstIndex + 2] = b;
      rgba[dstIndex + 3] = a;
    }
  }

  return {
    width,
    height,
    pixelDepth,
    imageType,
    rgba,
  };
}

export function decodeWytBuffer(arrayBuffer) {
  const bytes = new Uint8Array(arrayBuffer);
  if (bytes.length < 22) {
    throw new Error('WYT buffer muito pequeno');
  }

  const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
  if (magic !== 'WT10') {
    throw new Error(`Header WYT inválido: ${magic}`);
  }

  const tgaBytes = bytes.subarray(4);
  const decoded = decodeTga(tgaBytes);

  return {
    ...decoded,
    format: 'WYT/TGA',
  };
}

export function decodeWysBuffer(arrayBuffer) {
  const bytes = new Uint8Array(arrayBuffer);
  if (bytes.length < 129) {
    throw new Error('WYS buffer muito pequeno');
  }

  const magic = readAscii(bytes, 0, 4);
  if (magic !== 'WS10') {
    throw new Error(`Header WYS inválido: ${magic}`);
  }

  // Formato legado do client:
  // 1) descarta o primeiro byte
  // 2) substitui os 3 primeiros por "DDS"
  // 3) corrige fourCC em [84..87] para DXT1/DXT3
  const ddsBytes = new Uint8Array(bytes.length - 1);
  ddsBytes.set(bytes.subarray(1));
  ddsBytes[0] = 0x44; // D
  ddsBytes[1] = 0x44; // D
  ddsBytes[2] = 0x53; // S

  const dxtKind = ddsBytes[84] === 0x32 ? 'DXT1' : 'DXT3';
  ddsBytes[84] = 0x44; // D
  ddsBytes[85] = 0x58; // X
  ddsBytes[86] = 0x54; // T
  ddsBytes[87] = dxtKind === 'DXT1' ? 0x31 : 0x33; // 1 or 3

  const dds = parseDdsBuffer(ddsBytes);
  return {
    ...dds,
    format: `WYS/${dds.fourCC}`,
  };
}
