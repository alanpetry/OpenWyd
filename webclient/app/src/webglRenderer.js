function createShader(gl, type, source) {
  const shader = gl.createShader(type);
  if (!shader) {
    throw new Error('Unable to create shader');
  }

  gl.shaderSource(shader, source);
  gl.compileShader(shader);

  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(shader);
    gl.deleteShader(shader);
    throw new Error(`Shader compile error: ${log}`);
  }

  return shader;
}

function createProgram(gl, vertexSource, fragmentSource) {
  const vertexShader = createShader(gl, gl.VERTEX_SHADER, vertexSource);
  const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fragmentSource);

  const program = gl.createProgram();
  if (!program) {
    throw new Error('Unable to create program');
  }

  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);

  gl.deleteShader(vertexShader);
  gl.deleteShader(fragmentShader);

  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(program);
    gl.deleteProgram(program);
    throw new Error(`Program link error: ${log}`);
  }

  return program;
}

function mat4Perspective(out, fovy, aspect, near, far) {
  const f = 1.0 / Math.tan(fovy / 2);

  out[0] = f / aspect;
  out[1] = 0;
  out[2] = 0;
  out[3] = 0;
  out[4] = 0;
  out[5] = f;
  out[6] = 0;
  out[7] = 0;
  out[8] = 0;
  out[9] = 0;
  out[11] = -1;
  out[12] = 0;
  out[13] = 0;
  out[15] = 0;

  if (far != null && far !== Infinity) {
    const nf = 1 / (near - far);
    out[10] = (far + near) * nf;
    out[14] = (2 * far * near) * nf;
  } else {
    out[10] = -1;
    out[14] = -2 * near;
  }

  return out;
}

function mat4Multiply(out, a, b) {
  const a00 = a[0]; const a01 = a[1]; const a02 = a[2]; const a03 = a[3];
  const a10 = a[4]; const a11 = a[5]; const a12 = a[6]; const a13 = a[7];
  const a20 = a[8]; const a21 = a[9]; const a22 = a[10]; const a23 = a[11];
  const a30 = a[12]; const a31 = a[13]; const a32 = a[14]; const a33 = a[15];

  const b00 = b[0]; const b01 = b[1]; const b02 = b[2]; const b03 = b[3];
  const b10 = b[4]; const b11 = b[5]; const b12 = b[6]; const b13 = b[7];
  const b20 = b[8]; const b21 = b[9]; const b22 = b[10]; const b23 = b[11];
  const b30 = b[12]; const b31 = b[13]; const b32 = b[14]; const b33 = b[15];

  out[0] = b00 * a00 + b01 * a10 + b02 * a20 + b03 * a30;
  out[1] = b00 * a01 + b01 * a11 + b02 * a21 + b03 * a31;
  out[2] = b00 * a02 + b01 * a12 + b02 * a22 + b03 * a32;
  out[3] = b00 * a03 + b01 * a13 + b02 * a23 + b03 * a33;

  out[4] = b10 * a00 + b11 * a10 + b12 * a20 + b13 * a30;
  out[5] = b10 * a01 + b11 * a11 + b12 * a21 + b13 * a31;
  out[6] = b10 * a02 + b11 * a12 + b12 * a22 + b13 * a32;
  out[7] = b10 * a03 + b11 * a13 + b12 * a23 + b13 * a33;

  out[8] = b20 * a00 + b21 * a10 + b22 * a20 + b23 * a30;
  out[9] = b20 * a01 + b21 * a11 + b22 * a21 + b23 * a31;
  out[10] = b20 * a02 + b21 * a12 + b22 * a22 + b23 * a32;
  out[11] = b20 * a03 + b21 * a13 + b22 * a23 + b23 * a33;

  out[12] = b30 * a00 + b31 * a10 + b32 * a20 + b33 * a30;
  out[13] = b30 * a01 + b31 * a11 + b32 * a21 + b33 * a31;
  out[14] = b30 * a02 + b31 * a12 + b32 * a22 + b33 * a32;
  out[15] = b30 * a03 + b31 * a13 + b32 * a23 + b33 * a33;

  return out;
}

function mat4LookAt(out, eye, center, up) {
  let x0;
  let x1;
  let x2;
  let y0;
  let y1;
  let y2;
  let z0;
  let z1;
  let z2;
  let len;

  z0 = eye[0] - center[0];
  z1 = eye[1] - center[1];
  z2 = eye[2] - center[2];

  len = Math.hypot(z0, z1, z2);
  if (len === 0) {
    z2 = 1;
  } else {
    z0 /= len;
    z1 /= len;
    z2 /= len;
  }

  x0 = up[1] * z2 - up[2] * z1;
  x1 = up[2] * z0 - up[0] * z2;
  x2 = up[0] * z1 - up[1] * z0;

  len = Math.hypot(x0, x1, x2);
  if (len === 0) {
    x0 = 0;
    x1 = 0;
    x2 = 0;
  } else {
    x0 /= len;
    x1 /= len;
    x2 /= len;
  }

  y0 = z1 * x2 - z2 * x1;
  y1 = z2 * x0 - z0 * x2;
  y2 = z0 * x1 - z1 * x0;

  len = Math.hypot(y0, y1, y2);
  if (len !== 0) {
    y0 /= len;
    y1 /= len;
    y2 /= len;
  }

  out[0] = x0;
  out[1] = y0;
  out[2] = z0;
  out[3] = 0;
  out[4] = x1;
  out[5] = y1;
  out[6] = z1;
  out[7] = 0;
  out[8] = x2;
  out[9] = y2;
  out[10] = z2;
  out[11] = 0;
  out[12] = -(x0 * eye[0] + x1 * eye[1] + x2 * eye[2]);
  out[13] = -(y0 * eye[0] + y1 * eye[1] + y2 * eye[2]);
  out[14] = -(z0 * eye[0] + z1 * eye[1] + z2 * eye[2]);
  out[15] = 1;
  return out;
}

function mat4TransformPoint(out, matrix, x, y, z, w = 1.0) {
  out[0] = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12] * w;
  out[1] = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13] * w;
  out[2] = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14] * w;
  out[3] = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15] * w;
  return out;
}

