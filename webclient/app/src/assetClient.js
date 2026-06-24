export class AssetClient {
  async fetchJson(path) {
    const response = await fetch(path);
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${path}`);
    }

    return response.json();
  }

  async getManifestSummary() {
    return this.fetchJson('/api/manifest');
  }

  async getBootstrapManifest() {
    return this.fetchJson('/api/manifest/bootstrap');
  }

  async resolve(path) {
    const response = await fetch(`/api/resolve?path=${encodeURIComponent(path)}`);
    const payload = await response.json();

    if (!response.ok || !payload.ok) {
      throw new Error(payload.error || `Resolve failed for ${path}`);
    }

    return payload;
  }

  async fetchBuffer(path) {
    const resolved = await this.resolve(path);
    const response = await fetch(resolved.assetUrl);
    if (!response.ok) {
      throw new Error(`HTTP ${response.status} when fetching ${resolved.assetUrl}`);
    }

    return {
      resolved,
      buffer: await response.arrayBuffer(),
      contentType: response.headers.get('content-type') || 'application/octet-stream',
    };
  }
}

export function bytesToAscii(bytes, length = 4) {
  const slice = bytes.slice(0, length);
  return String.fromCharCode(...slice);
}

export function bytesToHex(bytes, length = 16) {
  const slice = bytes.slice(0, Math.min(length, bytes.length));
  return Array.from(slice)
    .map((value) => value.toString(16).padStart(2, '0'))
    .join(' ');
}