export class WebGLRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.gl = canvas.getContext('webgl2', { antialias: true, alpha: false });
    if (!this.gl) {
      throw new Error('WebGL2 indisponível neste navegador');
    }

    this.startTime = performance.now();
    this.s3tc = null;

    this.texture = null;
    this.textureReady = false;
    this.terrainTexture = null;
    this.terrainTextureReady = false;

    this.programQuad = null;
    this.programMesh = null;
    this.programTerrain = null;
    this.programPoints = null;
    this.programObjectMesh = null;

    this.quad = {
      vertexBuffer: null,
      attributes: { position: -1, uv: -1 },
      uniforms: { time: null, textureReady: null, textureSampler: null },
    };

    this.mesh = {
      ready: false,
      positionBuffer: null,
      normalBuffer: null,
      uvBuffer: null,
      indexBuffer: null,
      indexCount: 0,
      vertexCount: 0,
      center: [0, 0, 0],
      radius: 1,
      drawScale: 1,
      sceneScale: 1,
      world: {
        enabled: false,
        position: [0, 0, 0],
        yaw: 0,
      },
      dynamic: false,
      attributes: { position: -1, normal: -1, uv: -1 },
      uniforms: {
        viewProj: null,
        center: null,
        meshScale: null,
        sceneScale: null,
        modelPos: null,
        modelYaw: null,
        lightDir: null,
        textureSampler: null,
        textureReady: null,
      },
    };

    this.terrain = {
      ready: false,
      visible: true,
      scale: 0.08,
      textureVisible: true,
      textureBlend: 0.82,
      center: [0, 0, 0],
      radius: 48,
      positionBuffer: null,
      normalBuffer: null,
      colorBuffer: null,
      uvBuffer: null,
      indexBuffer: null,
      indexCount: 0,
      attributes: { position: -1, normal: -1, color: -1, uv: -1 },
      uniforms: {
        viewProj: null,
        center: null,
        scale: null,
        lightDir: null,
        textureSampler: null,
        textureReady: null,
        textureBlend: null,
      },
    };

    this.objectPoints = {
      ready: false,
      visible: true,
      pointSize: 2.5,
      center: [0, 0, 0],
      scale: 0.08,
      count: 0,
      positionBuffer: null,
      colorBuffer: null,
      positions: null,
      baseColors: null,
      items: [],
      selectedIndex: -1,
      attributes: { position: -1, color: -1 },
      uniforms: {
        viewProj: null,
        center: null,
        scale: null,
        pointSize: null,
      },
    };

    this.objectMeshes = {
      ready: false,
      visible: true,
      scale: 0.08,
      maxInstances: 640,
      groups: [],
      center: [0, 0, 0],
      radius: 46,
      drawStats: {
        groupCount: 0,
        materialCount: 0,
        instanceCount: 0,
        vertexCount: 0,
        triangleCount: 0,
      },
      attributes: {
        position: -1,
        normal: -1,
        uv: -1,
        instancePos: -1,
        instanceYaw: -1,
        instanceScaleHV: -1,
      },
      uniforms: {
        viewProj: null,
        center: null,
        scale: null,
        lightDir: null,
        textureSampler: null,
        textureReady: null,
      },
    };

    this.sceneState = {
      hasViewProj: false,
      viewProj: new Float32Array(16),
      viewportWidth: 0,
      viewportHeight: 0,
    };

    this.camera = {
      targetEnabled: false,
      target: [0, 0, 0],
      mode: 'orbit',
      followYaw: 0,
      followDistance: 7.2,
      followHeight: 2.8,
      followLead: 0.55,
    };

    this._initPipeline();
  }

  _initPipeline() {
    const gl = this.gl;

    const quadVertexSource = `#version 300 es
in vec2 aPosition;
in vec2 aUV;
out vec2 vUV;

void main() {
  vUV = aUV;
  gl_Position = vec4(aPosition, 0.0, 1.0);
}
`;

    const quadFragmentSource = `#version 300 es
precision mediump float;

in vec2 vUV;
out vec4 outColor;

uniform sampler2D uTexture;
uniform float uTime;
uniform float uTextureReady;

void main() {
  vec3 base = vec3(
    0.08 + 0.04 * sin(uTime * 0.7),
    0.10 + 0.03 * cos(uTime * 0.6),
    0.16 + 0.02 * sin(uTime * 0.4)
  );

  vec3 tex = texture(uTexture, vUV).rgb;
  vec3 color = mix(base, tex, uTextureReady);
  outColor = vec4(color, 1.0);
}
`;

    const meshVertexSource = `#version 300 es
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUV;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uMeshScale;
uniform float uSceneScale;
uniform vec3 uModelPos;
uniform float uModelYaw;

out vec3 vNormal;
out vec2 vUV;

vec3 applyPitchFix(vec3 value) {
  return vec3(value.x, value.z, -value.y);
}

void main() {
  vec3 local = applyPitchFix(aPosition - uCenter) * uMeshScale;
  float c = cos(uModelYaw);
  float s = sin(uModelYaw);
  vec3 rotated = vec3(
    local.x * c - local.z * s,
    local.y,
    local.x * s + local.z * c
  );

  vec3 centeredPosition = rotated + (uModelPos * uSceneScale);
  vec3 normalFixed = normalize(applyPitchFix(aNormal));
  vNormal = vec3(
    normalFixed.x * c - normalFixed.z * s,
    normalFixed.y,
    normalFixed.x * s + normalFixed.z * c
  );
  vUV = aUV;
  gl_Position = uViewProj * vec4(centeredPosition, 1.0);
}
`;

    const meshFragmentSource = `#version 300 es
precision mediump float;

in vec3 vNormal;
in vec2 vUV;
out vec4 outColor;

uniform sampler2D uTexture;
uniform float uTextureReady;
uniform vec3 uLightDir;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 lightDir = normalize(-uLightDir);
  float ndl = max(dot(normal, lightDir), 0.2);

  vec3 base = vec3(0.68, 0.74, 0.80);
  vec3 tex = texture(uTexture, vUV).rgb;
  vec3 albedo = mix(base, tex, uTextureReady);

  vec3 finalColor = albedo * ndl + albedo * 0.25;
  outColor = vec4(finalColor, 1.0);
}
`;

    const terrainVertexSource = `#version 300 es
in vec3 aPosition;
in vec3 aNormal;
in vec3 aColor;
in vec2 aUV;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uScale;

out vec3 vNormal;
out vec3 vColor;
out vec2 vUV;

void main() {
  vec3 centeredPosition = (aPosition - uCenter) * uScale;
  vNormal = aNormal;
  vColor = aColor;
  vUV = aUV;
  gl_Position = uViewProj * vec4(centeredPosition, 1.0);
}
`;

    const terrainFragmentSource = `#version 300 es
precision mediump float;

in vec3 vNormal;
in vec3 vColor;
in vec2 vUV;
out vec4 outColor;

uniform vec3 uLightDir;
uniform sampler2D uTerrainTexture;
uniform float uTextureReady;
uniform float uTextureBlend;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 lightDir = normalize(-uLightDir);
  float ndl = max(dot(normal, lightDir), 0.25);
  vec3 tex = texture(uTerrainTexture, vUV).rgb;
  float blend = clamp(uTextureReady * uTextureBlend, 0.0, 1.0);
  vec3 albedo = mix(vColor, tex, blend);
  vec3 lit = albedo * ndl + albedo * 0.22;
  outColor = vec4(lit, 0.98);
}
`;

    const pointsVertexSource = `#version 300 es
in vec3 aPosition;
in vec3 aColor;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uScale;
uniform float uPointSize;

out vec3 vColor;

void main() {
  vec3 centeredPosition = (aPosition - uCenter) * uScale;
  vColor = aColor;
  gl_Position = uViewProj * vec4(centeredPosition, 1.0);
  gl_PointSize = uPointSize;
}
`;

    const pointsFragmentSource = `#version 300 es
precision mediump float;
in vec3 vColor;
out vec4 outColor;

void main() {
  vec2 p = gl_PointCoord * 2.0 - 1.0;
  float r2 = dot(p, p);
  if (r2 > 1.0) {
    discard;
  }
  outColor = vec4(vColor, 0.95);
}
`;

    const objectMeshVertexSource = `#version 300 es
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUV;
in vec3 aInstancePos;
in float aInstanceYaw;
in vec2 aInstanceScaleHV;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uScale;

out vec3 vNormal;
out vec2 vUV;

vec3 applyPitchFix(vec3 value) {
  // O client DX9 aplica pitch -90deg no render padrão de mesh.
  return vec3(value.x, value.z, -value.y);
}

void main() {
  vec3 localPos = applyPitchFix(aPosition);
  localPos = vec3(localPos.x * aInstanceScaleHV.x, localPos.y * aInstanceScaleHV.y, localPos.z * aInstanceScaleHV.x);

  float c = cos(aInstanceYaw);
  float s = sin(aInstanceYaw);
  vec3 rotatedPos = vec3(
    localPos.x * c - localPos.z * s,
    localPos.y,
    localPos.x * s + localPos.z * c
  );
  vec3 worldPos = rotatedPos + aInstancePos;
  vec3 centeredPosition = (worldPos - uCenter) * uScale;

  vec3 localNormal = normalize(applyPitchFix(aNormal));
  vec3 rotatedNormal = vec3(
    localNormal.x * c - localNormal.z * s,
    localNormal.y,
    localNormal.x * s + localNormal.z * c
  );

  vNormal = rotatedNormal;
  vUV = aUV;
  gl_Position = uViewProj * vec4(centeredPosition, 1.0);
}
`;

    const objectMeshFragmentSource = `#version 300 es
precision mediump float;

in vec3 vNormal;
in vec2 vUV;
out vec4 outColor;

uniform sampler2D uTexture;
uniform float uTextureReady;
uniform vec3 uLightDir;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 lightDir = normalize(-uLightDir);
  float ndl = max(dot(normal, lightDir), 0.24);

  vec3 base = vec3(0.62, 0.67, 0.72);
  vec3 tex = texture(uTexture, vUV).rgb;
  vec3 albedo = mix(base, tex, uTextureReady);
  vec3 lit = albedo * ndl + albedo * 0.22;
  outColor = vec4(lit, 1.0);
}
`;

    this.programQuad = createProgram(gl, quadVertexSource, quadFragmentSource);
    this.programMesh = createProgram(gl, meshVertexSource, meshFragmentSource);
    this.programTerrain = createProgram(gl, terrainVertexSource, terrainFragmentSource);
    this.programPoints = createProgram(gl, pointsVertexSource, pointsFragmentSource);
    this.programObjectMesh = createProgram(gl, objectMeshVertexSource, objectMeshFragmentSource);

    this.quad.attributes.position = gl.getAttribLocation(this.programQuad, 'aPosition');
    this.quad.attributes.uv = gl.getAttribLocation(this.programQuad, 'aUV');
    this.quad.uniforms.time = gl.getUniformLocation(this.programQuad, 'uTime');
    this.quad.uniforms.textureReady = gl.getUniformLocation(this.programQuad, 'uTextureReady');
    this.quad.uniforms.textureSampler = gl.getUniformLocation(this.programQuad, 'uTexture');

    this.mesh.attributes.position = gl.getAttribLocation(this.programMesh, 'aPosition');
    this.mesh.attributes.normal = gl.getAttribLocation(this.programMesh, 'aNormal');
    this.mesh.attributes.uv = gl.getAttribLocation(this.programMesh, 'aUV');
    this.mesh.uniforms.viewProj = gl.getUniformLocation(this.programMesh, 'uViewProj');
    this.mesh.uniforms.center = gl.getUniformLocation(this.programMesh, 'uCenter');
    this.mesh.uniforms.meshScale = gl.getUniformLocation(this.programMesh, 'uMeshScale');
    this.mesh.uniforms.sceneScale = gl.getUniformLocation(this.programMesh, 'uSceneScale');
    this.mesh.uniforms.modelPos = gl.getUniformLocation(this.programMesh, 'uModelPos');
    this.mesh.uniforms.modelYaw = gl.getUniformLocation(this.programMesh, 'uModelYaw');
    this.mesh.uniforms.lightDir = gl.getUniformLocation(this.programMesh, 'uLightDir');
    this.mesh.uniforms.textureSampler = gl.getUniformLocation(this.programMesh, 'uTexture');
    this.mesh.uniforms.textureReady = gl.getUniformLocation(this.programMesh, 'uTextureReady');

    this.terrain.attributes.position = gl.getAttribLocation(this.programTerrain, 'aPosition');
    this.terrain.attributes.normal = gl.getAttribLocation(this.programTerrain, 'aNormal');
    this.terrain.attributes.color = gl.getAttribLocation(this.programTerrain, 'aColor');
    this.terrain.attributes.uv = gl.getAttribLocation(this.programTerrain, 'aUV');
    this.terrain.uniforms.viewProj = gl.getUniformLocation(this.programTerrain, 'uViewProj');
    this.terrain.uniforms.center = gl.getUniformLocation(this.programTerrain, 'uCenter');
    this.terrain.uniforms.scale = gl.getUniformLocation(this.programTerrain, 'uScale');
    this.terrain.uniforms.lightDir = gl.getUniformLocation(this.programTerrain, 'uLightDir');
    this.terrain.uniforms.textureSampler = gl.getUniformLocation(this.programTerrain, 'uTerrainTexture');
    this.terrain.uniforms.textureReady = gl.getUniformLocation(this.programTerrain, 'uTextureReady');
    this.terrain.uniforms.textureBlend = gl.getUniformLocation(this.programTerrain, 'uTextureBlend');

    this.objectPoints.attributes.position = gl.getAttribLocation(this.programPoints, 'aPosition');
    this.objectPoints.attributes.color = gl.getAttribLocation(this.programPoints, 'aColor');
    this.objectPoints.uniforms.viewProj = gl.getUniformLocation(this.programPoints, 'uViewProj');
    this.objectPoints.uniforms.center = gl.getUniformLocation(this.programPoints, 'uCenter');
    this.objectPoints.uniforms.scale = gl.getUniformLocation(this.programPoints, 'uScale');
    this.objectPoints.uniforms.pointSize = gl.getUniformLocation(this.programPoints, 'uPointSize');

    this.objectMeshes.attributes.position = gl.getAttribLocation(this.programObjectMesh, 'aPosition');
    this.objectMeshes.attributes.normal = gl.getAttribLocation(this.programObjectMesh, 'aNormal');
    this.objectMeshes.attributes.uv = gl.getAttribLocation(this.programObjectMesh, 'aUV');
    this.objectMeshes.attributes.instancePos = gl.getAttribLocation(this.programObjectMesh, 'aInstancePos');
    this.objectMeshes.attributes.instanceYaw = gl.getAttribLocation(this.programObjectMesh, 'aInstanceYaw');
    this.objectMeshes.attributes.instanceScaleHV = gl.getAttribLocation(this.programObjectMesh, 'aInstanceScaleHV');
    this.objectMeshes.uniforms.viewProj = gl.getUniformLocation(this.programObjectMesh, 'uViewProj');
    this.objectMeshes.uniforms.center = gl.getUniformLocation(this.programObjectMesh, 'uCenter');
    this.objectMeshes.uniforms.scale = gl.getUniformLocation(this.programObjectMesh, 'uScale');
    this.objectMeshes.uniforms.lightDir = gl.getUniformLocation(this.programObjectMesh, 'uLightDir');
    this.objectMeshes.uniforms.textureSampler = gl.getUniformLocation(this.programObjectMesh, 'uTexture');
    this.objectMeshes.uniforms.textureReady = gl.getUniformLocation(this.programObjectMesh, 'uTextureReady');

    const quadVertices = new Float32Array([
      -1.0, -1.0, 0.0, 0.0,
      1.0, -1.0, 1.0, 0.0,
      -1.0, 1.0, 0.0, 1.0,
      -1.0, 1.0, 0.0, 1.0,
      1.0, -1.0, 1.0, 0.0,
      1.0, 1.0, 1.0, 1.0,
    ]);

    this.quad.vertexBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad.vertexBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, quadVertices, gl.STATIC_DRAW);

    this.texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      1,
      1,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      new Uint8Array([20, 30, 40, 255]),
    );

    this.terrainTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.terrainTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      1,
      1,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      new Uint8Array([34, 52, 40, 255]),
    );
  }

  async loadTextureFromUrl(url) {
    const image = await new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error(`Falha ao carregar textura: ${url}`));
      img.src = url;
    });

    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    this.textureReady = true;
  }

  loadTextureFromPixels(width, height, rgbaBytes) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      width,
      height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      rgbaBytes,
    );
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    this.textureReady = true;
  }

  setTerrainTextureFromPixels(width, height, rgbaBytes) {
    if (!this.terrainTexture) {
      return;
    }

    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.terrainTexture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      width,
      height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      rgbaBytes,
    );
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    this.terrainTextureReady = true;
  }

  clearTerrainTexture() {
    if (!this.terrainTexture) {
      return;
    }

    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.terrainTexture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      1,
      1,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      new Uint8Array([34, 52, 40, 255]),
    );
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    this.terrainTextureReady = false;
  }

  loadCompressedDdsTexture(dds, options = {}) {
    const target = String(options.target || 'mesh').toLowerCase();
    const targetTexture = target === 'terrain' ? this.terrainTexture : this.texture;
    if (!targetTexture) {
      return { ok: false, reason: 'textura alvo indisponível' };
    }

    const gl = this.gl;
    const ext = this.s3tc
      || gl.getExtension('WEBGL_compressed_texture_s3tc')
      || gl.getExtension('WEBKIT_WEBGL_compressed_texture_s3tc')
      || gl.getExtension('MOZ_WEBGL_compressed_texture_s3tc');

    this.s3tc = ext;
    if (!ext) {
      return { ok: false, reason: 'S3TC não suportado neste navegador/GPU' };
    }

    let format;
    let blockBytes;
    if (dds.fourCC === 'DXT1') {
      format = ext.COMPRESSED_RGBA_S3TC_DXT1_EXT;
      blockBytes = 8;
    } else if (dds.fourCC === 'DXT3') {
      format = ext.COMPRESSED_RGBA_S3TC_DXT3_EXT;
      blockBytes = 16;
    } else {
      return { ok: false, reason: `fourCC não suportado: ${dds.fourCC}` };
    }

    gl.bindTexture(gl.TEXTURE_2D, targetTexture);

    let offset = 128;
    let uploadedLevels = 0;
    for (let level = 0; level < dds.mipMapCount; level += 1) {
      const w = Math.max(1, dds.width >> level);
      const h = Math.max(1, dds.height >> level);
      const blockWidth = Math.max(1, Math.ceil(w / 4));
      const blockHeight = Math.max(1, Math.ceil(h / 4));
      const levelSize = blockWidth * blockHeight * blockBytes;

      if (offset + levelSize > dds.bytes.length) {
        break;
      }

      const levelData = dds.bytes.subarray(offset, offset + levelSize);
      gl.compressedTexImage2D(gl.TEXTURE_2D, level, format, w, h, 0, levelData);
      offset += levelSize;
      uploadedLevels += 1;
    }

    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);

    if (target === 'terrain') {
      this.terrainTextureReady = uploadedLevels > 0;
    } else {
      this.textureReady = uploadedLevels > 0;
    }

    if (uploadedLevels <= 0) {
      return { ok: false, uploadedLevels: 0, reason: 'Nenhum mip level DDS carregado' };
    }

    return { ok: true, uploadedLevels };
  }

  setMeshGeometry(meshData) {
    const gl = this.gl;

    if (this.mesh.positionBuffer) {
      gl.deleteBuffer(this.mesh.positionBuffer);
    }
    if (this.mesh.normalBuffer) {
      gl.deleteBuffer(this.mesh.normalBuffer);
    }
    if (this.mesh.uvBuffer) {
      gl.deleteBuffer(this.mesh.uvBuffer);
    }
    if (this.mesh.indexBuffer) {
      gl.deleteBuffer(this.mesh.indexBuffer);
    }

    const positionBuffer = gl.createBuffer();
    const normalBuffer = gl.createBuffer();
    const uvBuffer = gl.createBuffer();
    const indexBuffer = gl.createBuffer();

    if (!positionBuffer || !normalBuffer || !uvBuffer || !indexBuffer) {
      throw new Error('Falha ao criar buffers de mesh');
    }

    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, meshData.positions, gl.DYNAMIC_DRAW);

    gl.bindBuffer(gl.ARRAY_BUFFER, normalBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, meshData.normals, gl.DYNAMIC_DRAW);

    gl.bindBuffer(gl.ARRAY_BUFFER, uvBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, meshData.uvs, gl.STATIC_DRAW);

    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, meshData.indices, gl.STATIC_DRAW);

    this.mesh.positionBuffer = positionBuffer;
    this.mesh.normalBuffer = normalBuffer;
    this.mesh.uvBuffer = uvBuffer;
    this.mesh.indexBuffer = indexBuffer;
    this.mesh.indexCount = meshData.indices.length;
    this.mesh.vertexCount = meshData.header.vertexCount;
    this.mesh.center = meshData.bounds.center;
    this.mesh.radius = meshData.bounds.radius;
    this.mesh.drawScale = Math.min(128, Math.max(0.25, 2.5 / this.mesh.radius));
    this.mesh.dynamic = true;
    this.mesh.ready = true;
  }

  updateMeshDeformation(positions, normals) {
    if (!this.mesh.ready || !this.mesh.dynamic) {
      return;
    }

    if (!this.mesh.positionBuffer || !this.mesh.normalBuffer) {
      return;
    }

    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.mesh.positionBuffer);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, positions);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.mesh.normalBuffer);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, normals);
  }

  clearTerrain() {
    const gl = this.gl;
    if (this.terrain.positionBuffer) {
      gl.deleteBuffer(this.terrain.positionBuffer);
      this.terrain.positionBuffer = null;
    }
    if (this.terrain.normalBuffer) {
      gl.deleteBuffer(this.terrain.normalBuffer);
      this.terrain.normalBuffer = null;
    }
    if (this.terrain.colorBuffer) {
      gl.deleteBuffer(this.terrain.colorBuffer);
      this.terrain.colorBuffer = null;
    }
    if (this.terrain.uvBuffer) {
      gl.deleteBuffer(this.terrain.uvBuffer);
      this.terrain.uvBuffer = null;
    }
    if (this.terrain.indexBuffer) {
      gl.deleteBuffer(this.terrain.indexBuffer);
      this.terrain.indexBuffer = null;
    }
    this.terrain.indexCount = 0;
    this.terrain.ready = false;
  }

  setTerrainVisible(visible) {
    this.terrain.visible = Boolean(visible);
  }

  setTerrainTextureVisible(visible) {
    this.terrain.textureVisible = Boolean(visible);
  }

  setTerrainTextureBlend(blend) {
    if (!Number.isFinite(blend)) {
      return;
    }
    this.terrain.textureBlend = Math.max(0.0, Math.min(1.0, blend));
  }

  setTerrainScale(scale) {
    if (!Number.isFinite(scale)) {
      return;
    }
    this.terrain.scale = Math.max(0.01, Math.min(0.35, scale));
    this.objectPoints.scale = this.terrain.scale;
    this.objectMeshes.scale = this.terrain.scale;
  }

  setMeshSceneScale(scale) {
    if (!Number.isFinite(scale)) {
      return;
    }
    this.mesh.sceneScale = Math.max(0.001, Math.min(1.0, scale));
  }

  setMeshWorldTransform(options = {}) {
    this.mesh.world.enabled = Boolean(options.enabled);
    if (!this.mesh.world.enabled) {
      this.mesh.world.position[0] = 0;
      this.mesh.world.position[1] = 0;
      this.mesh.world.position[2] = 0;
      this.mesh.world.yaw = 0;
      return;
    }

    this.mesh.world.position[0] = Number.isFinite(options.x) ? Number(options.x) : this.mesh.world.position[0];
    this.mesh.world.position[1] = Number.isFinite(options.y) ? Number(options.y) : this.mesh.world.position[1];
    this.mesh.world.position[2] = Number.isFinite(options.z) ? Number(options.z) : this.mesh.world.position[2];
    this.mesh.world.yaw = Number.isFinite(options.yaw) ? Number(options.yaw) : this.mesh.world.yaw;
  }

  setCameraTarget(target, enabled = true) {
    this.camera.targetEnabled = Boolean(enabled);
    if (!this.camera.targetEnabled || !Array.isArray(target) || target.length < 3) {
      this.camera.target = [0, 0, 0];
      return;
    }

    this.camera.target = [
      Number(target[0] || 0),
      Number(target[1] || 0),
      Number(target[2] || 0),
    ];
  }

  setCameraMode(mode) {
    const normalized = String(mode || '').toLowerCase();
    this.camera.mode = normalized === 'follow' ? 'follow' : 'orbit';
  }

  setCameraFollowYaw(yaw) {
    if (!Number.isFinite(yaw)) {
      return;
    }
    this.camera.followYaw = Number(yaw);
  }

  setCameraFollowOptions(options = {}) {
    if (Number.isFinite(options.distance)) {
      this.camera.followDistance = Math.max(2.8, Math.min(18.0, Number(options.distance)));
    }
    if (Number.isFinite(options.height)) {
      this.camera.followHeight = Math.max(1.2, Math.min(9.0, Number(options.height)));
    }
    if (Number.isFinite(options.lead)) {
      this.camera.followLead = Math.max(0.0, Math.min(2.0, Number(options.lead)));
    }
  }

  setTerrainHeightGrid(heightGrid, options = {}) {
    if (!Array.isArray(heightGrid) || heightGrid.length === 0) {
      this.clearTerrain();
      return;
    }

    const sizeY = heightGrid.length;
    const sizeX = Array.isArray(heightGrid[0]) ? heightGrid[0].length : 0;
    if (sizeX <= 1 || sizeY <= 1) {
      this.clearTerrain();
      return;
    }

    const gl = this.gl;
    const heightUnit = Number.isFinite(options.heightUnit) ? options.heightUnit : 0.1;
    const centerX = (sizeX - 1) * 0.5;
    const centerY = (sizeY - 1) * 0.5;
    const vertexCount = sizeX * sizeY;

    const heights = new Float32Array(vertexCount);
    let minHeight = Infinity;
    let maxHeight = -Infinity;
    for (let y = 0; y < sizeY; y += 1) {
      const row = heightGrid[y] || [];
      for (let x = 0; x < sizeX; x += 1) {
        const h = Number(row[x] ?? 0);
        const index = y * sizeX + x;
        heights[index] = h;
        minHeight = Math.min(minHeight, h);
        maxHeight = Math.max(maxHeight, h);
      }
    }

    const heightRange = Math.max(1.0, maxHeight - minHeight);
    const positions = new Float32Array(vertexCount * 3);
    const normals = new Float32Array(vertexCount * 3);
    const colors = new Float32Array(vertexCount * 3);
    const uvs = new Float32Array(vertexCount * 2);

    const getHeight = (x, y) => {
      const cx = Math.max(0, Math.min(sizeX - 1, x));
      const cy = Math.max(0, Math.min(sizeY - 1, y));
      return heights[cy * sizeX + cx];
    };

    for (let y = 0; y < sizeY; y += 1) {
      for (let x = 0; x < sizeX; x += 1) {
        const index = y * sizeX + x;
        const posOffset = index * 3;
        const h = heights[index];

        positions[posOffset + 0] = x - centerX;
        positions[posOffset + 1] = h * heightUnit;
        positions[posOffset + 2] = y - centerY;

        const hx = getHeight(x + 1, y) - getHeight(x - 1, y);
        const hy = getHeight(x, y + 1) - getHeight(x, y - 1);
        const nx = -hx * heightUnit;
        const ny = 2.0;
        const nz = -hy * heightUnit;
        const nLen = Math.hypot(nx, ny, nz) || 1.0;
        normals[posOffset + 0] = nx / nLen;
        normals[posOffset + 1] = ny / nLen;
        normals[posOffset + 2] = nz / nLen;

        const t = (h - minHeight) / heightRange;
        colors[posOffset + 0] = 0.12 + (t * 0.52);
        colors[posOffset + 1] = 0.22 + (t * 0.58);
        colors[posOffset + 2] = 0.16 + (t * 0.28);

        const uvOffset = index * 2;
        uvs[uvOffset + 0] = x / (sizeX - 1);
        uvs[uvOffset + 1] = y / (sizeY - 1);
      }
    }

    const quadCount = (sizeX - 1) * (sizeY - 1);
    const indices = new Uint32Array(quadCount * 6);
    let i = 0;
    for (let y = 0; y < sizeY - 1; y += 1) {
      for (let x = 0; x < sizeX - 1; x += 1) {
        const a = y * sizeX + x;
        const b = a + 1;
        const c = a + sizeX;
        const d = c + 1;
        indices[i + 0] = a;
        indices[i + 1] = c;
        indices[i + 2] = b;
        indices[i + 3] = b;
        indices[i + 4] = c;
        indices[i + 5] = d;
        i += 6;
      }
    }

    if (this.terrain.positionBuffer) {
      gl.deleteBuffer(this.terrain.positionBuffer);
    }
    if (this.terrain.normalBuffer) {
      gl.deleteBuffer(this.terrain.normalBuffer);
    }
    if (this.terrain.colorBuffer) {
      gl.deleteBuffer(this.terrain.colorBuffer);
    }
    if (this.terrain.uvBuffer) {
      gl.deleteBuffer(this.terrain.uvBuffer);
    }
    if (this.terrain.indexBuffer) {
      gl.deleteBuffer(this.terrain.indexBuffer);
    }

    this.terrain.positionBuffer = gl.createBuffer();
    this.terrain.normalBuffer = gl.createBuffer();
    this.terrain.colorBuffer = gl.createBuffer();
    this.terrain.uvBuffer = gl.createBuffer();
    this.terrain.indexBuffer = gl.createBuffer();
    if (
      !this.terrain.positionBuffer
      || !this.terrain.normalBuffer
      || !this.terrain.colorBuffer
      || !this.terrain.uvBuffer
      || !this.terrain.indexBuffer
    ) {
      throw new Error('Falha ao criar buffers de terreno');
    }

    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.positionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.normalBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, normals, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.colorBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, colors, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.uvBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, uvs, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.terrain.indexBuffer);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

    this.terrain.indexCount = indices.length;
    this.terrain.center = [0, ((minHeight + maxHeight) * heightUnit) * 0.5, 0];
    const radiusXZ = Math.hypot(centerX, centerY);
    const radiusY = ((maxHeight - minHeight) * heightUnit) * 0.5;
    this.terrain.radius = Math.max(1, Math.hypot(radiusXZ, radiusY));
    if (Number.isFinite(options.terrainScale)) {
      this.setTerrainScale(options.terrainScale);
    }
    this.terrain.ready = true;
  }

  clearFieldObjectPoints() {
    const gl = this.gl;
    if (this.objectPoints.positionBuffer) {
      gl.deleteBuffer(this.objectPoints.positionBuffer);
      this.objectPoints.positionBuffer = null;
    }
    if (this.objectPoints.colorBuffer) {
      gl.deleteBuffer(this.objectPoints.colorBuffer);
      this.objectPoints.colorBuffer = null;
    }
    this.objectPoints.count = 0;
    this.objectPoints.positions = null;
    this.objectPoints.baseColors = null;
    this.objectPoints.items = [];
    this.objectPoints.selectedIndex = -1;
    this.objectPoints.ready = false;
  }

  setFieldObjectPointsVisible(visible) {
    this.objectPoints.visible = Boolean(visible);
  }

  setFieldObjectPointSize(size) {
    if (!Number.isFinite(size)) {
      return;
    }
    this.objectPoints.pointSize = Math.max(1.0, Math.min(12.0, size));
  }

  setFieldObjectPoints(items, options = {}) {
    if (!Array.isArray(items) || items.length === 0) {
      this.clearFieldObjectPoints();
      return;
    }

    const limit = Math.max(0, Math.min(items.length, Number.isFinite(options.limit) ? options.limit : 2200));
    if (limit === 0) {
      this.clearFieldObjectPoints();
      return;
    }

    const positions = new Float32Array(limit * 3);
    const colors = new Float32Array(limit * 3);
    const mappedItems = new Array(limit);
    for (let i = 0; i < limit; i += 1) {
      const item = items[i] || {};
      const x = Number(item.x ?? 0) * 0.5 - 31.5;
      const y = Number(item.height ?? 0) * 0.1 + 0.3;
      const z = Number(item.y ?? 0) * 0.5 - 31.5;
      const objType = Number(item.objType ?? 0);

      const p = i * 3;
      positions[p + 0] = x;
      positions[p + 1] = y;
      positions[p + 2] = z;
      colors[p + 0] = ((objType * 73) % 255) / 255;
      colors[p + 1] = ((objType * 151) % 255) / 255;
      colors[p + 2] = ((objType * 197) % 255) / 255;

      mappedItems[i] = {
        sourceIndex: i,
        objType,
        x: Number(item.x ?? 0),
        y: Number(item.y ?? 0),
        height: Number(item.height ?? 0),
        angle: Number(item.angle ?? 0),
        textureSetIndex: Number(item.textureSetIndex ?? 0),
        maskIndex: Number(item.maskIndex ?? 0),
        scaleH: Number(item.scaleH ?? 1),
        scaleV: Number(item.scaleV ?? 1),
        hasScale: Boolean(item.hasScale),
        worldX: x,
        worldY: y,
        worldZ: z,
      };
    }

    const gl = this.gl;
    if (this.objectPoints.positionBuffer) {
      gl.deleteBuffer(this.objectPoints.positionBuffer);
    }
    if (this.objectPoints.colorBuffer) {
      gl.deleteBuffer(this.objectPoints.colorBuffer);
    }

    this.objectPoints.positionBuffer = gl.createBuffer();
    this.objectPoints.colorBuffer = gl.createBuffer();
    if (!this.objectPoints.positionBuffer || !this.objectPoints.colorBuffer) {
      throw new Error('Falha ao criar buffers de pontos de objetos');
    }

    gl.bindBuffer(gl.ARRAY_BUFFER, this.objectPoints.positionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.objectPoints.colorBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, colors, gl.STATIC_DRAW);

    this.objectPoints.count = limit;
    this.objectPoints.positions = positions;
    this.objectPoints.baseColors = colors;
    this.objectPoints.items = mappedItems;
    this.objectPoints.selectedIndex = -1;
    if (Number.isFinite(options.pointSize)) {
      this.setFieldObjectPointSize(options.pointSize);
    }
    this.objectPoints.ready = true;
  }

  clearFieldObjectMeshes() {
    const gl = this.gl;
    for (const group of this.objectMeshes.groups) {
      if (group.positionBuffer) {
        gl.deleteBuffer(group.positionBuffer);
      }
      if (group.normalBuffer) {
        gl.deleteBuffer(group.normalBuffer);
      }
      if (group.uvBuffer) {
        gl.deleteBuffer(group.uvBuffer);
      }
      if (group.indexBuffer) {
        gl.deleteBuffer(group.indexBuffer);
      }
      if (group.instanceBuffer) {
        gl.deleteBuffer(group.instanceBuffer);
      }
      if (Array.isArray(group.materials)) {
        for (const material of group.materials) {
          if (material?.texture) {
            gl.deleteTexture(material.texture);
          }
        }
      }
    }

    this.objectMeshes.groups = [];
    this.objectMeshes.center = [0, 0, 0];
    this.objectMeshes.radius = 2;
    this.objectMeshes.drawStats = {
      groupCount: 0,
      materialCount: 0,
      instanceCount: 0,
      vertexCount: 0,
      triangleCount: 0,
    };
    this.objectMeshes.ready = false;
  }

  setFieldObjectMeshesVisible(visible) {
    this.objectMeshes.visible = Boolean(visible);
  }

  getFieldObjectMeshInfo() {
    return {
      ready: this.objectMeshes.ready,
      visible: this.objectMeshes.visible,
      scale: this.objectMeshes.scale,
      radius: this.objectMeshes.radius,
      maxInstances: this.objectMeshes.maxInstances,
      groupCapacity: this.objectMeshes.groups.length,
      ...this.objectMeshes.drawStats,
    };
  }

  _createObjectMeshTexture(texturePayload, fallbackColor) {
    const gl = this.gl;
    const texture = gl.createTexture();
    if (!texture) {
      throw new Error('Falha ao criar textura de object mesh');
    }

    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);

    const fallback = () => {
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA,
        1,
        1,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        new Uint8Array(fallbackColor),
      );
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      return {
        texture,
        textureReady: false,
        width: 1,
        height: 1,
      };
    };

    if (!texturePayload || !texturePayload.kind) {
      return fallback();
    }

    if (texturePayload.kind === 'wyt' && texturePayload.width > 0 && texturePayload.height > 0 && texturePayload.rgba) {
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA,
        texturePayload.width,
        texturePayload.height,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        texturePayload.rgba,
      );
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      return {
        texture,
        textureReady: true,
        width: texturePayload.width,
        height: texturePayload.height,
      };
    }

    if (texturePayload.kind === 'wys' && texturePayload.dds) {
      const dds = texturePayload.dds;
      const ext = this.s3tc
        || gl.getExtension('WEBGL_compressed_texture_s3tc')
        || gl.getExtension('WEBKIT_WEBGL_compressed_texture_s3tc')
        || gl.getExtension('MOZ_WEBGL_compressed_texture_s3tc');
      this.s3tc = ext;

      if (!ext) {
        return fallback();
      }

      let format = 0;
      let blockBytes = 0;
      if (dds.fourCC === 'DXT1') {
        format = ext.COMPRESSED_RGBA_S3TC_DXT1_EXT;
        blockBytes = 8;
      } else if (dds.fourCC === 'DXT3') {
        format = ext.COMPRESSED_RGBA_S3TC_DXT3_EXT;
        blockBytes = 16;
      } else {
        return fallback();
      }

      let offset = 128;
      let uploadedLevels = 0;
      for (let level = 0; level < dds.mipMapCount; level += 1) {
        const w = Math.max(1, dds.width >> level);
        const h = Math.max(1, dds.height >> level);
        const blockWidth = Math.max(1, Math.ceil(w / 4));
        const blockHeight = Math.max(1, Math.ceil(h / 4));
        const levelSize = blockWidth * blockHeight * blockBytes;
        if (offset + levelSize > dds.bytes.length) {
          break;
        }

        const levelData = dds.bytes.subarray(offset, offset + levelSize);
        gl.compressedTexImage2D(gl.TEXTURE_2D, level, format, w, h, 0, levelData);
        offset += levelSize;
        uploadedLevels += 1;
      }

      if (uploadedLevels <= 0) {
        return fallback();
      }

      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      return {
        texture,
        textureReady: true,
        width: dds.width,
        height: dds.height,
      };
    }

    return fallback();
  }

  setFieldObjectMeshes(groups, options = {}) {
    if (!Array.isArray(groups) || groups.length === 0) {
      this.clearFieldObjectMeshes();
      return;
    }

    this.clearFieldObjectMeshes();
    const gl = this.gl;
    const nextGroups = [];

    let instanceTotal = 0;
    let materialTotal = 0;
    let vertexTotal = 0;
    let triangleTotal = 0;
    let sceneRadius = 2.0;

    for (const groupInput of groups) {
      const geometry = groupInput?.geometry;
      const instances = Array.isArray(groupInput?.instances) ? groupInput.instances : [];
      if (!geometry || !geometry.positions || !geometry.normals || !geometry.uvs || !geometry.indices || instances.length === 0) {
        continue;
      }

      const positionBuffer = gl.createBuffer();
      const normalBuffer = gl.createBuffer();
      const uvBuffer = gl.createBuffer();
      const indexBuffer = gl.createBuffer();
      const instanceBuffer = gl.createBuffer();
      if (!positionBuffer || !normalBuffer || !uvBuffer || !indexBuffer || !instanceBuffer) {
        throw new Error('Falha ao criar buffers de object mesh');
      }

      gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, geometry.positions, gl.STATIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, normalBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, geometry.normals, gl.STATIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, uvBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, geometry.uvs, gl.STATIC_DRAW);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geometry.indices, gl.STATIC_DRAW);

      const instanceData = new Float32Array(instances.length * 6);
      let instanceOffset = 0;
      let instanceRadius = 0;
      for (const instance of instances) {
        const x = Number(instance.x ?? 0);
        const y = Number(instance.y ?? 0);
        const z = Number(instance.z ?? 0);
        const yaw = Number(instance.yaw ?? 0);
        const scaleH = Number.isFinite(instance.scaleH) ? Number(instance.scaleH) : 1.0;
        const scaleV = Number.isFinite(instance.scaleV) ? Number(instance.scaleV) : 1.0;
        instanceData[instanceOffset + 0] = x;
        instanceData[instanceOffset + 1] = y;
        instanceData[instanceOffset + 2] = z;
        instanceData[instanceOffset + 3] = yaw;
        instanceData[instanceOffset + 4] = scaleH;
        instanceData[instanceOffset + 5] = scaleV;
        instanceOffset += 6;

        const geometryRadius = Number(groupInput?.geometry?.bounds?.radius ?? 1.0);
        const instanceScale = Math.max(Math.abs(scaleH), Math.abs(scaleV), 0.1);
        instanceRadius = Math.max(instanceRadius, Math.hypot(x, y, z) + (geometryRadius * instanceScale));
      }

      gl.bindBuffer(gl.ARRAY_BUFFER, instanceBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, instanceData, gl.STATIC_DRAW);

      const materialInputs = Array.isArray(groupInput.materials) && groupInput.materials.length > 0
        ? groupInput.materials
        : [
          {
            faceStart: 0,
            faceCount: Math.floor(geometry.indices.length / 3),
            texturePath: String(groupInput.texturePath || ''),
            texturePayload: groupInput.texturePayload || null,
          },
        ];

      const materials = [];
      for (let materialIndex = 0; materialIndex < materialInputs.length; materialIndex += 1) {
        const input = materialInputs[materialIndex] || {};
        const faceStart = Math.max(0, Math.floor(Number(input.faceStart ?? 0)));
        const faceCount = Math.max(0, Math.floor(Number(input.faceCount ?? 0)));
        if (faceCount <= 0) {
          continue;
        }

        const startIndex = faceStart * 3;
        if (startIndex >= geometry.indices.length) {
          continue;
        }

        const requestedIndexCount = faceCount * 3;
        const indexCount = Math.min(requestedIndexCount, geometry.indices.length - startIndex);
        if (indexCount <= 0) {
          continue;
        }

        const fallbackColor = [
          ((Number(groupInput.objType || 0) + materialIndex * 17) * 73) % 255,
          ((Number(groupInput.objType || 0) + materialIndex * 19) * 151) % 255,
          ((Number(groupInput.objType || 0) + materialIndex * 23) * 197) % 255,
          255,
        ];
        const textureInfo = this._createObjectMeshTexture(input.texturePayload || null, fallbackColor);
        materials.push({
          texturePath: String(input.texturePath || ''),
          texture: textureInfo.texture,
          textureReady: textureInfo.textureReady,
          indexOffsetBytes: startIndex * 2,
          indexCount,
        });
      }

      if (materials.length <= 0) {
        const fallbackColor = [
          (Number(groupInput.objType || 0) * 73) % 255,
          (Number(groupInput.objType || 0) * 151) % 255,
          (Number(groupInput.objType || 0) * 197) % 255,
          255,
        ];
        const textureInfo = this._createObjectMeshTexture(groupInput.texturePayload || null, fallbackColor);
        materials.push({
          texturePath: String(groupInput.texturePath || ''),
          texture: textureInfo.texture,
          textureReady: textureInfo.textureReady,
          indexOffsetBytes: 0,
          indexCount: geometry.indices.length,
        });
      }

      nextGroups.push({
        objType: Number(groupInput.objType || 0),
        meshPath: String(groupInput.meshPath || ''),
        positionBuffer,
        normalBuffer,
        uvBuffer,
        indexBuffer,
        instanceBuffer,
        vertexCount: geometry.positions.length / 3,
        instanceCount: instances.length,
        materials,
      });

      instanceTotal += instances.length;
      materialTotal += materials.length;
      vertexTotal += geometry.positions.length / 3;
      for (const material of materials) {
        triangleTotal += Math.floor(material.indexCount / 3);
      }
      sceneRadius = Math.max(sceneRadius, instanceRadius);
    }

    if (nextGroups.length <= 0) {
      this.clearFieldObjectMeshes();
      return;
    }

    if (Number.isFinite(options.maxInstances)) {
      this.objectMeshes.maxInstances = Math.max(64, Math.min(4096, Math.floor(options.maxInstances)));
    }
    if (Number.isFinite(options.scale)) {
      this.objectMeshes.scale = Math.max(0.01, Math.min(0.35, Number(options.scale)));
    }

    this.objectMeshes.groups = nextGroups;
    this.objectMeshes.center = [0, 0, 0];
    this.objectMeshes.radius = sceneRadius;
    this.objectMeshes.drawStats = {
      groupCount: nextGroups.length,
      materialCount: materialTotal,
      instanceCount: instanceTotal,
      vertexCount: vertexTotal,
      triangleCount: triangleTotal,
    };
    this.objectMeshes.ready = true;
  }

  setSelectedFieldObjectIndex(index) {
    if (!this.objectPoints.ready || !this.objectPoints.colorBuffer || !this.objectPoints.baseColors) {
      this.objectPoints.selectedIndex = -1;
      return;
    }

    let next = Number.isFinite(index) ? Math.floor(index) : -1;
    if (next < 0 || next >= this.objectPoints.count) {
      next = -1;
    }
    if (next === this.objectPoints.selectedIndex) {
      return;
    }

    const colors = this.objectPoints.baseColors.slice();
    if (next >= 0) {
      const p = next * 3;
      colors[p + 0] = 1.0;
      colors[p + 1] = 0.92;
      colors[p + 2] = 0.18;
    }

    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.objectPoints.colorBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, colors, gl.STATIC_DRAW);
    this.objectPoints.selectedIndex = next;
  }

  pickFieldObjectAtCanvas(canvasX, canvasY, options = {}) {
    if (!this.objectPoints.ready || !this.objectPoints.positions || this.objectPoints.count <= 0) {
      return null;
    }
    if (!this.sceneState.hasViewProj) {
      return null;
    }

    const pixelRadius = Math.max(2, Number.isFinite(options.pixelRadius) ? options.pixelRadius : 14);
    const radius2 = pixelRadius * pixelRadius;
    const clip = [0, 0, 0, 1];

    let bestIndex = -1;
    let bestDistance2 = Infinity;
    let bestDepth = Infinity;
    let bestScreenX = 0;
    let bestScreenY = 0;

    const positions = this.objectPoints.positions;
    for (let i = 0; i < this.objectPoints.count; i += 1) {
      const p = i * 3;
      const projection = this._projectScenePointToCanvas(
        positions[p + 0],
        positions[p + 1],
        positions[p + 2],
        clip,
      );
      if (!projection) {
        continue;
      }

      const dx = projection.x - canvasX;
      const dy = projection.y - canvasY;
      const dist2 = (dx * dx) + (dy * dy);
      if (dist2 > radius2) {
        continue;
      }

      if (dist2 < bestDistance2 || (Math.abs(dist2 - bestDistance2) < 1e-3 && projection.depth < bestDepth)) {
        bestIndex = i;
        bestDistance2 = dist2;
        bestDepth = projection.depth;
        bestScreenX = projection.x;
        bestScreenY = projection.y;
      }
    }

    if (bestIndex < 0) {
      return null;
    }

    return {
      index: bestIndex,
      item: this.objectPoints.items[bestIndex] || null,
      screenX: bestScreenX,
      screenY: bestScreenY,
      distancePx: Math.sqrt(bestDistance2),
      depthNdc: bestDepth,
    };
  }

  _projectScenePointToCanvas(x, y, z, clipScratch) {
    const center = this.objectPoints.center;
    const scale = this.objectPoints.scale;
    mat4TransformPoint(
      clipScratch,
      this.sceneState.viewProj,
      (x - center[0]) * scale,
      (y - center[1]) * scale,
      (z - center[2]) * scale,
      1.0,
    );

    const w = clipScratch[3];
    if (w <= 1e-6) {
      return null;
    }

    const invW = 1.0 / w;
    const ndcX = clipScratch[0] * invW;
    const ndcY = clipScratch[1] * invW;
    const ndcZ = clipScratch[2] * invW;
    if (ndcX < -1.1 || ndcX > 1.1 || ndcY < -1.1 || ndcY > 1.1 || ndcZ < -1.0 || ndcZ > 1.0) {
      return null;
    }

    const viewportWidth = this.sceneState.viewportWidth;
    const viewportHeight = this.sceneState.viewportHeight;
    const xPx = (ndcX * 0.5 + 0.5) * viewportWidth;
    const yPx = (1.0 - (ndcY * 0.5 + 0.5)) * viewportHeight;
    return { x: xPx, y: yPx, depth: ndcZ };
  }

  start() {
    const frame = () => {
      this.render();
      requestAnimationFrame(frame);
    };

    requestAnimationFrame(frame);
  }

  render() {
    const gl = this.gl;
    this._resizeToDisplaySize();

    const elapsed = (performance.now() - this.startTime) / 1000.0;

    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(
      0.06 + 0.03 * Math.sin(elapsed * 0.8),
      0.09 + 0.02 * Math.cos(elapsed * 0.9),
      0.13 + 0.02 * Math.sin(elapsed * 0.5),
      1.0,
    );
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const viewProj = this._buildSceneViewProj(elapsed);
    this.sceneState.viewProj.set(viewProj);
    this.sceneState.viewportWidth = this.canvas.width;
    this.sceneState.viewportHeight = this.canvas.height;
    this.sceneState.hasViewProj = true;

    let any3d = false;
    any3d = this._drawTerrain(viewProj) || any3d;
    any3d = this._drawObjectMeshes(viewProj) || any3d;
    any3d = this._drawMesh(viewProj) || any3d;
    any3d = this._drawObjectPoints(viewProj) || any3d;

    if (!any3d) {
      this._drawQuad(elapsed);
    }
  }

  _buildSceneViewProj(elapsed) {
    const aspect = Math.max(1e-6, this.canvas.width / this.canvas.height);
    const target = this.camera.targetEnabled ? this.camera.target : [0, 0, 0];
    let sceneRadius = 2.5;
    if (this.mesh.ready) {
      sceneRadius = Math.max(sceneRadius, this.mesh.radius * this.mesh.drawScale);
    }
    if (this.terrain.ready && this.terrain.visible) {
      sceneRadius = Math.max(sceneRadius, this.terrain.radius * this.terrain.scale);
    }
    if (this.objectPoints.ready && this.objectPoints.visible && this.terrain.ready) {
      sceneRadius = Math.max(sceneRadius, this.terrain.radius * this.objectPoints.scale);
    }
    if (this.objectMeshes.ready && this.objectMeshes.visible) {
      sceneRadius = Math.max(sceneRadius, this.objectMeshes.radius * this.objectMeshes.scale);
    }

    const projection = new Float32Array(16);
    const view = new Float32Array(16);
    const viewProj = new Float32Array(16);

    if (this.camera.mode === 'follow' && this.camera.targetEnabled) {
      const yaw = this.camera.followYaw;
      const distance = Math.max(2.8, Math.min(18.0, this.camera.followDistance));
      const eye = [
        target[0] - (Math.sin(yaw) * distance),
        target[1] + this.camera.followHeight,
        target[2] - (Math.cos(yaw) * distance),
      ];
      const lookTarget = [
        target[0] + (Math.sin(yaw) * this.camera.followLead),
        target[1] + 0.2,
        target[2] + (Math.cos(yaw) * this.camera.followLead),
      ];
      const near = Math.max(0.01, distance / 180.0);
      const far = Math.max(120.0, (sceneRadius * 24.0) + (distance * 12.0));
      mat4Perspective(projection, Math.PI / 3.0, aspect, near, far);
      mat4LookAt(view, eye, lookTarget, [0, 1, 0]);
    } else {
      const distance = Math.max(4.0, sceneRadius * 3.1);
      const orbitAngle = elapsed * 0.3;
      const eye = [
        target[0] + (Math.cos(orbitAngle) * distance),
        target[1] + (distance * 0.62),
        target[2] + (Math.sin(orbitAngle) * distance),
      ];
      const near = Math.max(0.01, distance / 300.0);
      const far = Math.max(120.0, distance * 14.0);
      mat4Perspective(projection, Math.PI / 3.0, aspect, near, far);
      mat4LookAt(view, eye, target, [0, 1, 0]);
    }
    mat4Multiply(viewProj, projection, view);
    return viewProj;
  }

  _drawQuad(elapsed) {
    const gl = this.gl;

    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(this.programQuad);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad.vertexBuffer);

    gl.enableVertexAttribArray(this.quad.attributes.position);
    gl.vertexAttribPointer(this.quad.attributes.position, 2, gl.FLOAT, false, 16, 0);

    gl.enableVertexAttribArray(this.quad.attributes.uv);
    gl.vertexAttribPointer(this.quad.attributes.uv, 2, gl.FLOAT, false, 16, 8);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.uniform1i(this.quad.uniforms.textureSampler, 0);
    gl.uniform1f(this.quad.uniforms.time, elapsed);
    gl.uniform1f(this.quad.uniforms.textureReady, this.textureReady ? 1.0 : 0.0);

    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }

  _drawMesh(viewProj) {
    if (!this.mesh.ready || !this.mesh.positionBuffer || !this.mesh.indexBuffer) {
      return false;
    }

    const gl = this.gl;

    gl.enable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);

    gl.useProgram(this.programMesh);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.mesh.positionBuffer);
    gl.enableVertexAttribArray(this.mesh.attributes.position);
    gl.vertexAttribPointer(this.mesh.attributes.position, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.mesh.normalBuffer);
    gl.enableVertexAttribArray(this.mesh.attributes.normal);
    gl.vertexAttribPointer(this.mesh.attributes.normal, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.mesh.uvBuffer);
    gl.enableVertexAttribArray(this.mesh.attributes.uv);
    gl.vertexAttribPointer(this.mesh.attributes.uv, 2, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.mesh.indexBuffer);

    gl.uniformMatrix4fv(this.mesh.uniforms.viewProj, false, viewProj);
    gl.uniform3f(this.mesh.uniforms.center, this.mesh.center[0], this.mesh.center[1], this.mesh.center[2]);
    gl.uniform1f(this.mesh.uniforms.meshScale, this.mesh.drawScale);
    gl.uniform1f(this.mesh.uniforms.sceneScale, this.mesh.sceneScale);
    gl.uniform3f(
      this.mesh.uniforms.modelPos,
      this.mesh.world.position[0],
      this.mesh.world.position[1],
      this.mesh.world.position[2],
    );
    gl.uniform1f(this.mesh.uniforms.modelYaw, this.mesh.world.enabled ? this.mesh.world.yaw : 0.0);
    gl.uniform3f(this.mesh.uniforms.lightDir, 0.35, 1.0, 0.25);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.uniform1i(this.mesh.uniforms.textureSampler, 0);
    gl.uniform1f(this.mesh.uniforms.textureReady, this.textureReady ? 1.0 : 0.0);

    gl.drawElements(gl.TRIANGLES, this.mesh.indexCount, gl.UNSIGNED_SHORT, 0);

    return true;
  }

  _drawTerrain(viewProj) {
    if (
      !this.terrain.ready
      || !this.terrain.visible
      || !this.terrain.positionBuffer
      || !this.terrain.uvBuffer
      || !this.terrain.indexBuffer
    ) {
      return false;
    }

    const gl = this.gl;
    gl.enable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.useProgram(this.programTerrain);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.positionBuffer);
    gl.enableVertexAttribArray(this.terrain.attributes.position);
    gl.vertexAttribPointer(this.terrain.attributes.position, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.normalBuffer);
    gl.enableVertexAttribArray(this.terrain.attributes.normal);
    gl.vertexAttribPointer(this.terrain.attributes.normal, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.colorBuffer);
    gl.enableVertexAttribArray(this.terrain.attributes.color);
    gl.vertexAttribPointer(this.terrain.attributes.color, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.terrain.uvBuffer);
    gl.enableVertexAttribArray(this.terrain.attributes.uv);
    gl.vertexAttribPointer(this.terrain.attributes.uv, 2, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.terrain.indexBuffer);
    gl.uniformMatrix4fv(this.terrain.uniforms.viewProj, false, viewProj);
    gl.uniform3f(this.terrain.uniforms.center, this.terrain.center[0], this.terrain.center[1], this.terrain.center[2]);
    gl.uniform1f(this.terrain.uniforms.scale, this.terrain.scale);
    gl.uniform3f(this.terrain.uniforms.lightDir, 0.25, 1.0, 0.3);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.terrainTexture);
    gl.uniform1i(this.terrain.uniforms.textureSampler, 1);
    gl.uniform1f(this.terrain.uniforms.textureReady, this.terrainTextureReady && this.terrain.textureVisible ? 1.0 : 0.0);
    gl.uniform1f(this.terrain.uniforms.textureBlend, this.terrain.textureBlend);
    gl.drawElements(gl.TRIANGLES, this.terrain.indexCount, gl.UNSIGNED_INT, 0);
    return true;
  }

  _drawObjectMeshes(viewProj) {
    if (!this.objectMeshes.ready || !this.objectMeshes.visible || this.objectMeshes.groups.length <= 0) {
      return false;
    }

    const gl = this.gl;
    gl.enable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.useProgram(this.programObjectMesh);

    gl.uniformMatrix4fv(this.objectMeshes.uniforms.viewProj, false, viewProj);
    gl.uniform3f(
      this.objectMeshes.uniforms.center,
      this.objectMeshes.center[0],
      this.objectMeshes.center[1],
      this.objectMeshes.center[2],
    );
    gl.uniform1f(this.objectMeshes.uniforms.scale, this.objectMeshes.scale);
    gl.uniform3f(this.objectMeshes.uniforms.lightDir, 0.30, 1.0, 0.24);

    for (const group of this.objectMeshes.groups) {
      if (
        !group.positionBuffer
        || !group.normalBuffer
        || !group.uvBuffer
        || !group.indexBuffer
        || !group.instanceBuffer
        || !Array.isArray(group.materials)
        || group.materials.length <= 0
        || group.instanceCount <= 0
      ) {
        continue;
      }

      gl.bindBuffer(gl.ARRAY_BUFFER, group.positionBuffer);
      gl.enableVertexAttribArray(this.objectMeshes.attributes.position);
      gl.vertexAttribPointer(this.objectMeshes.attributes.position, 3, gl.FLOAT, false, 0, 0);
      gl.vertexAttribDivisor(this.objectMeshes.attributes.position, 0);

      gl.bindBuffer(gl.ARRAY_BUFFER, group.normalBuffer);
      gl.enableVertexAttribArray(this.objectMeshes.attributes.normal);
      gl.vertexAttribPointer(this.objectMeshes.attributes.normal, 3, gl.FLOAT, false, 0, 0);
      gl.vertexAttribDivisor(this.objectMeshes.attributes.normal, 0);

      gl.bindBuffer(gl.ARRAY_BUFFER, group.uvBuffer);
      gl.enableVertexAttribArray(this.objectMeshes.attributes.uv);
      gl.vertexAttribPointer(this.objectMeshes.attributes.uv, 2, gl.FLOAT, false, 0, 0);
      gl.vertexAttribDivisor(this.objectMeshes.attributes.uv, 0);

      gl.bindBuffer(gl.ARRAY_BUFFER, group.instanceBuffer);
      gl.enableVertexAttribArray(this.objectMeshes.attributes.instancePos);
      gl.vertexAttribPointer(this.objectMeshes.attributes.instancePos, 3, gl.FLOAT, false, 24, 0);
      gl.vertexAttribDivisor(this.objectMeshes.attributes.instancePos, 1);

      gl.enableVertexAttribArray(this.objectMeshes.attributes.instanceYaw);
      gl.vertexAttribPointer(this.objectMeshes.attributes.instanceYaw, 1, gl.FLOAT, false, 24, 12);
      gl.vertexAttribDivisor(this.objectMeshes.attributes.instanceYaw, 1);

      gl.enableVertexAttribArray(this.objectMeshes.attributes.instanceScaleHV);
      gl.vertexAttribPointer(this.objectMeshes.attributes.instanceScaleHV, 2, gl.FLOAT, false, 24, 16);
      gl.vertexAttribDivisor(this.objectMeshes.attributes.instanceScaleHV, 1);

      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, group.indexBuffer);
      for (const material of group.materials) {
        if (!material || material.indexCount <= 0) {
          continue;
        }

        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, material.texture);
        gl.uniform1i(this.objectMeshes.uniforms.textureSampler, 2);
        gl.uniform1f(this.objectMeshes.uniforms.textureReady, material.textureReady ? 1.0 : 0.0);
        gl.drawElementsInstanced(
          gl.TRIANGLES,
          material.indexCount,
          gl.UNSIGNED_SHORT,
          material.indexOffsetBytes,
          group.instanceCount,
        );
      }
    }

    gl.vertexAttribDivisor(this.objectMeshes.attributes.instancePos, 0);
    gl.vertexAttribDivisor(this.objectMeshes.attributes.instanceYaw, 0);
    gl.vertexAttribDivisor(this.objectMeshes.attributes.instanceScaleHV, 0);
    return true;
  }

  _drawObjectPoints(viewProj) {
    if (!this.objectPoints.ready || !this.objectPoints.visible || !this.objectPoints.positionBuffer || this.objectPoints.count <= 0) {
      return false;
    }

    const gl = this.gl;
    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    gl.useProgram(this.programPoints);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.objectPoints.positionBuffer);
    gl.enableVertexAttribArray(this.objectPoints.attributes.position);
    gl.vertexAttribPointer(this.objectPoints.attributes.position, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.objectPoints.colorBuffer);
    gl.enableVertexAttribArray(this.objectPoints.attributes.color);
    gl.vertexAttribPointer(this.objectPoints.attributes.color, 3, gl.FLOAT, false, 0, 0);

    gl.uniformMatrix4fv(this.objectPoints.uniforms.viewProj, false, viewProj);
    gl.uniform3f(this.objectPoints.uniforms.center, this.objectPoints.center[0], this.objectPoints.center[1], this.objectPoints.center[2]);
    gl.uniform1f(this.objectPoints.uniforms.scale, this.objectPoints.scale);
    gl.uniform1f(this.objectPoints.uniforms.pointSize, this.objectPoints.pointSize);
    gl.drawArrays(gl.POINTS, 0, this.objectPoints.count);
    gl.disable(gl.BLEND);
    return true;
  }

  _resizeToDisplaySize() {
    const rect = this.canvas.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) {
      return;
    }

    const dpr = window.devicePixelRatio || 1;
    const maxBufferSize = 4096;
    const displayWidth = Math.min(maxBufferSize, Math.floor(rect.width * dpr));
    const displayHeight = Math.min(maxBufferSize, Math.floor(rect.height * dpr));

    if (this.canvas.width !== displayWidth || this.canvas.height !== displayHeight) {
      this.canvas.width = displayWidth;
      this.canvas.height = displayHeight;
    }
  }

  getViewportInfo() {
    const rect = this.canvas.getBoundingClientRect();
    return {
      cssWidth: Math.round(rect.width),
      cssHeight: Math.round(rect.height),
      bufferWidth: this.canvas.width,
      bufferHeight: this.canvas.height,
      dpr: window.devicePixelRatio || 1,
    };
  }
}
