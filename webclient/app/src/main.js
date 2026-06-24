import { AssetClient, bytesToAscii, bytesToHex } from './assetClient.js';
import { WebGLRenderer } from './webglRenderer.js';
import { decodeWysBuffer, decodeWytBuffer } from './wydTextureDecoder.js';
import { parseWydMshBuffer } from './wydMeshParser.js';
import { parseWydMsaBuffer } from './wydMsaParser.js';
import { parseWydAniBuffer } from './wydAniParser.js';
import { parseWydBonBuffer } from './wydBonParser.js';
import { applySkinningAtTick, createSkinningContext, isMeshSkinnable } from './wydSkinningRuntime.js';
import { createWasmBridge } from './wasmBridge.js';

const statusEl = document.getElementById('status');
const logEl = document.getElementById('log');
const metricFilesEl = document.getElementById('metric-files');
const metricBytesEl = document.getElementById('metric-bytes');
const metricBootstrapEl = document.getElementById('metric-bootstrap');
const reloadButton = document.getElementById('reload');
const meshSelectEl = document.getElementById('mesh-select');
const loadMeshButton = document.getElementById('load-mesh');
const meshMetaEl = document.getElementById('mesh-meta');
const renderMetaEl = document.getElementById('render-meta');
const fieldSelectEl = document.getElementById('field-select');
const loadFieldButton = document.getElementById('load-field');
const fieldMetaEl = document.getElementById('field-meta');
const togglePlayerModeEl = document.getElementById('toggle-player-mode');
const playerWalkSpeedEl = document.getElementById('player-walk-speed');
const playerRunSpeedEl = document.getElementById('player-run-speed');
const togglePlayerClickMoveEl = document.getElementById('toggle-player-click-move');
const togglePlayerCollisionEl = document.getElementById('toggle-player-collision');
const togglePlayerAutoRunEl = document.getElementById('toggle-player-autorun');
const playerCameraModeEl = document.getElementById('player-camera-mode');
const playerCameraDistanceEl = document.getElementById('player-camera-distance');
const playerCameraHeightEl = document.getElementById('player-camera-height');
const spawnPlayerAtSelectedButton = document.getElementById('spawn-player-at-selected');
const playerMetaEl = document.getElementById('player-meta');
const fieldObjectMetaEl = document.getElementById('field-object-meta');
const fieldObjectSelectedEl = document.getElementById('field-object-selected');
const fieldMinimapMetaEl = document.getElementById('field-minimap-meta');
const fieldPreviewCanvas = document.getElementById('field-preview');
const toggleTerrainEl = document.getElementById('toggle-terrain');
const terrainScaleEl = document.getElementById('terrain-scale');
const toggleTerrainTextureEl = document.getElementById('toggle-terrain-texture');
const terrainTextureBlendEl = document.getElementById('terrain-texture-blend');
const toggleObjectPointsEl = document.getElementById('toggle-object-points');
const objectPointSizeEl = document.getElementById('object-point-size');
const toggleObjectMeshesEl = document.getElementById('toggle-object-meshes');
const objectMeshBudgetEl = document.getElementById('object-mesh-budget');
const objectMeshTypesEl = document.getElementById('object-mesh-types');
const fieldObjectMeshMetaEl = document.getElementById('field-object-mesh-meta');
const clearObjectSelectionButton = document.getElementById('clear-object-selection');
const aniSelectEl = document.getElementById('ani-select');
const bonSelectEl = document.getElementById('bon-select');
const aniProfileEntryEl = document.getElementById('ani-profile-entry');
const aniProfileWeaponEl = document.getElementById('ani-profile-weapon');
const aniProfileMotionEl = document.getElementById('ani-profile-motion');
const applyProfileAniButton = document.getElementById('apply-profile-ani');
const preloadProfileButton = document.getElementById('preload-profile');
const sequenceScopeEl = document.getElementById('sequence-scope');
const sequenceIntervalEl = document.getElementById('sequence-interval');
const toggleProfileSeqButton = document.getElementById('toggle-profile-seq');
const aniSequenceMetaEl = document.getElementById('ani-sequence-meta');
const aniProfileMetaEl = document.getElementById('ani-profile-meta');
const loadSkinningButton = document.getElementById('load-skinning');
const toggleAniButton = document.getElementById('toggle-ani');
const aniFrameEl = document.getElementById('ani-frame');
const aniSpeedEl = document.getElementById('ani-speed');
const aniMetaEl = document.getElementById('ani-meta');

const assetClient = new AssetClient();
const renderer = new WebGLRenderer(document.getElementById('viewport'));

const LOG_LINE_LIMIT = 800;
const PROFILE_PRELOAD_CONCURRENCY = 6;
const MAX_ANI_CACHE = 96;
const MAX_BON_CACHE = 64;
const logLines = [];

const MOTION_LABELS = {
  0: 'STAND01',
  1: 'STAND02',
  2: 'WALK',
  3: 'RUN',
  4: 'ATTACK01',
  5: 'ATTACK02',
  6: 'ATTACK03',
  7: 'ATTACK04',
  8: 'ATTACK05',
  9: 'ATTACK06',
  10: 'STRIKE',
  11: 'DIE',
  12: 'DEAD',
  13: 'SEAT',
  14: 'LEVELUP',
  15: 'PUNISH',
  16: 'CACKLE',
  17: 'YAWN',
  18: 'MERCHANT_LOUNGE',
  19: 'RELAX',
  20: 'NEVER',
  21: 'COMEON',
  22: 'SALUTE',
  23: 'HOLYTOUCH',
  24: 'SEATING',
  25: 'STAND03',
  26: 'PUNISHING',
  27: 'PUNEND',
};

for (let i = 0; i <= 27; i += 1) {
  const label = MOTION_LABELS[i] || `MOTION_${i}`;
  MOTION_LABELS[i + 28] = `${label} (MOUNT)`;
}

function parseBooleanQueryParam(value) {
  const normalized = String(value || '').trim().toLowerCase();
  return normalized === '1' || normalized === 'true' || normalized === 'yes' || normalized === 'on';
}

function parseNumberQueryParam(searchParams, keys, fallback = null) {
  for (const key of keys) {
    if (!searchParams.has(key)) {
      continue;
    }

    const value = Number(searchParams.get(key));
    if (Number.isFinite(value)) {
      return value;
    }
  }

  return fallback;
}

function parseStringQueryParam(searchParams, keys, fallback = '') {
  for (const key of keys) {
    if (!searchParams.has(key)) {
      continue;
    }

    const value = String(searchParams.get(key) || '').trim();
    if (value.length > 0) {
      return value;
    }
  }

  return fallback;
}

function parseStartupOptions() {
  const searchParams = new URLSearchParams(window.location.search);
  const sequenceScopeRaw = parseStringQueryParam(searchParams, ['seqScope', 'sequenceScope'], 'weapon').toLowerCase();
  const sequenceScope = sequenceScopeRaw === 'all' ? 'all' : 'weapon';
  const sequenceIntervalRaw = parseNumberQueryParam(searchParams, ['seqInterval', 'sequenceInterval'], 2.0);
  const sequenceIntervalSec = Math.max(0.8, Math.min(6.0, Number.isFinite(sequenceIntervalRaw) ? sequenceIntervalRaw : 2.0));
  const sequenceAuto = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['seq', 'sequence'], '0'));
  const preloadAuto = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['preloadProfile', 'preload'], '0'));
  const entryIndex = parseNumberQueryParam(searchParams, ['entry', 'aniEntry', 'profileEntry'], null);
  const weaponType = parseNumberQueryParam(searchParams, ['weapon', 'weaponType'], null);
  const motionIndex = parseNumberQueryParam(searchParams, ['motion', 'motionIndex'], null);
  const fieldName = parseStringQueryParam(searchParams, ['field'], '');
  const terrainVisible = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['terrain', 'showTerrain'], '1'));
  const terrainTextureVisible = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['terrainTexture', 'showTerrainTexture', 'terrainTex'], '1'));
  const objectPointsVisible = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['objPoints', 'showObjects'], '1'));
  const objectMeshesVisible = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['objMeshes', 'showObjMeshes'], '1'));
  const terrainScaleRaw = parseNumberQueryParam(searchParams, ['terrainScale'], 0.08);
  const terrainScale = Math.max(0.02, Math.min(0.20, Number.isFinite(terrainScaleRaw) ? terrainScaleRaw : 0.08));
  const terrainTextureBlendRaw = parseNumberQueryParam(searchParams, ['terrainTextureBlend', 'terrainBlend'], 0.82);
  const terrainTextureBlend = Math.max(0.0, Math.min(1.0, Number.isFinite(terrainTextureBlendRaw) ? terrainTextureBlendRaw : 0.82));
  const objectPointSizeRaw = parseNumberQueryParam(searchParams, ['objectPointSize', 'objPointSize'], 2.5);
  const objectPointSize = Math.max(1.0, Math.min(8.0, Number.isFinite(objectPointSizeRaw) ? objectPointSizeRaw : 2.5));
  const objectMeshBudgetRaw = parseNumberQueryParam(searchParams, ['objMeshBudget', 'objectMeshBudget'], 640);
  const objectMeshBudget = Math.max(128, Math.min(3200, Number.isFinite(objectMeshBudgetRaw) ? objectMeshBudgetRaw : 640));
  const objectMeshTypesRaw = parseNumberQueryParam(searchParams, ['objMeshTypes', 'objectMeshTypes'], 20);
  const objectMeshTypes = Math.max(4, Math.min(96, Number.isFinite(objectMeshTypesRaw) ? objectMeshTypesRaw : 20));
  const playerMode = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['player', 'playerMode'], '0'));
  const playerWalkSpeedRaw = parseNumberQueryParam(searchParams, ['walkSpeed', 'playerWalkSpeed'], 3.8);
  const playerRunSpeedRaw = parseNumberQueryParam(searchParams, ['runSpeed', 'playerRunSpeed'], 7.2);
  const playerClickMove = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['clickMove', 'playerClickMove'], '1'));
  const playerCollision = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['collision', 'playerCollision'], '1'));
  const playerAutoRun = parseBooleanQueryParam(parseStringQueryParam(searchParams, ['autoRun', 'playerAutoRun'], '1'));
  const playerCameraModeRaw = parseStringQueryParam(searchParams, ['camera', 'playerCamera'], 'follow').toLowerCase();
  const playerCameraMode = playerCameraModeRaw === 'orbit' ? 'orbit' : 'follow';
  const playerCameraDistanceRaw = parseNumberQueryParam(searchParams, ['cameraDistance', 'playerCameraDistance'], 7.2);
  const playerCameraHeightRaw = parseNumberQueryParam(searchParams, ['cameraHeight', 'playerCameraHeight'], 2.8);
  const playerWalkSpeed = Math.max(1.5, Math.min(8.0, Number.isFinite(playerWalkSpeedRaw) ? playerWalkSpeedRaw : 3.8));
  const playerRunSpeed = Math.max(playerWalkSpeed + 0.2, Math.min(12.0, Number.isFinite(playerRunSpeedRaw) ? playerRunSpeedRaw : 7.2));
  const playerCameraDistance = Math.max(3.0, Math.min(15.0, Number.isFinite(playerCameraDistanceRaw) ? playerCameraDistanceRaw : 7.2));
  const playerCameraHeight = Math.max(1.2, Math.min(8.0, Number.isFinite(playerCameraHeightRaw) ? playerCameraHeightRaw : 2.8));

  return {
    sequenceAuto,
    preloadAuto,
    sequenceScope,
    sequenceIntervalSec,
    sequenceInitialized: false,
    entryIndex: Number.isFinite(entryIndex) ? Number(entryIndex) : null,
    weaponType: Number.isFinite(weaponType) ? Number(weaponType) : null,
    motionIndex: Number.isFinite(motionIndex) ? Number(motionIndex) : null,
    fieldName,
    terrainVisible,
    terrainTextureVisible,
    terrainScale,
    terrainTextureBlend,
    objectPointsVisible,
    objectPointSize,
    objectMeshesVisible,
    objectMeshBudget,
    objectMeshTypes,
    playerMode,
    playerWalkSpeed,
    playerRunSpeed,
    playerClickMove,
    playerCollision,
    playerAutoRun,
    playerCameraMode,
    playerCameraDistance,
    playerCameraHeight,
  };
}

const startupOptions = parseStartupOptions();

const state = {
  meshCatalog: [],
  currentMeshPath: 'Mesh\\CP010102.msh',
  fieldCatalog: [],
  currentFieldName: startupOptions.fieldName || '',
  startedRenderLoop: false,
  renderInfoTimer: null,
  skinningTimer: null,
  startup: startupOptions,
  meshLoadToken: 0,
  fieldLoadToken: 0,
  field: {
    summary: null,
    previewImageData: null,
    minimapPath: '',
    selectedObjectIndex: -1,
    objectMeshMapByType: new Map(),
    objectMeshReloadTimer: null,
    objectTypeRadiusByType: new Map(),
    collisionProxies: [],
    lastPreviewPlayerStampMs: 0,
  },
  objectMeshCache: new Map(),
  objectTextureCache: new Map(),
  aniCache: new Map(),
  bonCache: new Map(),
  wasm: {
    ready: false,
    bridge: null,
    initStarted: false,
    initError: '',
  },
  player: {
    enabled: startupOptions.playerMode,
    active: false,
    worldX: 0,
    worldY: 0,
    worldZ: 0,
    yaw: 0,
    walkSpeed: startupOptions.playerWalkSpeed,
    runSpeed: startupOptions.playerRunSpeed,
    clickMoveEnabled: startupOptions.playerClickMove,
    collisionEnabled: startupOptions.playerCollision,
    autoRunEnabled: startupOptions.playerAutoRun,
    cameraMode: startupOptions.playerCameraMode,
    cameraDistance: startupOptions.playerCameraDistance,
    cameraHeight: startupOptions.playerCameraHeight,
    radius: 0.42,
    collisionPadding: 0.04,
    mode: 'idle',
    motionBusy: false,
    currentMotionIndex: -1,
    destination: {
      active: false,
      worldX: 0,
      worldZ: 0,
      source: '',
    },
    blockedLastFrame: false,
    stuckMs: 0,
    keyState: {
      forward: false,
      backward: false,
      left: false,
      right: false,
      run: false,
    },
    loopTimer: null,
    lastUpdateMs: 0,
  },
  skinning: {
    mesh: null,
    context: null,
    ani: null,
    bon: null,
    aniPath: '',
    bonPath: '',
    profile: null,
    playing: true,
    frameFloat: 0,
    speed: 1,
    lastUpdateMs: 0,
    profileBank: null,
    sequence: {
      active: false,
      timer: null,
      intervalMs: 2000,
      scope: 'weapon',
      position: 0,
      busy: false,
    },
  },
};

toggleTerrainEl.value = state.startup.terrainVisible ? 'on' : 'off';
terrainScaleEl.value = state.startup.terrainScale.toFixed(2);
toggleTerrainTextureEl.value = state.startup.terrainTextureVisible ? 'on' : 'off';
terrainTextureBlendEl.value = state.startup.terrainTextureBlend.toFixed(2);
toggleObjectPointsEl.value = state.startup.objectPointsVisible ? 'on' : 'off';
objectPointSizeEl.value = state.startup.objectPointSize.toFixed(1);
toggleObjectMeshesEl.value = state.startup.objectMeshesVisible ? 'on' : 'off';
objectMeshBudgetEl.value = String(Math.round(state.startup.objectMeshBudget));
objectMeshTypesEl.value = String(Math.round(state.startup.objectMeshTypes));
togglePlayerModeEl.value = state.player.enabled ? 'on' : 'off';
playerWalkSpeedEl.value = state.player.walkSpeed.toFixed(1);
playerRunSpeedEl.value = state.player.runSpeed.toFixed(1);
if (togglePlayerClickMoveEl) {
  togglePlayerClickMoveEl.value = state.player.clickMoveEnabled ? 'on' : 'off';
}
if (togglePlayerCollisionEl) {
  togglePlayerCollisionEl.value = state.player.collisionEnabled ? 'on' : 'off';
}
if (togglePlayerAutoRunEl) {
  togglePlayerAutoRunEl.value = state.player.autoRunEnabled ? 'on' : 'off';
}
if (playerCameraModeEl) {
  playerCameraModeEl.value = state.player.cameraMode;
}
if (playerCameraDistanceEl) {
  playerCameraDistanceEl.value = state.player.cameraDistance.toFixed(1);
}
if (playerCameraHeightEl) {
  playerCameraHeightEl.value = state.player.cameraHeight.toFixed(1);
}
renderer.setTerrainVisible(state.startup.terrainVisible);
renderer.setTerrainScale(state.startup.terrainScale);
renderer.setTerrainTextureVisible(state.startup.terrainTextureVisible);
renderer.setTerrainTextureBlend(state.startup.terrainTextureBlend);
renderer.setFieldObjectPointsVisible(state.startup.objectPointsVisible);
renderer.setFieldObjectPointSize(state.startup.objectPointSize);
renderer.setFieldObjectMeshesVisible(state.startup.objectMeshesVisible);
renderer.setMeshSceneScale(state.startup.terrainScale);
renderer.setCameraMode(state.player.enabled ? state.player.cameraMode : 'orbit');
renderer.setCameraFollowOptions({
  distance: state.player.cameraDistance,
  height: state.player.cameraHeight,
});

function formatBytes(bytes) {
  const units = ['B', 'KB', 'MB', 'GB'];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }

  return `${value.toFixed(unit === 0 ? 0 : 2)} ${units[unit]}`;
}

function shaderVersionString(signature) {
  switch (signature) {
    case 0xfffe0101:
      return 'vs_1_1';
    case 0xffff0101:
      return 'ps_1_1';
    default:
      return 'unknown';
  }
}

function clearLog() {
  logLines.length = 0;
  logEl.textContent = '';
}

function appendLog(message) {
  logLines.push(message);
  if (logLines.length > LOG_LINE_LIMIT) {
    logLines.splice(0, logLines.length - LOG_LINE_LIMIT);
  }

  logEl.textContent = logLines.join('\n');
  logEl.scrollTop = logEl.scrollHeight;
}

function setStatus(message, isError = false) {
  statusEl.textContent = message;
  statusEl.style.background = isError ? 'rgba(214, 85, 85, 0.20)' : 'rgba(45, 179, 161, 0.15)';
  statusEl.style.borderColor = isError ? 'rgba(214, 85, 85, 0.55)' : 'rgba(45, 179, 161, 0.45)';
}

function setMeshMeta(message) {
  meshMetaEl.textContent = message;
}

function setRenderMeta(message) {
  renderMetaEl.textContent = message;
}

function setFieldMeta(message) {
  fieldMetaEl.textContent = message;
}

function setPlayerMeta(message) {
  if (playerMetaEl) {
    playerMetaEl.textContent = message;
  }
}

function setFieldObjectMeta(message) {
  fieldObjectMetaEl.textContent = message;
}

function setFieldObjectMeshMeta(message) {
  if (fieldObjectMeshMetaEl) {
    fieldObjectMeshMetaEl.textContent = message;
  }
}

function refreshFieldObjectMeshMeta() {
  const info = renderer.getFieldObjectMeshInfo();
  if (!info.ready) {
    return;
  }

  setFieldObjectMeshMeta(
    `Meshes estáticos: grupos ${info.groupCount} | materiais ${info.materialCount} | instâncias ${info.instanceCount} | tri ${info.triangleCount} | colisão ${state.field.collisionProxies.length} proxies | budget ${getObjectMeshInstanceBudget()} | ${info.visible ? 'visível' : 'oculto'}`,
  );
}

function setFieldObjectSelectedMeta(message) {
  if (fieldObjectSelectedEl) {
    fieldObjectSelectedEl.textContent = message;
  }
}

function setFieldMinimapMeta(message) {
  if (fieldMinimapMetaEl) {
    fieldMinimapMetaEl.textContent = message;
  }
}

function terrainEnabled() {
  return String(toggleTerrainEl.value || 'on') !== 'off';
}

function terrainTextureEnabled() {
  return String(toggleTerrainTextureEl.value || 'on') !== 'off';
}

function objectPointsEnabled() {
  return String(toggleObjectPointsEl.value || 'on') !== 'off';
}

function objectMeshesEnabled() {
  return String(toggleObjectMeshesEl.value || 'on') !== 'off';
}

function setAniMeta(message) {
  aniMetaEl.textContent = message;
}

function setAniProfileMeta(message) {
  aniProfileMetaEl.textContent = message;
}

function setAniSequenceMeta(message) {
  aniSequenceMetaEl.textContent = message;
}

async function initWasmBridge() {
  const wasmState = state.wasm;
  if (wasmState.ready && wasmState.bridge) {
    return wasmState.bridge;
  }
  if (wasmState.initStarted) {
    return wasmState.bridge;
  }

  wasmState.initStarted = true;
  try {
    const bridge = await createWasmBridge();
    wasmState.bridge = bridge;
    wasmState.ready = true;
    wasmState.initError = '';
    appendLog('[WASM] bridge C++ carregada com sucesso (parser nativo ativo).');
    return bridge;
  } catch (error) {
    wasmState.ready = false;
    wasmState.bridge = null;
    wasmState.initError = String(error?.message || error);
    appendLog(`[WASM] indisponível, fallback JS ativo: ${wasmState.initError}`);
    return null;
  }
}

function normalizeMeshPath(meshPath) {
  return (meshPath || '').replace(/\//g, '\\').trim();
}

function toPathKey(path) {
  return normalizeMeshPath(path).toLowerCase();
}

function lruGet(cacheMap, key) {
  if (!cacheMap.has(key)) {
    return null;
  }

  const value = cacheMap.get(key);
  cacheMap.delete(key);
  cacheMap.set(key, value);
  return value;
}

function lruSet(cacheMap, key, value, maxEntries) {
  if (cacheMap.has(key)) {
    cacheMap.delete(key);
  }
  cacheMap.set(key, value);

  while (cacheMap.size > maxEntries) {
    const oldestKey = cacheMap.keys().next().value;
    if (oldestKey == null) {
      break;
    }
    cacheMap.delete(oldestKey);
  }
}

function updateUrlMeshQuery(meshPath) {
  const url = new URL(window.location.href);
  url.searchParams.set('mesh', meshPath);
  window.history.replaceState({}, '', url);
}

function updateUrlFieldQuery(fieldName) {
  const url = new URL(window.location.href);
  if (fieldName) {
    url.searchParams.set('field', fieldName);
  } else {
    url.searchParams.delete('field');
  }
  window.history.replaceState({}, '', url);
}

function clearSelect(selectEl, emptyLabel) {
  selectEl.innerHTML = '';
  const option = document.createElement('option');
  option.value = '';
  option.textContent = emptyLabel;
  selectEl.appendChild(option);
}

function fillPathSelect(selectEl, paths, selectedPath, emptyLabel) {
  clearSelect(selectEl, emptyLabel);
  const normalizedSelected = toPathKey(selectedPath || '');

  for (const path of paths) {
    const option = document.createElement('option');
    option.value = path;
    option.textContent = path;
    if (toPathKey(path) === normalizedSelected) {
      option.selected = true;
    }
    selectEl.appendChild(option);
  }
}

function ensurePathOption(selectEl, path) {
  const normalized = toPathKey(path);
  for (const option of selectEl.options) {
    if (toPathKey(option.value) === normalized) {
      return;
    }
  }

  const option = document.createElement('option');
  option.value = path;
  option.textContent = path;
  selectEl.appendChild(option);
}

function fillNumberSelect(selectEl, values, selectedValue, formatter = null, emptyLabel = '(n/d)') {
  clearSelect(selectEl, emptyLabel);
  const selected = Number.isFinite(selectedValue) ? Number(selectedValue) : null;

  for (const value of values) {
    const option = document.createElement('option');
    option.value = String(value);
    option.textContent = formatter ? formatter(value) : String(value);
    if (selected != null && value === selected) {
      option.selected = true;
    }
    selectEl.appendChild(option);
  }
}

function motionLabel(motionIndex) {
  if (Object.prototype.hasOwnProperty.call(MOTION_LABELS, motionIndex)) {
    return `${motionIndex} - ${MOTION_LABELS[motionIndex]}`;
  }
  return `${motionIndex} - MOTION_${motionIndex}`;
}

function profileEntryLabel(entry) {
  return `${entry.index} | ${entry.basePath} | ANI=${entry.existingAniCount}/${entry.aniTypeCount}`;
}

function normalizeProfileRecord(record) {
  return {
    tableIndex: Number(record.tableIndex || 0),
    arrayIndex: Number(record.arrayIndex || 0),
    weaponType: Number(record.weaponType ?? -1),
    motionIndex: Number(record.motionIndex ?? -1),
    aniPath: normalizeMeshPath(record.aniPath || ''),
    exists: Boolean(record.exists),
    tickCount: Number(record.tickCount || 0),
    bonesPerFrame: Number(record.bonesPerFrame || 0),
  };
}

function pickRecommendedProfileRecord(records) {
  if (!records || records.length === 0) {
    return null;
  }

  const preferred = [
    [0, 0],
    [0, 1],
    [0, 2],
    [0, 3],
    [0, 4],
    [0, 11],
    [0, 12],
    [0, 14],
  ];

  for (const [weaponType, motionIndex] of preferred) {
    const found = records.find((record) => record.weaponType === weaponType && record.motionIndex === motionIndex);
    if (found) {
      return found;
    }
  }

  const sorted = [...records].sort((a, b) => {
    if (a.weaponType !== b.weaponType) {
      return a.weaponType - b.weaponType;
    }
    if (a.motionIndex !== b.motionIndex) {
      return a.motionIndex - b.motionIndex;
    }
    return a.tableIndex - b.tableIndex;
  });

  return sorted[0] || null;
}

function findProfileRecord(records, weaponType, motionIndex) {
  const exact = records.find((record) => record.weaponType === weaponType && record.motionIndex === motionIndex);
  if (exact) {
    return exact;
  }

  const byWeapon = records.find((record) => record.weaponType === weaponType);
  if (byWeapon) {
    return byWeapon;
  }

  const byMotion = records.find((record) => record.motionIndex === motionIndex);
  if (byMotion) {
    return byMotion;
  }

  return records[0] || null;
}

function collectMotionsForWeapon(records, weaponType) {
  const set = new Set();
  for (const record of records) {
    if (record.weaponType === weaponType && record.motionIndex >= 0) {
      set.add(record.motionIndex);
    }
  }
  return [...set].sort((a, b) => a - b);
}

function buildProfileFromCatalogEntry(meshPath, baseProfile, entryPayload) {
  const recordsRaw = Array.isArray(entryPayload.records) ? entryPayload.records : [];
  const records = recordsRaw
    .map(normalizeProfileRecord)
    .filter((record) => record.exists && record.aniPath.length > 0);

  const entry = {
    index: Number(entryPayload.index || 0),
    aniTypeCount: Number(entryPayload.aniTypeCount || 0),
    parts: Number(entryPayload.parts || 0),
    basePath: normalizeMeshPath(entryPayload.basePath || ''),
    baseStem: String(entryPayload.baseStem || '').toLowerCase(),
    bonPath: normalizeMeshPath(entryPayload.bonPath || ''),
    bonExists: Boolean(entryPayload.bonExists),
    existingAniCount: Number(entryPayload.existingAniCount || records.length),
    availableWeaponTypes: Array.isArray(entryPayload.availableWeaponTypes)
      ? entryPayload.availableWeaponTypes.map((value) => Number(value)).filter((value) => Number.isFinite(value))
      : [],
  };

  const availableWeaponTypes = entry.availableWeaponTypes.length > 0
    ? [...entry.availableWeaponTypes].sort((a, b) => a - b)
    : [...new Set(records.map((record) => record.weaponType).filter((value) => value >= 0))].sort((a, b) => a - b);

  const recommended = pickRecommendedProfileRecord(records);

  return {
    meshPath,
    entry,
    bonPath: entry.bonPath,
    records,
    matchCandidates: baseProfile?.matchCandidates || [
      {
        index: entry.index,
        basePath: entry.basePath,
        baseStem: entry.baseStem,
        aniTypeCount: entry.aniTypeCount,
        existingAniCount: entry.existingAniCount,
      },
    ],
    availableWeaponTypes,
    recommended,
  };
}

function resetAnimationProfileControls() {
  stopProfileSequence();
  clearSelect(aniProfileEntryEl, '(sem perfil)');
  clearSelect(aniProfileWeaponEl, '(sem weapon)');
  clearSelect(aniProfileMotionEl, '(sem motion)');

  const startupScope = !state.startup.sequenceInitialized
    ? (state.startup.sequenceScope === 'all' ? 'all' : 'weapon')
    : (state.skinning.sequence.scope === 'all' ? 'all' : 'weapon');
  const startupIntervalSec = !state.startup.sequenceInitialized
    ? state.startup.sequenceIntervalSec
    : Math.max(0.2, state.skinning.sequence.intervalMs / 1000);

  sequenceScopeEl.value = startupScope;
  sequenceIntervalEl.value = startupIntervalSec.toFixed(1);
  state.skinning.sequence.scope = startupScope;
  state.skinning.sequence.intervalMs = Math.round(startupIntervalSec * 1000);
  state.skinning.sequence.position = 0;
  aniProfileEntryEl.disabled = true;
  aniProfileWeaponEl.disabled = true;
  aniProfileMotionEl.disabled = true;
  applyProfileAniButton.disabled = true;
  preloadProfileButton.disabled = true;
  sequenceScopeEl.disabled = true;
  sequenceIntervalEl.disabled = true;
  toggleProfileSeqButton.disabled = true;
  toggleProfileSeqButton.textContent = 'Iniciar Sequência';
  setAniProfileMeta('Perfil de animação: aguardando mesh...');
  setAniSequenceMeta('Sequência: inativa.');
}

function refreshProfileWeaponAndMotionControls(profile, preferredWeapon = null, preferredMotion = null) {
  if (!profile || !profile.records || profile.records.length === 0) {
    clearSelect(aniProfileWeaponEl, '(sem weapon)');
    clearSelect(aniProfileMotionEl, '(sem motion)');
    aniProfileWeaponEl.disabled = true;
    aniProfileMotionEl.disabled = true;
    return;
  }

  const defaultRecord = profile.recommended || profile.records[0];
  const weaponTypes = profile.availableWeaponTypes.length > 0
    ? profile.availableWeaponTypes
    : [...new Set(profile.records.map((record) => record.weaponType).filter((value) => value >= 0))].sort((a, b) => a - b);

  const selectedWeapon = Number.isFinite(preferredWeapon) ? preferredWeapon : defaultRecord.weaponType;
  fillNumberSelect(
    aniProfileWeaponEl,
    weaponTypes,
    selectedWeapon,
    (value) => `Weapon ${value}`,
    '(sem weapon)',
  );
  aniProfileWeaponEl.disabled = weaponTypes.length === 0;

  const weaponNow = Number(aniProfileWeaponEl.value);
  const motions = collectMotionsForWeapon(profile.records, weaponNow);
  const selectedMotion = Number.isFinite(preferredMotion) ? preferredMotion : defaultRecord.motionIndex;
  fillNumberSelect(aniProfileMotionEl, motions, selectedMotion, motionLabel, '(sem motion)');
  aniProfileMotionEl.disabled = motions.length === 0;

  applyProfileAniButton.disabled = false;
}

function setAnimationProfile(profile) {
  const previousEntryIndex = state.skinning.profile?.entry?.index ?? null;
  state.skinning.profile = profile;

  if (!profile || !profile.records || profile.records.length === 0) {
    resetProfileBank();
    resetAnimationProfileControls();
    return;
  }

  if (previousEntryIndex !== profile.entry.index) {
    resetProfileBank();
  }

  const candidates = Array.isArray(profile.matchCandidates) && profile.matchCandidates.length > 0
    ? profile.matchCandidates
    : [{
      index: profile.entry.index,
      basePath: profile.entry.basePath,
      baseStem: profile.entry.baseStem,
      aniTypeCount: profile.entry.aniTypeCount,
      existingAniCount: profile.entry.existingAniCount,
    }];

  aniProfileEntryEl.innerHTML = '';
  for (const candidate of candidates) {
    const option = document.createElement('option');
    option.value = String(candidate.index);
    option.textContent = profileEntryLabel(candidate);
    if (candidate.index === profile.entry.index) {
      option.selected = true;
    }
    aniProfileEntryEl.appendChild(option);
  }

  aniProfileEntryEl.disabled = candidates.length <= 1;

  refreshProfileWeaponAndMotionControls(
    profile,
    profile.recommended ? profile.recommended.weaponType : null,
    profile.recommended ? profile.recommended.motionIndex : null,
  );

  const recommendedText = profile.recommended
    ? `recomendado: ${profile.recommended.aniPath} (${motionLabel(profile.recommended.motionIndex)})`
    : 'sem ANI recomendado';

  setAniProfileMeta(`Perfil ${profile.entry.basePath} | records=${profile.records.length} | ${recommendedText}`);
  preloadProfileButton.disabled = false;
  sequenceScopeEl.disabled = false;
  sequenceIntervalEl.disabled = false;
  toggleProfileSeqButton.disabled = false;
  updateSequenceIntervalMs();
  updateSequenceSummary(sequenceScopeEl.value === 'all' ? 'all' : 'weapon', getRecordsForSequenceScope(profile, sequenceScopeEl.value === 'all' ? 'all' : 'weapon', aniProfileWeaponEl.value));
}

function makeProfileRecordKey(record) {
  return `${record.weaponType}:${record.motionIndex}`;
}

function stopProfileSequence() {
  const sequence = state.skinning.sequence;
  sequence.active = false;
  sequence.busy = false;
  sequence.position = 0;

  if (sequence.timer != null) {
    clearInterval(sequence.timer);
    sequence.timer = null;
  }

  toggleProfileSeqButton.textContent = 'Iniciar Sequência';
  if (state.skinning.profile) {
    const intervalSec = (sequence.intervalMs / 1000).toFixed(1);
    setAniSequenceMeta(`Sequência: inativa (${sequence.scope === 'all' ? 'todos os weapons' : 'weapon atual'}, ${intervalSec}s).`);
  } else {
    setAniSequenceMeta('Sequência: inativa.');
  }
}

function resetProfileBank() {
  state.skinning.profileBank = null;
  stopProfileSequence();
}

function ensureProfileBank(profile) {
  const current = state.skinning.profileBank;
  if (current && current.entryIndex === profile.entry.index) {
    return current;
  }

  const next = {
    entryIndex: profile.entry.index,
    loadedPathKeys: new Set(),
    byAniPath: new Map(),
    byRecordKey: new Map(),
    loadedCount: 0,
    failedCount: 0,
  };
  state.skinning.profileBank = next;
  return next;
}

function getProfileBankAni(record) {
  const bank = state.skinning.profileBank;
  if (!bank || !record || !record.aniPath) {
    return null;
  }

  return bank.byAniPath.get(toPathKey(record.aniPath)) || null;
}

function getRecordsForSequenceScope(profile, scope, weaponTypeValue) {
  if (!profile || !profile.records || profile.records.length === 0) {
    return [];
  }

  if (scope === 'all') {
    return [...profile.records].sort((a, b) => {
      if (a.weaponType !== b.weaponType) {
        return a.weaponType - b.weaponType;
      }
      if (a.motionIndex !== b.motionIndex) {
        return a.motionIndex - b.motionIndex;
      }
      return a.tableIndex - b.tableIndex;
    });
  }

  const weaponType = Number(weaponTypeValue);
  return profile.records
    .filter((record) => record.weaponType === weaponType)
    .sort((a, b) => {
      if (a.motionIndex !== b.motionIndex) {
        return a.motionIndex - b.motionIndex;
      }
      return a.tableIndex - b.tableIndex;
    });
}

function updateSequenceIntervalMs() {
  const value = Number(sequenceIntervalEl.value || 2);
  const sequence = state.skinning.sequence;
  sequence.intervalMs = Math.max(200, Math.round((Number.isFinite(value) ? value : 2) * 1000));
}

function updateSequenceSummary(scope, records) {
  const intervalSec = (state.skinning.sequence.intervalMs / 1000).toFixed(1);
  const label = scope === 'all' ? 'todos os weapons' : `weapon ${aniProfileWeaponEl.value || 'n/d'}`;
  setAniSequenceMeta(`Sequência: ${records.length} clips em ${label}, intervalo ${intervalSec}s.`);
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function getTerrainSceneScale() {
  const value = Number(terrainScaleEl.value || state.startup.terrainScale);
  return Number.isFinite(value) ? clamp(value, 0.02, 0.20) : 0.08;
}

function canRunPlayerRuntime() {
  const tileMap = state.field.summary?.tileMap;
  const hasTerrain = tileMap && Array.isArray(tileMap.heightGrid) && tileMap.heightGrid.length > 0;
  return Boolean(state.player.enabled && hasTerrain && renderer.mesh.ready);
}

function sampleTerrainHeightAtScene(xScene, zScene) {
  const grid = state.field.summary?.tileMap?.heightGrid;
  if (!Array.isArray(grid) || grid.length <= 0 || !Array.isArray(grid[0]) || grid[0].length <= 0) {
    return 0;
  }

  const sizeZ = grid.length;
  const sizeX = grid[0].length;
  const halfX = (sizeX - 1) * 0.5;
  const halfZ = (sizeZ - 1) * 0.5;

  const gx = clamp(xScene + halfX, 0, sizeX - 1);
  const gz = clamp(zScene + halfZ, 0, sizeZ - 1);

  const x0 = Math.floor(gx);
  const z0 = Math.floor(gz);
  const x1 = Math.min(sizeX - 1, x0 + 1);
  const z1 = Math.min(sizeZ - 1, z0 + 1);
  const tx = gx - x0;
  const tz = gz - z0;

  const h00 = Number(grid[z0]?.[x0] ?? 0);
  const h10 = Number(grid[z0]?.[x1] ?? h00);
  const h01 = Number(grid[z1]?.[x0] ?? h00);
  const h11 = Number(grid[z1]?.[x1] ?? h00);
  const h0 = (h00 * (1 - tx)) + (h10 * tx);
  const h1 = (h01 * (1 - tx)) + (h11 * tx);
  const h = (h0 * (1 - tz)) + (h1 * tz);
  return (h * 0.1) + 0.02;
}

function clampScenePositionToFieldBounds(x, z) {
  const grid = state.field.summary?.tileMap?.heightGrid;
  if (!Array.isArray(grid) || grid.length <= 0 || !Array.isArray(grid[0]) || grid[0].length <= 0) {
    return { x, z };
  }

  const sizeZ = grid.length;
  const sizeX = grid[0].length;
  const halfX = (sizeX - 1) * 0.5;
  const halfZ = (sizeZ - 1) * 0.5;
  return {
    x: clamp(x, -halfX, halfX),
    z: clamp(z, -halfZ, halfZ),
  };
}

function clampPlayerToFieldBounds() {
  const clamped = clampScenePositionToFieldBounds(state.player.worldX, state.player.worldZ);
  state.player.worldX = clamped.x;
  state.player.worldZ = clamped.z;
}

function clearPlayerTransform() {
  renderer.setMeshWorldTransform({ enabled: false });
  renderer.setMeshSceneScale(1.0);
  renderer.setCameraTarget([0, 0, 0], false);
  renderer.setCameraMode('orbit');
}

function stopPlayerLoop() {
  if (state.player.loopTimer != null) {
    clearInterval(state.player.loopTimer);
    state.player.loopTimer = null;
  }
  state.player.lastUpdateMs = 0;
}

function clearPlayerDestination() {
  state.player.destination.active = false;
  state.player.destination.source = '';
}

function setPlayerDestination(worldX, worldZ, source = 'minimap') {
  const clamped = clampScenePositionToFieldBounds(worldX, worldZ);
  state.player.destination.active = true;
  state.player.destination.worldX = clamped.x;
  state.player.destination.worldZ = clamped.z;
  state.player.destination.source = source;
  state.player.stuckMs = 0;
}

function spawnPlayerAt(worldX, worldZ, options = {}) {
  const clamped = clampScenePositionToFieldBounds(worldX, worldZ);
  const player = state.player;
  player.worldX = clamped.x;
  player.worldZ = clamped.z;
  player.worldY = sampleTerrainHeightAtScene(clamped.x, clamped.z);
  player.active = true;
  player.stuckMs = 0;
  player.blockedLastFrame = false;
  if (options.clearDestination !== false) {
    clearPlayerDestination();
  }
  drawFieldPreview(state.field.summary);
}

function updateObjectTypeRadiusHints(groups) {
  const byType = state.field.objectTypeRadiusByType;
  if (!Array.isArray(groups)) {
    return;
  }

  for (const group of groups) {
    const objType = Number(group?.objType);
    const radiusRaw = Number(group?.geometry?.bounds?.radius);
    if (!Number.isFinite(objType) || objType < 0 || !Number.isFinite(radiusRaw)) {
      continue;
    }

    const radius = clamp(radiusRaw, 0.20, 7.5);
    const current = byType.get(objType);
    if (!Number.isFinite(current) || radius > current) {
      byType.set(objType, radius);
    }
  }
}

function estimateFieldObjectCollisionRadius(item) {
  const objType = Number(item?.objType ?? -1);
  const typeHint = state.field.objectTypeRadiusByType.get(objType);
  const scaleH = item?.hasScale ? Number(item?.scaleH ?? 1.0) : 1.0;
  const scaleV = item?.hasScale ? Number(item?.scaleV ?? 1.0) : 1.0;
  const scale = Math.max(0.25, Math.min(4.0, Math.max(Math.abs(scaleH), Math.abs(scaleV))));
  const base = Number.isFinite(typeHint) ? typeHint : 0.75;
  return clamp(base * scale, 0.22, 8.0);
}

function rebuildFieldCollisionProxies(objects) {
  const list = Array.isArray(objects) ? objects : [];
  const proxies = [];
  for (let index = 0; index < list.length; index += 1) {
    const item = list[index];
    const radius = estimateFieldObjectCollisionRadius(item);
    if (!Number.isFinite(radius) || radius <= 0.02) {
      continue;
    }
    proxies.push({
      index,
      objType: Number(item?.objType ?? -1),
      worldX: fieldCoordToScene(Number(item?.x ?? 0)),
      worldZ: fieldCoordToScene(Number(item?.y ?? 0)),
      radius,
    });
  }
  state.field.collisionProxies = proxies;
}

function resolvePlayerCollision(nextX, nextZ) {
  const player = state.player;
  const clamped = clampScenePositionToFieldBounds(nextX, nextZ);
  if (!player.collisionEnabled) {
    return {
      worldX: clamped.x,
      worldZ: clamped.z,
      blocked: false,
    };
  }

  const proxies = state.field.collisionProxies;
  if (!Array.isArray(proxies) || proxies.length === 0) {
    return {
      worldX: clamped.x,
      worldZ: clamped.z,
      blocked: false,
    };
  }

  let x = clamped.x;
  let z = clamped.z;
  let blocked = false;
  const minBase = Math.max(0.05, player.radius + player.collisionPadding);

  for (let iter = 0; iter < 2; iter += 1) {
    for (const proxy of proxies) {
      const minDist = minBase + Number(proxy.radius || 0);
      const dx = x - proxy.worldX;
      const dz = z - proxy.worldZ;
      const distSq = (dx * dx) + (dz * dz);
      if (distSq >= minDist * minDist) {
        continue;
      }

      blocked = true;
      const dist = Math.sqrt(Math.max(distSq, 1e-7));
      const nx = dist > 1e-5 ? dx / dist : 1.0;
      const nz = dist > 1e-5 ? dz / dist : 0.0;
      const push = (minDist - dist) + 1e-3;
      x += nx * push;
      z += nz * push;
    }
    const bounded = clampScenePositionToFieldBounds(x, z);
    x = bounded.x;
    z = bounded.z;
  }

  return {
    worldX: x,
    worldZ: z,
    blocked,
  };
}

function normalizeAngleRadians(angle) {
  let next = Number(angle) || 0;
  while (next > Math.PI) next -= Math.PI * 2;
  while (next < -Math.PI) next += Math.PI * 2;
  return next;
}

function interpolateAngleRadians(from, to, alpha) {
  let delta = normalizeAngleRadians(to) - normalizeAngleRadians(from);
  while (delta > Math.PI) delta -= Math.PI * 2;
  while (delta < -Math.PI) delta += Math.PI * 2;
  return normalizeAngleRadians(from + (delta * clamp(alpha, 0, 1)));
}

function resolveMotionRecordForMode(profile, weaponType, mode) {
  const fallbackByMode = {
    idle: [0, 1, 25, 26, 27, 12],
    walk: [2, 1, 0, 3],
    run: [3, 2, 1, 0],
  };
  const candidateMotions = fallbackByMode[mode] || fallbackByMode.idle;
  for (const motionIndex of candidateMotions) {
    const record = findProfileRecord(profile.records, weaponType, motionIndex);
    if (record && Number(record.motionIndex) === Number(motionIndex) && record.aniPath) {
      return { record, motionIndex };
    }
  }
  const fallback = findProfileRecord(profile.records, weaponType, 0);
  if (!fallback || !fallback.aniPath) {
    return null;
  }
  return {
    record: fallback,
    motionIndex: Number(fallback.motionIndex ?? 0),
  };
}

async function applyPlayerMotion(mode) {
  const player = state.player;
  const profile = state.skinning.profile;
  if (!profile || !Array.isArray(profile.records) || profile.records.length <= 0) {
    return;
  }

  if (player.motionBusy) {
    return;
  }

  const preferredWeapon = Number(aniProfileWeaponEl.value || profile.recommended?.weaponType || 0);
  const resolved = resolveMotionRecordForMode(profile, preferredWeapon, mode);
  if (!resolved) {
    return;
  }

  if (resolved.motionIndex === player.currentMotionIndex) {
    return;
  }

  player.motionBusy = true;
  try {
    const ok = await applyProfileRecord(resolved.record, 'player-runtime');
    if (ok) {
      player.currentMotionIndex = resolved.motionIndex;
    }
  } finally {
    player.motionBusy = false;
  }
}

function refreshPlayerMeta() {
  const player = state.player;
  if (!player.enabled) {
    setPlayerMeta('Personagem: inativo (ative e use WASD + Shift; clique no minimapa para destino, Shift+clique para teleport).');
    return;
  }
  if (!canRunPlayerRuntime()) {
    setPlayerMeta('Personagem: aguardando mesh + field com terreno.');
    return;
  }

  const modeLabel = player.mode === 'run' ? 'RUN' : player.mode === 'walk' ? 'WALK' : 'IDLE';
  const destinationText = player.destination.active
    ? ` -> (${player.destination.worldX.toFixed(2)}, ${player.destination.worldZ.toFixed(2)})`
    : ' -> n/d';
  const collisionText = player.collisionEnabled
    ? `colisão=${player.blockedLastFrame ? 'bloqueada' : 'ok'}`
    : 'colisão=off';
  setPlayerMeta(
    `Personagem: ativo | mode=${modeLabel} | pos=(${player.worldX.toFixed(2)}, ${player.worldY.toFixed(2)}, ${player.worldZ.toFixed(2)}) | yaw=${player.yaw.toFixed(2)} | alvo${destinationText} | ${collisionText} | WASD+Shift`,
  );
}

function updatePlayerFrame() {
  const player = state.player;
  if (!player.enabled) {
    return;
  }

  if (!canRunPlayerRuntime()) {
    player.active = false;
    refreshPlayerMeta();
    return;
  }

  const now = performance.now();
  if (player.lastUpdateMs <= 0) {
    player.lastUpdateMs = now;
  }
  const dt = Math.min(0.08, Math.max(0.0, (now - player.lastUpdateMs) / 1000.0));
  player.lastUpdateMs = now;

  if (!player.active) {
    player.worldX = 0;
    player.worldZ = 0;
    player.worldY = sampleTerrainHeightAtScene(player.worldX, player.worldZ);
    player.yaw = 0;
    player.active = true;
    clearPlayerDestination();
  }

  let dirX = 0;
  let dirZ = 0;
  let isKeyboardMove = false;
  let speed = 0;

  if (player.keyState.forward) {
    dirZ -= 1;
    isKeyboardMove = true;
  }
  if (player.keyState.backward) {
    dirZ += 1;
    isKeyboardMove = true;
  }
  if (player.keyState.left) {
    dirX -= 1;
    isKeyboardMove = true;
  }
  if (player.keyState.right) {
    dirX += 1;
    isKeyboardMove = true;
  }

  if (isKeyboardMove) {
    clearPlayerDestination();
    speed = player.keyState.run ? player.runSpeed : player.walkSpeed;
  } else if (player.destination.active) {
    const toX = player.destination.worldX - player.worldX;
    const toZ = player.destination.worldZ - player.worldZ;
    const dist = Math.hypot(toX, toZ);
    if (dist <= 0.12) {
      clearPlayerDestination();
      player.stuckMs = 0;
    } else {
      dirX = toX / dist;
      dirZ = toZ / dist;
      const shouldRun = player.autoRunEnabled && dist > 4.6;
      speed = shouldRun ? player.runSpeed : player.walkSpeed;
    }
  }

  const moveLength = Math.hypot(dirX, dirZ);
  const moving = moveLength > 1e-6 && speed > 0.01;
  if (moving) {
    const inv = 1 / moveLength;
    const moveX = dirX * inv;
    const moveZ = dirZ * inv;
    let step = speed * dt;
    if (!isKeyboardMove && player.destination.active) {
      const remainX = player.destination.worldX - player.worldX;
      const remainZ = player.destination.worldZ - player.worldZ;
      step = Math.min(step, Math.hypot(remainX, remainZ));
    }

    const desiredX = player.worldX + (moveX * step);
    const desiredZ = player.worldZ + (moveZ * step);
    const resolved = resolvePlayerCollision(desiredX, desiredZ);
    const progress = Math.hypot(resolved.worldX - player.worldX, resolved.worldZ - player.worldZ);
    player.blockedLastFrame = resolved.blocked && progress < Math.max(0.015, step * 0.45);

    player.worldX = resolved.worldX;
    player.worldZ = resolved.worldZ;
    clampPlayerToFieldBounds();

    if (player.destination.active && !isKeyboardMove) {
      if (player.blockedLastFrame) {
        player.stuckMs += dt * 1000;
        if (player.stuckMs > 1200) {
          clearPlayerDestination();
          player.stuckMs = 0;
        }
      } else {
        player.stuckMs = Math.max(0, player.stuckMs - (dt * 400));
      }
    } else {
      player.stuckMs = 0;
    }

    const desiredYaw = Math.atan2(moveX, moveZ);
    player.yaw = interpolateAngleRadians(player.yaw, desiredYaw, dt * 13.0);
    player.mode = speed >= (player.walkSpeed + 0.35) ? 'run' : 'walk';
  } else {
    player.mode = 'idle';
    player.blockedLastFrame = false;
    player.stuckMs = Math.max(0, player.stuckMs - (dt * 800));
  }

  player.worldY = sampleTerrainHeightAtScene(player.worldX, player.worldZ);
  const sceneScale = getTerrainSceneScale();
  renderer.setMeshSceneScale(sceneScale);
  renderer.setMeshWorldTransform({
    enabled: true,
    x: player.worldX,
    y: player.worldY,
    z: player.worldZ,
    yaw: player.yaw,
  });
  renderer.setCameraTarget(
    [
      player.worldX * sceneScale,
      (player.worldY * sceneScale) + 0.15,
      player.worldZ * sceneScale,
    ],
    true,
  );
  renderer.setCameraMode(player.cameraMode);
  renderer.setCameraFollowYaw(player.yaw);
  renderer.setCameraFollowOptions({
    distance: player.cameraDistance,
    height: player.cameraHeight,
  });

  void applyPlayerMotion(player.mode);
  if (now - state.field.lastPreviewPlayerStampMs > 140) {
    drawFieldPreview(state.field.summary);
    state.field.lastPreviewPlayerStampMs = now;
  }
  refreshPlayerMeta();
}

function ensurePlayerLoop() {
  if (state.player.loopTimer != null) {
    return;
  }

  state.player.loopTimer = setInterval(() => {
    updatePlayerFrame();
  }, 33);
}

function setPlayerEnabled(enabled) {
  const player = state.player;
  player.enabled = Boolean(enabled);
  togglePlayerModeEl.value = player.enabled ? 'on' : 'off';

  if (!player.enabled) {
    player.active = false;
    player.mode = 'idle';
    player.currentMotionIndex = -1;
    clearPlayerDestination();
    stopPlayerLoop();
    clearPlayerTransform();
    drawFieldPreview(state.field.summary);
    refreshPlayerMeta();
    return;
  }

  stopProfileSequence();
  renderer.setCameraMode(player.cameraMode);
  renderer.setCameraFollowOptions({
    distance: player.cameraDistance,
    height: player.cameraHeight,
  });
  ensurePlayerLoop();
  drawFieldPreview(state.field.summary);
  refreshPlayerMeta();
}

async function preloadProfileAniBank({ scope = 'weapon', silent = false } = {}) {
  const profile = state.skinning.profile;
  if (!profile || !profile.records || profile.records.length === 0) {
    return {
      ok: false,
      loaded: 0,
      failed: 0,
      total: 0,
    };
  }

  const records = getRecordsForSequenceScope(profile, scope, aniProfileWeaponEl.value);
  if (records.length === 0) {
    if (!silent) {
      setAniProfileMeta(`Perfil ${profile.entry.basePath}: nenhum clip para o escopo selecionado.`);
    }
    return {
      ok: false,
      loaded: 0,
      failed: 0,
      total: 0,
    };
  }

  const bank = ensureProfileBank(profile);
  const pending = [];
  for (const record of records) {
    const pathKey = toPathKey(record.aniPath);
    if (bank.loadedPathKeys.has(pathKey)) {
      continue;
    }
    pending.push(record);
  }

  if (pending.length === 0) {
    if (!silent) {
      setAniProfileMeta(`Perfil ${profile.entry.basePath}: clips já em cache (${records.length}).`);
      updateSequenceSummary(scope, records);
    }
    return {
      ok: true,
      loaded: 0,
      failed: 0,
      total: records.length,
    };
  }

  let loaded = 0;
  let failed = 0;
  let index = 0;
  const workerCount = Math.max(1, Math.min(PROFILE_PRELOAD_CONCURRENCY, pending.length));

  if (!silent) {
    setStatus(`Precarregando clips ANI do perfil (${pending.length})...`);
  }

  const workers = Array.from({ length: workerCount }, async () => {
    while (index < pending.length) {
      const record = pending[index];
      index += 1;
      try {
        const aniParsed = await loadAniParsed(record.aniPath);
        const aniObject = aniParsed.ani;
        const pathKey = toPathKey(record.aniPath);
        bank.loadedPathKeys.add(pathKey);
        bank.byAniPath.set(pathKey, aniObject);
        bank.byRecordKey.set(makeProfileRecordKey(record), {
          record,
          ani: aniObject,
        });
        loaded += 1;
      } catch (error) {
        failed += 1;
        appendLog(`[Profile Preload] falha em ${record.aniPath}: ${error.message}`);
      }
    }
  });

  await Promise.all(workers);
  bank.loadedCount += loaded;
  bank.failedCount += failed;

  if (!silent) {
    updateSequenceSummary(scope, records);
    setAniProfileMeta(`Perfil ${profile.entry.basePath}: preload concluído (${loaded} ok, ${failed} falhas, escopo ${scope}).`);
    setStatus(failed === 0
      ? 'Preload de clips ANI concluído.'
      : `Preload concluído com falhas (${failed}).`, failed > 0);
  }

  return {
    ok: loaded > 0 || failed === 0,
    loaded,
    failed,
    total: records.length,
  };
}

async function applyProfileRecord(record, sourceLabel) {
  ensurePathOption(aniSelectEl, record.aniPath);
  aniSelectEl.value = record.aniPath;

  const profile = state.skinning.profile;
  const bonPath = profile?.bonPath || profile?.entry?.bonPath || '';
  if (bonPath) {
    ensurePathOption(bonSelectEl, bonPath);
    bonSelectEl.value = bonPath;
  }

  const preloadedAni = getProfileBankAni(record);
  return applySkinningFromSelectedBundle({
    source: sourceLabel,
    aniPathOverride: record.aniPath,
    bonPathOverride: bonPath,
    aniOverride: preloadedAni || undefined,
  });
}

async function runProfileSequenceStep() {
  const sequence = state.skinning.sequence;
  const profile = state.skinning.profile;
  if (!sequence.active || sequence.busy || !profile) {
    return;
  }

  const records = getRecordsForSequenceScope(profile, sequence.scope, aniProfileWeaponEl.value);
  if (records.length === 0) {
    stopProfileSequence();
    return;
  }

  const record = records[sequence.position % records.length];
  sequence.position = (sequence.position + 1) % records.length;
  sequence.busy = true;

  try {
    if (sequence.scope === 'all' && record.weaponType !== Number(aniProfileWeaponEl.value)) {
      refreshProfileWeaponAndMotionControls(profile, record.weaponType, record.motionIndex);
    } else {
      aniProfileMotionEl.value = String(record.motionIndex);
    }

    const ok = await applyProfileRecord(record, 'profile-sequence');
    if (ok) {
      setAniSequenceMeta(`Sequência ativa: weapon ${record.weaponType}, ${motionLabel(record.motionIndex)} -> ${record.aniPath}`);
    }
  } finally {
    sequence.busy = false;
  }
}

async function toggleProfileSequence() {
  const sequence = state.skinning.sequence;
  const profile = state.skinning.profile;
  if (!profile || !profile.records || profile.records.length === 0) {
    setAniSequenceMeta('Sequência: perfil indisponível.');
    return;
  }

  updateSequenceIntervalMs();
  sequence.scope = sequenceScopeEl.value === 'all' ? 'all' : 'weapon';

  if (sequence.active) {
    stopProfileSequence();
    return;
  }

  const preloadResult = await preloadProfileAniBank({
    scope: sequence.scope,
    silent: true,
  });
  if (!preloadResult.ok && preloadResult.total === 0) {
    setAniSequenceMeta('Sequência: sem clips no escopo atual.');
    return;
  }

  sequence.active = true;
  sequence.position = 0;
  toggleProfileSeqButton.textContent = 'Parar Sequência';
  updateSequenceSummary(sequence.scope, getRecordsForSequenceScope(profile, sequence.scope, aniProfileWeaponEl.value));

  await runProfileSequenceStep();
  if (!sequence.active) {
    return;
  }

  sequence.timer = setInterval(() => {
    void runProfileSequenceStep();
  }, sequence.intervalMs);
}

function stopSkinningPlayback() {
  stopProfileSequence();
  state.skinning.context = null;
  state.skinning.ani = null;
  state.skinning.bon = null;
  state.skinning.aniPath = '';
  state.skinning.bonPath = '';
  state.skinning.frameFloat = 0;
  state.skinning.lastUpdateMs = 0;
  state.skinning.playing = true;
  state.skinning.profileBank = null;
  toggleAniButton.textContent = 'Pausar ANI';
  aniFrameEl.min = '0';
  aniFrameEl.max = '0';
  aniFrameEl.value = '0';
  aniFrameEl.disabled = true;
}

function ensureSkinningLoop() {
  if (state.skinningTimer != null) {
    return;
  }

  state.skinningTimer = setInterval(() => {
    const skin = state.skinning;
    if (!skin.context || !skin.ani || !skin.playing) {
      return;
    }

    const now = performance.now();
    if (skin.lastUpdateMs <= 0) {
      skin.lastUpdateMs = now;
      return;
    }

    const dt = Math.min(0.25, Math.max(0.0, (now - skin.lastUpdateMs) / 1000.0));
    skin.lastUpdateMs = now;

    const tickCount = Math.max(1, skin.ani.tickCount);
    skin.frameFloat += dt * 30.0 * skin.speed;
    const tick = Math.floor(skin.frameFloat) % tickCount;

    const result = applySkinningAtTick(skin.context, tick);
    renderer.updateMeshDeformation(result.positions, result.normals);

    aniFrameEl.value = String(tick);
    setAniMeta(`ANI ativo: ${skin.aniPath} | BON: ${skin.bonPath || 'n/d'} | frame ${tick + 1}/${tickCount} | speed ${skin.speed.toFixed(2)}x`);
  }, 33);
}

async function loadAniParsed(aniPath) {
  const normalizedPath = normalizeMeshPath(aniPath);
  const key = toPathKey(normalizedPath);

  const cached = lruGet(state.aniCache, key);
  if (cached) {
    return {
      ani: cached,
      cacheHit: true,
      path: normalizedPath,
    };
  }

  const aniProbe = await assetClient.fetchBuffer(normalizedPath);
  const ani = parseWydAniBuffer(aniProbe.buffer);
  lruSet(state.aniCache, key, ani, MAX_ANI_CACHE);

  return {
    ani,
    cacheHit: false,
    path: normalizedPath,
    probe: aniProbe,
  };
}

async function loadBonParsed(bonPath) {
  const normalizedPath = normalizeMeshPath(bonPath);
  if (!normalizedPath) {
    return {
      bon: null,
      cacheHit: true,
      path: '',
    };
  }

  const key = toPathKey(normalizedPath);
  const cached = lruGet(state.bonCache, key);
  if (cached) {
    return {
      bon: cached,
      cacheHit: true,
      path: normalizedPath,
    };
  }

  const bonProbe = await assetClient.fetchBuffer(normalizedPath);
  const bon = parseWydBonBuffer(bonProbe.buffer);
  lruSet(state.bonCache, key, bon, MAX_BON_CACHE);

  return {
    bon,
    cacheHit: false,
    path: normalizedPath,
    probe: bonProbe,
  };
}

async function loadMeshTexture(meshPath) {
  const candidateWys = meshPath.replace(/\.msh$/i, '.wys');
  const candidateWyt = meshPath.replace(/\.msh$/i, '.wyt');

  try {
    const wysProbe = await assetClient.fetchBuffer(candidateWys);
    const decodedWys = decodeWysBuffer(wysProbe.buffer);
    const ddsUpload = renderer.loadCompressedDdsTexture(decodedWys);

    appendLog(`[Mesh Texture] ${wysProbe.resolved.requestedPath} -> ${wysProbe.resolved.resolvedPath}`);
    appendLog(`  decode=${decodedWys.format} ${decodedWys.width}x${decodedWys.height} mips=${decodedWys.mipMapCount}`);

    if (ddsUpload.ok) {
      appendLog('  webgl-dds=ok');
      return;
    }

    appendLog(`  webgl-dds=fallback (${ddsUpload.reason})`);
  } catch (error) {
    appendLog(`[Mesh Texture] WYS indisponível: ${error.message}`);
  }

  try {
    const wytProbe = await assetClient.fetchBuffer(candidateWyt);
    const decodedWyt = decodeWytBuffer(wytProbe.buffer);
    renderer.loadTextureFromPixels(decodedWyt.width, decodedWyt.height, decodedWyt.rgba);

    appendLog(`[Mesh Texture] ${wytProbe.resolved.requestedPath} -> ${wytProbe.resolved.resolvedPath}`);
    appendLog(`  decode=${decodedWyt.format} ${decodedWyt.width}x${decodedWyt.height} depth=${decodedWyt.pixelDepth}`);
    appendLog('  webgl-wyt=ok');
    return;
  } catch (error) {
    appendLog(`[Mesh Texture] WYT indisponível: ${error.message}`);
  }

  const fallbackWytProbe = await assetClient.fetchBuffer('UI\\Inventory2.wyt');
  const fallbackWyt = decodeWytBuffer(fallbackWytProbe.buffer);
  renderer.loadTextureFromPixels(fallbackWyt.width, fallbackWyt.height, fallbackWyt.rgba);
  appendLog('[Mesh Texture] fallback global para UI\\Inventory2.wyt');
}

async function loadMeshRelatedAssets(meshPath) {
  try {
    const response = await assetClient.fetchJson(`/api/assets/mesh-related?mesh=${encodeURIComponent(meshPath)}`);
    return response.related || {
      aniCandidates: [],
      bonCandidates: [],
      textureCandidates: [],
    };
  } catch (error) {
    appendLog(`[Related] indisponível: ${error.message}`);
    return {
      aniCandidates: [],
      bonCandidates: [],
      textureCandidates: [],
    };
  }
}

async function loadMeshAnimationProfile(meshPath) {
  try {
    const response = await assetClient.fetchJson(`/api/assets/mesh-animation-profile?mesh=${encodeURIComponent(meshPath)}`);
    if (!response || !response.profile || !response.profile.entry) {
      return null;
    }

    const profile = response.profile;
    const normalizedRecords = (profile.records || []).map(normalizeProfileRecord).filter((record) => record.exists);
    const normalizedProfile = {
      meshPath: normalizeMeshPath(profile.meshPath || meshPath),
      entry: {
        index: Number(profile.entry.index || 0),
        aniTypeCount: Number(profile.entry.aniTypeCount || 0),
        parts: Number(profile.entry.parts || 0),
        basePath: normalizeMeshPath(profile.entry.basePath || ''),
        baseStem: String(profile.entry.baseStem || '').toLowerCase(),
        bonPath: normalizeMeshPath(profile.entry.bonPath || ''),
        bonExists: Boolean(profile.entry.bonExists),
        existingAniCount: Number(profile.entry.existingAniCount || 0),
      },
      bonPath: normalizeMeshPath(profile.bonPath || ''),
      records: normalizedRecords,
      matchCandidates: Array.isArray(profile.matchCandidates) ? profile.matchCandidates : [],
      availableWeaponTypes: Array.isArray(profile.availableWeaponTypes)
        ? profile.availableWeaponTypes.map((value) => Number(value)).filter((value) => Number.isFinite(value)).sort((a, b) => a - b)
        : [],
      recommended: profile.recommended ? normalizeProfileRecord(profile.recommended) : null,
    };

    if (!normalizedProfile.recommended && normalizedProfile.records.length > 0) {
      normalizedProfile.recommended = pickRecommendedProfileRecord(normalizedProfile.records);
    }

    return normalizedProfile;
  } catch (error) {
    appendLog(`[Animation Profile] indisponível: ${error.message}`);
    return null;
  }
}

async function fetchCatalogEntryProfile(entryIndex) {
  const activeProfile = state.skinning.profile;
  if (!activeProfile) {
    return null;
  }

  const response = await assetClient.fetchJson(`/api/assets/bone-ani-catalog?index=${entryIndex}&full=1`);
  if (!response || !response.entry) {
    return null;
  }

  return buildProfileFromCatalogEntry(activeProfile.meshPath || state.currentMeshPath, activeProfile, response.entry);
}

async function applySkinningFromSelectedBundle({
  source = 'manual',
  aniPathOverride = '',
  bonPathOverride = '',
  aniOverride = undefined,
  bonOverride = undefined,
} = {}) {
  const skin = state.skinning;
  if (!skin.mesh) {
    setAniMeta('Animação: carregue uma mesh antes.');
    return false;
  }

  const selectedAni = normalizeMeshPath(aniPathOverride || aniSelectEl.value);
  const selectedBon = normalizeMeshPath(bonPathOverride || bonSelectEl.value);

  if (!selectedAni) {
    setAniMeta('Animação: selecione um arquivo ANI para ativar playback.');
    return false;
  }

  let ani = null;
  let bon = null;

  const bankAni = aniOverride === undefined
    ? state.skinning.profileBank?.byAniPath?.get(toPathKey(selectedAni))
    : null;
  const effectiveAniOverride = aniOverride !== undefined ? aniOverride : bankAni;

  if (effectiveAniOverride != null) {
    ani = effectiveAniOverride;
    appendLog(`[ANI Parse] profile-bank hit: ${selectedAni}`);
  } else {
    try {
      const aniParsed = await loadAniParsed(selectedAni);
      ani = aniParsed.ani;

      if (aniParsed.cacheHit) {
        appendLog(`[ANI Parse] cache hit: ${selectedAni}`);
      } else {
        appendLog(`[ANI Parse] ${aniParsed.probe.resolved.requestedPath} -> ${aniParsed.probe.resolved.resolvedPath}`);
        appendLog(`  ticks=${ani.tickCount} bonesPerFrame=${ani.bonesPerFrame} matrices=${ani.matrixCount}/${ani.expectedMatrixCount}`);
        appendLog(`  payload=${ani.payloadBytes} expected=${ani.expectedPayloadBytes}`);
      }
    } catch (error) {
      setAniMeta(`Falha no ANI (${selectedAni}): ${error.message}`);
      appendLog(`[ANI Parse] erro: ${error.message}`);
      return false;
    }
  }

  if (selectedBon) {
    if (bonOverride !== undefined) {
      bon = bonOverride;
      if (bon) {
        appendLog(`[BON Parse] profile-bank hit: ${selectedBon}`);
      }
    } else {
      try {
        const bonParsed = await loadBonParsed(selectedBon);
        bon = bonParsed.bon;
        if (bonParsed.cacheHit) {
          appendLog(`[BON Parse] cache hit: ${selectedBon}`);
        } else {
          appendLog(`[BON Parse] ${bonParsed.probe.resolved.requestedPath} -> ${bonParsed.probe.resolved.resolvedPath}`);
          appendLog(`  pairs=${bon.pairCount} maxBoneId=${bon.maxBoneId}`);
        }
      } catch (error) {
        appendLog(`[BON Parse] erro (${selectedBon}): ${error.message}`);
        setAniMeta(`ANI carregado, BON inválido (${error.message})`);
        bon = null;
      }
    }
  }

  try {
    const context = createSkinningContext(skin.mesh, ani, bon);

    skin.context = context;
    skin.ani = ani;
    skin.bon = bon;
    skin.aniPath = selectedAni;
    skin.bonPath = selectedBon;
    skin.frameFloat = 0;
    skin.lastUpdateMs = 0;
    skin.playing = true;

    toggleAniButton.textContent = 'Pausar ANI';

    const maxFrame = Math.max(0, ani.tickCount - 1);
    aniFrameEl.disabled = false;
    aniFrameEl.min = '0';
    aniFrameEl.max = String(maxFrame);
    aniFrameEl.value = '0';

    const firstFrame = applySkinningAtTick(context, 0);
    renderer.updateMeshDeformation(firstFrame.positions, firstFrame.normals);

    setAniMeta(`ANI ativo: ${selectedAni} | BON: ${selectedBon || 'n/d'} | frame 1/${Math.max(1, ani.tickCount)} | speed ${skin.speed.toFixed(2)}x`);
    appendLog(`[Skinning] deformação CPU ativa no viewer (fonte=${source}).`);
    ensureSkinningLoop();
    return true;
  } catch (error) {
    setAniMeta(`Falha ao ativar skinning: ${error.message}`);
    appendLog(`[Skinning] erro: ${error.message}`);
    return false;
  }
}

async function applyProfileAniSelection({ auto = false } = {}) {
  const profile = state.skinning.profile;
  if (!profile || !profile.records || profile.records.length === 0) {
    setAniProfileMeta('Perfil de animação indisponível para esta mesh.');
    return false;
  }

  const weaponType = Number(aniProfileWeaponEl.value);
  const motionIndex = Number(aniProfileMotionEl.value);
  const record = findProfileRecord(profile.records, weaponType, motionIndex);

  if (!record || !record.aniPath) {
    setAniProfileMeta('Não foi possível resolver ANI para o weapon/motion selecionado.');
    return false;
  }

  if (!auto && state.skinning.sequence.active) {
    stopProfileSequence();
  }

  setAniProfileMeta(`Perfil ${profile.entry.basePath} -> ${record.aniPath} | ${motionLabel(record.motionIndex)} | weapon ${record.weaponType}`);
  appendLog(`[Profile ANI] ${auto ? 'auto' : 'manual'} weapon=${record.weaponType} motion=${record.motionIndex} file=${record.aniPath}`);
  return applyProfileRecord(record, auto ? 'profile-auto' : 'profile-manual');
}

function fillMeshSelector(catalog, selectedPath) {
  meshSelectEl.innerHTML = '';

  for (const item of catalog) {
    const option = document.createElement('option');
    option.value = item.path;
    const flags = [
      item.hasWys ? 'WYS' : null,
      item.hasWyt ? 'WYT' : null,
      item.hasAni ? 'ANI' : null,
    ].filter(Boolean).join('/');

    option.textContent = `${item.path}${flags ? ` [${flags}]` : ''}`;
    if (item.path.toLowerCase() === selectedPath.toLowerCase()) {
      option.selected = true;
    }
    meshSelectEl.appendChild(option);
  }
}

function fillFieldSelector(catalog, selectedName) {
  fieldSelectEl.innerHTML = '';

  for (const item of catalog) {
    const option = document.createElement('option');
    option.value = item.name;
    const flags = [
      item.hasTrn ? 'TRN' : null,
      item.hasDat ? 'DAT' : null,
      item.hasMap ? 'MAP' : null,
      item.hasMinimap ? 'MM' : null,
    ].filter(Boolean).join('/');
    option.textContent = `${item.name}${flags ? ` [${flags}]` : ''}`;
    if ((item.name || '').toLowerCase() === (selectedName || '').toLowerCase()) {
      option.selected = true;
    }
    fieldSelectEl.appendChild(option);
  }
}

function colorFromObjectType(objType) {
  const value = Number(objType) >>> 0;
  const r = (value * 73) % 255;
  const g = (value * 151) % 255;
  const b = (value * 197) % 255;
  return `rgba(${r}, ${g}, ${b}, 0.95)`;
}

function fieldCoordToScene(value) {
  return (Number(value || 0) * 0.5) - 31.5;
}

function sceneCoordToField(value) {
  return (Number(value || 0) + 31.5) * 2.0;
}

function sceneToPreviewPixel(worldX, worldZ, width, height) {
  const fx = sceneCoordToField(worldX);
  const fy = sceneCoordToField(worldZ);
  const px = Math.max(0, Math.min(width - 1, Math.floor((fx / 128.0) * width)));
  const py = Math.max(0, Math.min(height - 1, Math.floor((fy / 128.0) * height)));
  return { px, py };
}

function previewToScenePosition(canvasX, canvasY, width, height) {
  const nx = clamp(canvasX / Math.max(1, width), 0, 1);
  const ny = clamp(canvasY / Math.max(1, height), 0, 1);
  const fx = nx * 128.0;
  const fy = ny * 128.0;
  return {
    worldX: fieldCoordToScene(fx),
    worldZ: fieldCoordToScene(fy),
  };
}

function drawFieldPreview(summary) {
  const ctx = fieldPreviewCanvas.getContext('2d');
  const width = fieldPreviewCanvas.width;
  const height = fieldPreviewCanvas.height;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#070d14';
  ctx.fillRect(0, 0, width, height);

  const previewImageData = state.field.previewImageData;
  if (previewImageData && previewImageData.width > 0 && previewImageData.height > 0) {
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = previewImageData.width;
    tempCanvas.height = previewImageData.height;
    const tempCtx = tempCanvas.getContext('2d');
    const image = new ImageData(previewImageData.rgba, previewImageData.width, previewImageData.height);
    tempCtx.putImageData(image, 0, 0);
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(tempCanvas, 0, 0, width, height);
  }

  const tileMap = summary?.tileMap;
  if (!previewImageData && tileMap && Array.isArray(tileMap.heightGrid) && tileMap.heightGrid.length === 64) {
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = 64;
    tempCanvas.height = 64;
    const tempCtx = tempCanvas.getContext('2d');
    const image = tempCtx.createImageData(64, 64);
    const minHeight = Number(tileMap.heightMin ?? -1);
    const maxHeight = Number(tileMap.heightMax ?? 1);
    const range = Math.max(1, maxHeight - minHeight);

    for (let y = 0; y < 64; y += 1) {
      const row = tileMap.heightGrid[y] || [];
      for (let x = 0; x < 64; x += 1) {
        const h = Number(row[x] ?? minHeight);
        const t = Math.max(0, Math.min(1, (h - minHeight) / range));
        const r = Math.round(18 + (t * 200));
        const g = Math.round(30 + (t * 170));
        const b = Math.round(42 + (t * 120));
        const idx = (y * 64 + x) * 4;
        image.data[idx + 0] = r;
        image.data[idx + 1] = g;
        image.data[idx + 2] = b;
        image.data[idx + 3] = 255;
      }
    }

    tempCtx.putImageData(image, 0, 0);
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(tempCanvas, 0, 0, width, height);
  }

  const objects = summary?.objects?.items || [];
  if (objects.length > 0) {
    for (const objectItem of objects) {
      const px = Math.max(0, Math.min(width - 1, Math.floor((Number(objectItem.x) / 128) * width)));
      const py = Math.max(0, Math.min(height - 1, Math.floor((Number(objectItem.y) / 128) * height)));
      ctx.fillStyle = colorFromObjectType(objectItem.objType);
      ctx.fillRect(px, py, 2, 2);
    }

    const selectedIndex = state.field.selectedObjectIndex;
    if (Number.isFinite(selectedIndex) && selectedIndex >= 0 && selectedIndex < objects.length) {
      const item = objects[selectedIndex];
      const px = Math.max(0, Math.min(width - 1, Math.floor((Number(item.x) / 128) * width)));
      const py = Math.max(0, Math.min(height - 1, Math.floor((Number(item.y) / 128) * height)));
      ctx.strokeStyle = 'rgba(255, 234, 118, 0.95)';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.arc(px, py, 5.0, 0, Math.PI * 2);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(px - 6, py);
      ctx.lineTo(px + 6, py);
      ctx.moveTo(px, py - 6);
      ctx.lineTo(px, py + 6);
      ctx.stroke();
    }
  }

  if (state.player.enabled && state.player.active) {
    const marker = sceneToPreviewPixel(state.player.worldX, state.player.worldZ, width, height);
    ctx.fillStyle = 'rgba(80, 235, 255, 0.95)';
    ctx.beginPath();
    ctx.arc(marker.px, marker.py, 3.8, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = 'rgba(8, 24, 32, 0.85)';
    ctx.lineWidth = 1.2;
    ctx.stroke();
  }

  if (state.player.destination.active) {
    const marker = sceneToPreviewPixel(state.player.destination.worldX, state.player.destination.worldZ, width, height);
    ctx.strokeStyle = 'rgba(255, 235, 132, 0.95)';
    ctx.lineWidth = 1.3;
    ctx.beginPath();
    ctx.arc(marker.px, marker.py, 5.2, 0, Math.PI * 2);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(marker.px - 6, marker.py);
    ctx.lineTo(marker.px + 6, marker.py);
    ctx.moveTo(marker.px, marker.py - 6);
    ctx.lineTo(marker.px, marker.py + 6);
    ctx.stroke();
  }

  ctx.strokeStyle = 'rgba(255, 255, 255, 0.15)';
  ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
}

function clearFieldObjectSelection({ redraw = true } = {}) {
  state.field.selectedObjectIndex = -1;
  renderer.setSelectedFieldObjectIndex(-1);
  setFieldObjectSelectedMeta('Objeto selecionado: nenhum (clique no canvas 3D para inspecionar).');
  if (redraw) {
    drawFieldPreview(state.field.summary);
  }
}

function applyFieldObjectSelection(pickResult) {
  if (!pickResult || !pickResult.item) {
    return;
  }

  state.field.selectedObjectIndex = pickResult.index;
  renderer.setSelectedFieldObjectIndex(pickResult.index);
  drawFieldPreview(state.field.summary);

  const item = pickResult.item;
  const posText = `(${item.x.toFixed(2)}, ${item.y.toFixed(2)}, h=${item.height.toFixed(2)})`;
  const scaleText = item.hasScale ? `${item.scaleH.toFixed(2)}x${item.scaleV.toFixed(2)}` : '1.00x1.00';
  setFieldObjectSelectedMeta(
    `Selecionado #${pickResult.index} | type=${item.objType} | pos=${posText} | angle=${item.angle.toFixed(2)} | tex=${item.textureSetIndex} | mask=${item.maskIndex} | scale=${scaleText}`,
  );
}

function spawnPlayerAtSelectedObject() {
  const objects = state.field.summary?.objects?.items;
  const selected = Number(state.field.selectedObjectIndex);
  if (!Array.isArray(objects) || selected < 0 || selected >= objects.length) {
    setStatus('Selecione um objeto do field antes de spawnar o personagem.', true);
    return;
  }

  if (!state.player.enabled) {
    setPlayerEnabled(true);
  }

  const item = objects[selected];
  const worldX = fieldCoordToScene(Number(item.x || 0));
  const worldZ = fieldCoordToScene(Number(item.y || 0));
  spawnPlayerAt(worldX, worldZ, { clearDestination: true });
  state.player.lastUpdateMs = 0;
  setStatus(`Spawn do personagem aplicado no objeto #${selected} (type=${item.objType}).`);
  refreshPlayerMeta();
}

function collectFieldMinimapCandidates(fieldName, summary) {
  const candidates = [];
  const seen = new Set();

  const pushPath = (path) => {
    const normalized = normalizeMeshPath(path);
    if (!normalized) {
      return;
    }
    const key = normalized.toLowerCase();
    if (seen.has(key)) {
      return;
    }
    seen.add(key);
    candidates.push(normalized);
  };

  const minimap = summary?.minimap || null;
  if (minimap && typeof minimap.resolvedPath === 'string' && minimap.resolvedPath.length > 0) {
    pushPath(minimap.resolvedPath);
  }
  if (minimap && Array.isArray(minimap.candidates)) {
    const sorted = [...minimap.candidates].sort((a, b) => Number(Boolean(b.exists)) - Number(Boolean(a.exists)));
    for (const item of sorted) {
      if (item?.resolvedPath) {
        pushPath(item.resolvedPath);
      } else if (item?.requestedPath) {
        pushPath(item.requestedPath);
      }
    }
  }

  const match = String(fieldName || '').match(/field(\d{4})/i);
  if (match) {
    pushPath(`UI\\m${match[1]}.wyt`);
    pushPath(`UI\\m${match[1]}.wys`);
  }

  return candidates;
}

async function loadFieldMinimapTexture(fieldName, summary, loadToken) {
  const candidates = collectFieldMinimapCandidates(fieldName, summary);
  if (candidates.length === 0) {
    renderer.clearTerrainTexture();
    state.field.previewImageData = null;
    state.field.minimapPath = '';
    setFieldMinimapMeta(`Minimap ${fieldName}: não há candidato para este field.`);
    return false;
  }

  for (const candidate of candidates) {
    if (loadToken !== state.fieldLoadToken) {
      return false;
    }

    try {
      const probe = await assetClient.fetchBuffer(candidate);
      if (loadToken !== state.fieldLoadToken) {
        return false;
      }

      const resolvedLower = String(probe.resolved.resolvedPath || candidate).toLowerCase();
      if (resolvedLower.endsWith('.wyt')) {
        const decoded = decodeWytBuffer(probe.buffer);
        renderer.setTerrainTextureFromPixels(decoded.width, decoded.height, decoded.rgba);
        renderer.setTerrainTextureVisible(terrainTextureEnabled());
        renderer.setTerrainTextureBlend(Number(terrainTextureBlendEl.value || state.startup.terrainTextureBlend));
        state.field.previewImageData = {
          width: decoded.width,
          height: decoded.height,
          rgba: new Uint8ClampedArray(decoded.rgba),
        };
        state.field.minimapPath = probe.resolved.resolvedPath;
        setFieldMinimapMeta(`Minimap: ${probe.resolved.resolvedPath} (${decoded.width}x${decoded.height}, WYT).`);
        appendLog(`[Field Minimap] ${probe.resolved.requestedPath} -> ${probe.resolved.resolvedPath} | ${decoded.width}x${decoded.height} WYT`);
        return true;
      }

      if (resolvedLower.endsWith('.wys')) {
        const decoded = decodeWysBuffer(probe.buffer);
        const upload = renderer.loadCompressedDdsTexture(decoded, { target: 'terrain' });
        if (upload.ok) {
          renderer.setTerrainTextureVisible(terrainTextureEnabled());
          renderer.setTerrainTextureBlend(Number(terrainTextureBlendEl.value || state.startup.terrainTextureBlend));
          state.field.previewImageData = null;
          state.field.minimapPath = probe.resolved.resolvedPath;
          setFieldMinimapMeta(`Minimap: ${probe.resolved.resolvedPath} (${decoded.width}x${decoded.height}, ${decoded.fourCC}).`);
          appendLog(`[Field Minimap] ${probe.resolved.requestedPath} -> ${probe.resolved.resolvedPath} | ${decoded.width}x${decoded.height} ${decoded.fourCC}`);
          return true;
        }
        appendLog(`[Field Minimap] DDS sem suporte (${probe.resolved.resolvedPath}): ${upload.reason}`);
        continue;
      }
    } catch (error) {
      appendLog(`[Field Minimap] candidato inválido ${candidate}: ${error.message}`);
    }
  }

  renderer.clearTerrainTexture();
  state.field.previewImageData = null;
  state.field.minimapPath = '';
  setFieldMinimapMeta(`Minimap ${fieldName}: não foi possível carregar WYT/WYS.`);
  return false;
}

function getObjectMeshInstanceBudget() {
  const value = Number(objectMeshBudgetEl.value || state.startup.objectMeshBudget);
  return Math.max(128, Math.min(3200, Number.isFinite(value) ? Math.floor(value) : 640));
}

function getObjectMeshTypeBudget() {
  const value = Number(objectMeshTypesEl.value || state.startup.objectMeshTypes);
  return Math.max(4, Math.min(96, Number.isFinite(value) ? Math.floor(value) : 20));
}

async function ensureObjectMeshMapForTypes(typeList) {
  const missing = [];
  for (const value of typeList) {
    if (!state.field.objectMeshMapByType.has(value)) {
      missing.push(value);
    }
  }

  if (missing.length <= 0) {
    return;
  }

  const response = await assetClient.fetchJson(`/api/assets/object-mesh-map?types=${encodeURIComponent(missing.join(','))}`);
  const items = Array.isArray(response.items) ? response.items : [];
  for (const item of items) {
    state.field.objectMeshMapByType.set(Number(item.objType), item);
  }
}

async function loadObjectMsaParsed(meshPath) {
  const normalized = normalizeMeshPath(meshPath);
  const key = toPathKey(normalized);
  const cached = state.objectMeshCache.get(key);
  if (cached) {
    return cached;
  }

  const probe = await assetClient.fetchBuffer(normalized);
  const msa = parseWydMsaBuffer(probe.buffer, {
    sourcePath: probe.resolved.resolvedPath || normalized,
  });
  const payload = {
    meshPath: normalizeMeshPath(probe.resolved.resolvedPath || normalized),
    msa,
  };
  state.objectMeshCache.set(key, payload);
  return payload;
}

function firstAvailableTexturePath(textureCandidates) {
  if (!Array.isArray(textureCandidates)) {
    return '';
  }

  for (const candidate of textureCandidates) {
    if (!candidate || !candidate.exists) {
      continue;
    }

    if (candidate.resolvedPath) {
      return normalizeMeshPath(candidate.resolvedPath);
    }
  }

  return '';
}

function sourceGroupFolderName(value) {
  const normalized = String(value || '').trim().toLowerCase();
  if (normalized === 'effect') {
    return 'Effect';
  }
  return 'Mesh';
}

function textureStemFromRef(textureRef) {
  const normalized = normalizeMeshPath(String(textureRef || ''));
  if (!normalized) {
    return '';
  }

  const filename = normalized.split('\\').pop() || normalized;
  const stem = filename.replace(/\.[^./\\]+$/, '').trim();
  return stem;
}

function buildMsaMaterialPlans(msa, mapping, meshPath) {
  const attributes = Array.isArray(msa?.attributes) ? msa.attributes : [];
  const textureRefs = Array.isArray(msa?.textureRefs) ? msa.textureRefs : [];
  const normalizedMeshPath = normalizeMeshPath(meshPath || mapping?.resolvedPath || mapping?.sourcePath || msa?.sourcePath || '');
  const meshFileName = normalizedMeshPath.split('\\').pop() || '';
  const meshStem = meshFileName.replace(/\.[^./\\]+$/, '').trim();
  const sourceGroup = sourceGroupFolderName(mapping?.sourceGroup || normalizedMeshPath.split('\\')[0] || 'mesh');
  const meshStartsWithAA = meshStem.toLowerCase().startsWith('aa');
  const fallbackTexturePath = firstAvailableTexturePath(mapping?.textureCandidates || []);

  const createCandidateList = (preferredStem) => {
    const candidates = [];
    const seen = new Set();

    const appendCandidate = (stem, ext) => {
      const cleanStem = String(stem || '').trim();
      if (!cleanStem) {
        return;
      }
      const candidatePath = `${sourceGroup}\\${cleanStem}${ext}`;
      const key = toPathKey(candidatePath);
      if (seen.has(key)) {
        return;
      }
      seen.add(key);
      candidates.push(candidatePath);
    };

    if (preferredStem) {
      appendCandidate(preferredStem, '.wys');
      appendCandidate(preferredStem, '.wyt');
    }
    if (meshStem && (!preferredStem || meshStem.toLowerCase() !== preferredStem.toLowerCase())) {
      appendCandidate(meshStem, '.wys');
      appendCandidate(meshStem, '.wyt');
    }

    if (fallbackTexturePath) {
      const key = toPathKey(fallbackTexturePath);
      if (!seen.has(key)) {
        candidates.push(fallbackTexturePath);
      }
    }

    return candidates;
  };

  const plans = [];
  if (attributes.length > 0) {
    for (let i = 0; i < attributes.length; i += 1) {
      const attribute = attributes[i] || {};
      const faceStart = Math.max(0, Number(attribute.faceStart || 0));
      const faceCount = Math.max(0, Number(attribute.faceCount || 0));
      if (!Number.isFinite(faceCount) || faceCount <= 0) {
        continue;
      }

      let preferredStem = '';
      if (!meshStartsWithAA) {
        preferredStem = textureStemFromRef(textureRefs[i] || '');
      }
      if (!preferredStem) {
        preferredStem = meshStem;
      }

      plans.push({
        faceStart,
        faceCount,
        textureCandidates: createCandidateList(preferredStem),
      });
    }
  }

  if (plans.length <= 0) {
    plans.push({
      faceStart: 0,
      faceCount: Math.floor((Array.isArray(msa?.indices) ? msa.indices.length : 0) / 3),
      textureCandidates: createCandidateList(meshStem),
    });
  }

  return plans;
}

async function loadObjectTexturePayload(texturePath, options = {}) {
  const normalized = normalizeMeshPath(texturePath);
  if (!normalized) {
    return null;
  }

  const key = toPathKey(normalized);
  if (state.objectTextureCache.has(key)) {
    return state.objectTextureCache.get(key);
  }

  try {
    const probe = await assetClient.fetchBuffer(normalized);
    const resolvedPath = normalizeMeshPath(probe.resolved.resolvedPath || normalized);
    if (/\.wyt$/i.test(resolvedPath)) {
      const decoded = decodeWytBuffer(probe.buffer);
      const payload = {
        path: resolvedPath,
        kind: 'wyt',
        width: decoded.width,
        height: decoded.height,
        rgba: decoded.rgba,
      };
      state.objectTextureCache.set(key, payload);
      return payload;
    }

    if (/\.wys$/i.test(resolvedPath)) {
      const decoded = decodeWysBuffer(probe.buffer);
      const payload = {
        path: resolvedPath,
        kind: 'wys',
        dds: decoded,
      };
      state.objectTextureCache.set(key, payload);
      return payload;
    }
  } catch (error) {
    if (!options.silent) {
      appendLog(`[Field Obj Mesh] textura indisponível ${normalized}: ${error.message}`);
    }
  }

  state.objectTextureCache.set(key, null);
  return null;
}

async function resolveFirstObjectTexture(candidates) {
  const list = Array.isArray(candidates) ? candidates : [];
  let firstCandidate = '';
  for (const candidate of list) {
    const normalizedCandidate = normalizeMeshPath(candidate);
    if (!normalizedCandidate) {
      continue;
    }
    if (!firstCandidate) {
      firstCandidate = normalizedCandidate;
    }

    const payload = await loadObjectTexturePayload(normalizedCandidate, { silent: true });
    if (payload) {
      return {
        texturePath: payload.path || normalizedCandidate,
        texturePayload: payload,
      };
    }
  }

  return {
    texturePath: firstCandidate,
    texturePayload: null,
  };
}

function mapFieldObjectToInstance(item) {
  return {
    x: fieldCoordToScene(Number(item.x ?? 0)),
    y: Number(item.height ?? 0) * 0.1,
    z: fieldCoordToScene(Number(item.y ?? 0)),
    yaw: Number(item.angle ?? 0),
    scaleH: item.hasScale ? Number(item.scaleH ?? 1) : 1.0,
    scaleV: item.hasScale ? Number(item.scaleV ?? 1) : 1.0,
  };
}

function buildHistogramByObjectType(items) {
  const byType = new Map();
  for (const item of items) {
    const objType = Number(item.objType ?? -1);
    if (!Number.isFinite(objType) || objType < 0) {
      continue;
    }

    const bucket = byType.get(objType) || {
      objType,
      count: 0,
      items: [],
    };
    bucket.count += 1;
    bucket.items.push(item);
    byType.set(objType, bucket);
  }

  return [...byType.values()].sort((a, b) => b.count - a.count);
}

async function loadFieldObjectMeshes(objects, loadToken) {
  if (!Array.isArray(objects) || objects.length <= 0) {
    renderer.clearFieldObjectMeshes();
    setFieldObjectMeshMeta('Meshes estáticos: sem objetos para instanciar.');
    return;
  }

  const histogram = buildHistogramByObjectType(objects);
  const candidateTypes = histogram.slice(0, Math.min(histogram.length, 160)).map((entry) => entry.objType);
  await ensureObjectMeshMapForTypes(candidateTypes);
  if (loadToken !== state.fieldLoadToken) {
    return;
  }

  const meshTypeBudget = getObjectMeshTypeBudget();
  const instanceBudget = getObjectMeshInstanceBudget();
  const eligible = [];
  for (const bucket of histogram) {
    const mapping = state.field.objectMeshMapByType.get(bucket.objType);
    if (!mapping || !mapping.exists) {
      continue;
    }

    const sourcePath = normalizeMeshPath(mapping.resolvedPath || mapping.sourcePath);
    if (!/\.msa$/i.test(sourcePath)) {
      continue;
    }

    eligible.push({
      ...bucket,
      mapping,
      sourcePath,
    });

    if (eligible.length >= meshTypeBudget * 2) {
      break;
    }
  }

  if (eligible.length <= 0) {
    renderer.clearFieldObjectMeshes();
    setFieldObjectMeshMeta('Meshes estáticos: nenhum objType com `.msa` elegível neste field.');
    return;
  }

  const selected = eligible.slice(0, Math.min(meshTypeBudget, eligible.length));
  const typePlans = [];
  let remainingInstances = instanceBudget;

  for (let i = 0; i < selected.length; i += 1) {
    if (remainingInstances <= 0) {
      break;
    }

    const selectedType = selected[i];
    const remainingTypes = selected.length - i;
    const perTypeCap = Math.max(6, Math.floor(remainingInstances / Math.max(1, remainingTypes)));
    const pickedItems = selectedType.items.slice(0, Math.min(selectedType.items.length, perTypeCap));
    if (pickedItems.length <= 0) {
      continue;
    }

    typePlans.push({
      selectedType,
      pickedItems,
    });
    remainingInstances -= pickedItems.length;
  }

  const groupResults = new Array(typePlans.length).fill(null);
  const workerCount = Math.max(1, Math.min(4, typePlans.length));
  let cursor = 0;

  const workers = Array.from({ length: workerCount }, async () => {
    while (cursor < typePlans.length) {
      const index = cursor;
      cursor += 1;
      const plan = typePlans[index];
      try {
        const parsed = await loadObjectMsaParsed(plan.selectedType.sourcePath);
        if (loadToken !== state.fieldLoadToken) {
          return;
        }

        const materialPlans = buildMsaMaterialPlans(parsed.msa, plan.selectedType.mapping, parsed.meshPath);
        const materials = await Promise.all(materialPlans.map(async (materialPlan) => {
          const textureResult = await resolveFirstObjectTexture(materialPlan.textureCandidates || []);
          return {
            faceStart: materialPlan.faceStart,
            faceCount: materialPlan.faceCount,
            texturePath: textureResult.texturePath || '',
            texturePayload: textureResult.texturePayload,
          };
        }));
        if (loadToken !== state.fieldLoadToken) {
          return;
        }

        groupResults[index] = {
          objType: plan.selectedType.objType,
          meshPath: parsed.meshPath,
          geometry: parsed.msa,
          materials,
          instances: plan.pickedItems.map(mapFieldObjectToInstance),
        };
      } catch (error) {
        appendLog(`[Field Obj Mesh] falha objType=${plan.selectedType.objType} (${plan.selectedType.sourcePath}): ${error.message}`);
      }
    }
  });

  await Promise.all(workers);
  if (loadToken !== state.fieldLoadToken) {
    return;
  }

  const filteredGroups = groupResults.filter(Boolean);
  if (filteredGroups.length <= 0) {
    renderer.clearFieldObjectMeshes();
    setFieldObjectMeshMeta('Meshes estáticos: falha ao montar grupos renderizáveis.');
    return;
  }

  renderer.setFieldObjectMeshes(filteredGroups, {
    maxInstances: instanceBudget,
    scale: Number(terrainScaleEl.value || state.startup.terrainScale),
  });
  renderer.setFieldObjectMeshesVisible(objectMeshesEnabled());
  updateObjectTypeRadiusHints(filteredGroups);
  rebuildFieldCollisionProxies(state.field.summary?.objects?.items || []);

  const info = renderer.getFieldObjectMeshInfo();
  setFieldObjectMeshMeta(
    `Meshes estáticos: grupos ${info.groupCount} | materiais ${info.materialCount} | instâncias ${info.instanceCount} | tri ${info.triangleCount} | colisão ${state.field.collisionProxies.length} proxies | budget ${instanceBudget}`,
  );
  appendLog(`[Field Obj Mesh] grupos=${info.groupCount} materiais=${info.materialCount} instancias=${info.instanceCount} tri=${info.triangleCount} types=${filteredGroups.length}`);
}

async function loadFieldCatalog(defaultFallbackField = 'Field0815') {
  try {
    const response = await assetClient.fetchJson('/api/assets/fields');
    state.fieldCatalog = response.items || [];

    const query = new URLSearchParams(window.location.search);
    const fieldFromQuery = String(query.get('field') || '').trim();
    const candidates = [fieldFromQuery, defaultFallbackField, response.defaultField].filter(Boolean);

    let selected = response.defaultField || defaultFallbackField;
    for (const candidate of candidates) {
      const candidateLower = candidate.toLowerCase().replace(/\\/g, '/');
      const exists = state.fieldCatalog.some((item) => {
        const nameLower = (item.name || '').toLowerCase();
        return nameLower === candidateLower || candidateLower.endsWith(`${nameLower}.trn`) || candidateLower.endsWith(`${nameLower}.dat`);
      });
      if (exists) {
        const matched = state.fieldCatalog.find((item) => {
          const nameLower = (item.name || '').toLowerCase();
          return nameLower === candidateLower || candidateLower.endsWith(`${nameLower}.trn`) || candidateLower.endsWith(`${nameLower}.dat`);
        });
        selected = matched?.name || candidate;
        break;
      }
    }

    state.currentFieldName = selected;
    fillFieldSelector(state.fieldCatalog, selected);
    setFieldMeta(`Field catalogado: ${state.fieldCatalog.length} entradas.`);
  } catch (error) {
    state.fieldCatalog = [];
    clearSelect(fieldSelectEl, '(field indisponível)');
    setFieldMeta(`Field: catálogo indisponível (${error.message}).`);
    state.field.collisionProxies = [];
    renderer.clearTerrain();
    renderer.clearFieldObjectPoints();
    renderer.clearFieldObjectMeshes();
  }
}

async function loadFieldByName(fieldName, { objectLimit = 1024 } = {}) {
  const normalizedField = String(fieldName || '').trim();
  if (!normalizedField) {
    setFieldMeta('Field: selecione um entry válido.');
    return;
  }

  state.currentFieldName = normalizedField;
  const loadToken = state.fieldLoadToken + 1;
  state.fieldLoadToken = loadToken;
  if (state.field.objectMeshReloadTimer != null) {
    clearTimeout(state.field.objectMeshReloadTimer);
    state.field.objectMeshReloadTimer = null;
  }
  drawFieldPreview(null);

  const encodedField = encodeURIComponent(normalizedField);
  const response = await assetClient.fetchJson(`/api/assets/field-summary?field=${encodedField}&objectLimit=${objectLimit}`);
  if (loadToken !== state.fieldLoadToken) {
    return;
  }

  state.field.summary = response;
  state.field.previewImageData = null;
  clearFieldObjectSelection({ redraw: false });

  const tileMap = response.tileMap;
  const objects = response.objects;
  const mapData = response.map;
  await loadFieldMinimapTexture(normalizedField, response, loadToken);
  if (loadToken !== state.fieldLoadToken) {
    return;
  }
  drawFieldPreview(response);

  if (state.wasm.ready && state.wasm.bridge && response?.field?.trnPath) {
    try {
      const trnProbe = await assetClient.fetchBuffer(response.field.trnPath);
      if (loadToken !== state.fieldLoadToken) {
        return;
      }
      const trnInfo = state.wasm.bridge.parseTrnOverview(trnProbe.buffer);
      if (trnInfo) {
        appendLog(`[WASM TRN] ${normalizedField} tiles=${trnInfo.tileCount} h=[${trnInfo.heightMin}, ${trnInfo.heightMax}] avg=${trnInfo.heightAvg.toFixed(2)} offset=(${trnInfo.offsetX},${trnInfo.offsetY})`);
      }
    } catch (error) {
      appendLog(`[WASM TRN] erro: ${error.message}`);
    }
  }

  if (tileMap && Array.isArray(tileMap.heightGrid) && tileMap.heightGrid.length > 0) {
    renderer.setTerrainHeightGrid(tileMap.heightGrid, {
      heightUnit: 0.1,
      terrainScale: Number(terrainScaleEl.value || state.startup.terrainScale),
    });
    renderer.setTerrainVisible(terrainEnabled());
    renderer.setTerrainTextureVisible(terrainTextureEnabled());
    renderer.setTerrainTextureBlend(Number(terrainTextureBlendEl.value || state.startup.terrainTextureBlend));
    appendLog(`[Field Terrain] ${normalizedField} grid=${tileMap.heightGrid.length}x${tileMap.heightGrid[0]?.length || 0} scale=${terrainScaleEl.value}`);
  } else {
    renderer.clearTerrain();
    renderer.clearTerrainTexture();
    setFieldMinimapMeta(`Minimap ${normalizedField}: terreno indisponível.`);
  }

  if (objects && Array.isArray(objects.items) && objects.items.length > 0) {
    rebuildFieldCollisionProxies(objects.items);
    renderer.setFieldObjectPoints(objects.items, {
      pointSize: Number(objectPointSizeEl.value || state.startup.objectPointSize),
      limit: 2600,
    });
    renderer.setFieldObjectPointsVisible(objectPointsEnabled());
    renderer.setSelectedFieldObjectIndex(-1);
    appendLog(`[Field Objects] ${normalizedField} points=${objects.items.length}/${objects.totalParsedCount} size=${objectPointSizeEl.value}`);

    await loadFieldObjectMeshes(objects.items, loadToken);
    if (loadToken !== state.fieldLoadToken) {
      return;
    }
  } else {
    state.field.collisionProxies = [];
    renderer.clearFieldObjectPoints();
    renderer.clearFieldObjectMeshes();
    setFieldObjectMeshMeta('Meshes estáticos: sem objetos neste field.');
    clearFieldObjectSelection({ redraw: false });
  }

  if (Array.isArray(response.warnings) && response.warnings.length > 0) {
    for (const warning of response.warnings) {
      appendLog(`[Field Warning] ${warning}`);
    }
  }

  if (tileMap) {
    const avg = Number(tileMap.heightAvg || 0).toFixed(2);
    setFieldMeta(`Field ${normalizedField} | TRN ${tileMap.tileCount} tiles | h=[${tileMap.heightMin}, ${tileMap.heightMax}] avg=${avg}`);
  } else {
    setFieldMeta(`Field ${normalizedField} | sem TRN parseável.`);
  }

  if (objects) {
    const top = (objects.typeHistogramTop || [])
      .slice(0, 4)
      .map((item) => `${item.objType}(${item.count})`)
      .join(', ');
    setFieldObjectMeta(`Objetos DAT: ${objects.returnedCount}/${objects.totalParsedCount} | top types: ${top || 'n/d'}`);
  } else {
    setFieldObjectMeta('Objetos DAT: indisponível para este field.');
  }

  if (mapData && mapData.tileNameCount > 0) {
    appendLog(`[Field MAP] ${normalizedField} tileNames=${mapData.tileNameCount} (amostra: ${(mapData.tileNames || []).slice(0, 8).join(', ')})`);
  }

  updateUrlFieldQuery(normalizedField);
  if (state.player.enabled) {
    state.player.active = false;
    state.player.currentMotionIndex = -1;
    clearPlayerDestination();
    ensurePlayerLoop();
    refreshPlayerMeta();
  }
}

function scheduleFieldObjectMeshReload(delayMs = 260) {
  const objects = state.field.summary?.objects?.items;
  if (!Array.isArray(objects) || objects.length <= 0) {
    return;
  }

  if (state.field.objectMeshReloadTimer != null) {
    clearTimeout(state.field.objectMeshReloadTimer);
  }

  state.field.objectMeshReloadTimer = setTimeout(() => {
    state.field.objectMeshReloadTimer = null;
    const activeToken = state.fieldLoadToken;
    void loadFieldObjectMeshes(objects, activeToken);
  }, Math.max(50, delayMs));
}

function selectNumericOption(selectEl, value) {
  if (!Number.isFinite(value)) {
    return false;
  }

  const target = String(Number(value));
  for (const option of selectEl.options) {
    if (option.value === target) {
      selectEl.value = target;
      return true;
    }
  }
  return false;
}

async function applyStartupAnimationOptions(loadToken) {
  const startup = state.startup;
  if (startup.sequenceInitialized) {
    return;
  }

  let profile = state.skinning.profile;
  if (!profile || !profile.records || profile.records.length === 0) {
    startup.sequenceInitialized = true;
    return;
  }

  if (Number.isFinite(startup.entryIndex) && startup.entryIndex !== profile.entry.index) {
    const nextProfile = await fetchCatalogEntryProfile(startup.entryIndex);
    if (loadToken !== state.meshLoadToken) {
      return;
    }
    if (nextProfile && nextProfile.records.length > 0) {
      nextProfile.recommended = pickRecommendedProfileRecord(nextProfile.records);
      setAnimationProfile(nextProfile);
      profile = state.skinning.profile;
    }
  }

  let weaponChanged = false;
  if (selectNumericOption(aniProfileWeaponEl, startup.weaponType)) {
    aniProfileWeaponEl.dispatchEvent(new Event('change'));
    weaponChanged = true;
  }

  if (selectNumericOption(aniProfileMotionEl, startup.motionIndex)) {
    aniProfileMotionEl.dispatchEvent(new Event('change'));
  } else if (weaponChanged && Number.isFinite(startup.motionIndex)) {
    const candidate = findProfileRecord(
      profile.records,
      Number(aniProfileWeaponEl.value),
      Number(startup.motionIndex),
    );
    if (candidate) {
      selectNumericOption(aniProfileMotionEl, candidate.motionIndex);
    }
  }

  const applied = await applyProfileAniSelection({ auto: true });
  if (loadToken !== state.meshLoadToken) {
    return;
  }

  sequenceScopeEl.value = startup.sequenceScope === 'all' ? 'all' : 'weapon';
  state.skinning.sequence.scope = sequenceScopeEl.value;
  sequenceIntervalEl.value = startup.sequenceIntervalSec.toFixed(1);
  updateSequenceIntervalMs();

  if (startup.preloadAuto || startup.sequenceAuto) {
    await preloadProfileAniBank({
      scope: sequenceScopeEl.value === 'all' ? 'all' : 'weapon',
      silent: true,
    });
  }

  if (startup.sequenceAuto && applied) {
    await toggleProfileSequence();
  }

  startup.sequenceInitialized = true;
}

async function setupSkinningForMesh(meshPath, mesh, loadToken) {
  stopSkinningPlayback();
  resetAnimationProfileControls();

  state.skinning.mesh = mesh;
  state.skinning.profile = null;

  const related = await loadMeshRelatedAssets(meshPath);
  if (loadToken !== state.meshLoadToken) {
    return;
  }

  const aniCandidates = related.aniCandidates || [];
  const bonCandidates = related.bonCandidates || [];

  const profileHintAni = related.animationProfileHint?.recommendedAniPath || '';
  const profileHintBon = related.animationProfileHint?.bonPath || '';
  const preferredAni = profileHintAni || (aniCandidates.length > 0 ? aniCandidates[0] : '');
  const preferredBon = profileHintBon || (bonCandidates.length > 0 ? bonCandidates[0] : '');

  fillPathSelect(aniSelectEl, aniCandidates, preferredAni, '(sem ANI)');
  fillPathSelect(bonSelectEl, bonCandidates, preferredBon, '(sem BON)');

  if (!isMeshSkinnable(mesh)) {
    setAniMeta('Animação: mesh sem estrutura de skinning (palette/weights).');
    setAniProfileMeta('Perfil de animação disponível apenas para meshes com skinning.');
    aniFrameEl.disabled = true;
    return;
  }

  appendLog(`[Skinning Probe] mesh skin-friendly: influence=${mesh.header.faceInfluence} palette=${mesh.header.paletteCount}`);
  appendLog(`  related ANI=${aniCandidates.length} BON=${bonCandidates.length}`);

  const profile = await loadMeshAnimationProfile(meshPath);
  if (loadToken !== state.meshLoadToken) {
    return;
  }

  if (profile && profile.records.length > 0) {
    setAnimationProfile(profile);

    const applied = await applyProfileAniSelection({ auto: true });
    if (loadToken !== state.meshLoadToken) {
      return;
    }

    void preloadProfileAniBank({ scope: 'weapon', silent: true });

    if (!state.startup.sequenceInitialized) {
      await applyStartupAnimationOptions(loadToken);
      if (loadToken !== state.meshLoadToken) {
        return;
      }
    }

    if (applied || (state.skinning.context && state.skinning.ani)) {
      return;
    }
  } else {
    setAniProfileMeta('Perfil de animação não encontrado para esta mesh.');
  }

  if (!preferredAni) {
    setAniMeta('Animação: mesh com skinning, mas sem ANI candidato detectado.');
    return;
  }

  await applySkinningFromSelectedBundle({ source: 'related-fallback' });
}

async function runGlobalProbes() {
  const manifestSummary = await assetClient.getManifestSummary();
  metricFilesEl.textContent = `${manifestSummary.totals.files}`;
  metricBytesEl.textContent = formatBytes(manifestSummary.totals.bytes);

  const bootstrap = await assetClient.getBootstrapManifest();
  metricBootstrapEl.textContent = `${bootstrap.files.length} / ${bootstrap.files.length + bootstrap.missing.length}`;

  appendLog(`# Manifest gerado em: ${manifestSummary.generatedAt}`);
  appendLog(`# Top-level principal: Mesh=${manifestSummary.counts.byTopLevel.Mesh}, UI=${manifestSummary.counts.byTopLevel.UI}, Env=${manifestSummary.counts.byTopLevel.Env}`);

  const shaderProbe = await assetClient.fetchBuffer('Shader\\SKINMESH1.bin');
  const shaderBytes = new Uint8Array(shaderProbe.buffer);
  const shaderSig = new DataView(shaderProbe.buffer).getUint32(0, true);

  appendLog('');
  appendLog(`[Shader Probe] ${shaderProbe.resolved.requestedPath} -> ${shaderProbe.resolved.resolvedPath}`);
  appendLog(`  size=${shaderProbe.resolved.size} bytes, signature=0x${shaderSig.toString(16)}, profile=${shaderVersionString(shaderSig)}`);
  appendLog(`  first16=${bytesToHex(shaderBytes, 16)}`);

  const inventoryWysProbe = await assetClient.fetchBuffer('ui\\inventory2.wys');
  const inventoryWysBytes = new Uint8Array(inventoryWysProbe.buffer);
  const inventoryWys = decodeWysBuffer(inventoryWysProbe.buffer);

  appendLog('');
  appendLog(`[Texture Probe] ${inventoryWysProbe.resolved.requestedPath} -> ${inventoryWysProbe.resolved.resolvedPath}`);
  appendLog(`  header=${bytesToAscii(inventoryWysBytes, 4)} size=${inventoryWysProbe.resolved.size} bytes`);
  appendLog(`  first16=${bytesToHex(inventoryWysBytes, 16)}`);
  appendLog(`  decode=${inventoryWys.format} ${inventoryWys.width}x${inventoryWys.height} mips=${inventoryWys.mipMapCount}`);
}

async function loadMeshCatalog(defaultFallbackMesh) {
  const response = await assetClient.fetchJson('/api/assets/meshes');
  state.meshCatalog = response.items || [];

  const query = new URLSearchParams(window.location.search);
  const meshFromQuery = normalizeMeshPath(query.get('mesh') || '');

  const candidates = [meshFromQuery, defaultFallbackMesh, response.defaultMesh].filter(Boolean);
  let selected = response.defaultMesh || defaultFallbackMesh;

  for (const candidate of candidates) {
    const exists = state.meshCatalog.some((item) => item.path.toLowerCase() === candidate.toLowerCase());
    if (exists) {
      selected = candidate;
      break;
    }
  }

  state.currentMeshPath = selected;
  fillMeshSelector(state.meshCatalog, selected);
  setMeshMeta(`${state.meshCatalog.length} meshes catalogados`);
}

async function loadMeshByPath(meshPath) {
  const normalizedMeshPath = normalizeMeshPath(meshPath);
  state.currentMeshPath = normalizedMeshPath;

  const loadToken = state.meshLoadToken + 1;
  state.meshLoadToken = loadToken;

  appendLog('');

  const meshProbe = await assetClient.fetchBuffer(normalizedMeshPath);
  if (loadToken !== state.meshLoadToken) {
    return;
  }

  const mesh = parseWydMshBuffer(meshProbe.buffer);
  renderer.setMeshGeometry(mesh);

  if (state.wasm.ready && state.wasm.bridge) {
    try {
      const wasmHeader = state.wasm.bridge.parseMshHeader(meshProbe.buffer);
      if (wasmHeader) {
        appendLog(`[WASM MSH] parent=${wasmHeader.parentId} mesh=${wasmHeader.meshId} fvf=0x${wasmHeader.fvf.toString(16)} stride=${wasmHeader.vertexSize} influence=${wasmHeader.faceInfluence} palette=${wasmHeader.paletteCount} v=${wasmHeader.vertexCount} i=${wasmHeader.indexCount}`);
      }
    } catch (error) {
      appendLog(`[WASM MSH] erro: ${error.message}`);
    }
  }

  appendLog(`[Mesh Probe] ${meshProbe.resolved.requestedPath} -> ${meshProbe.resolved.resolvedPath}`);
  appendLog(`  fvf=0x${mesh.header.fvf.toString(16)} stride=${mesh.header.vertexSize} faceInfluence=${mesh.header.faceInfluence} palette=${mesh.header.paletteCount}`);
  appendLog(`  vertexCount=${mesh.header.vertexCount} indexCount=${mesh.header.indexCount}`);
  appendLog(`  bounds.min=(${mesh.bounds.min.map((v) => v.toFixed(2)).join(', ')})`);
  appendLog(`  bounds.max=(${mesh.bounds.max.map((v) => v.toFixed(2)).join(', ')})`);

  const likelySkinned = isMeshSkinnable(mesh) || mesh.header.faceInfluence > 1 || mesh.header.paletteCount > 1;
  if (likelySkinned) {
    appendLog('  aviso=mesh com estrutura de skinning detectada; iniciando bundle ANI/BON...');
  }

  await loadMeshTexture(normalizedMeshPath);
  if (loadToken !== state.meshLoadToken) {
    return;
  }

  await setupSkinningForMesh(normalizedMeshPath, mesh, loadToken);
  if (loadToken !== state.meshLoadToken) {
    return;
  }

  const skinText = likelySkinned ? ' | skinning M4 inicial ativo quando ANI disponível' : '';
  setMeshMeta(`Mesh ativo: ${normalizedMeshPath} | ${mesh.header.vertexCount} verts | ${mesh.header.indexCount} idx${skinText}`);
  updateUrlMeshQuery(normalizedMeshPath);

  appendLog('');
  appendLog('[WebGL Probe] mesh viewer ativo (câmera follow/orbita disponível no runtime de personagem).');
}

async function runAll(meshPathHint, fieldNameHint = '') {
  clearLog();
  await runGlobalProbes();

  if (!state.meshCatalog.length) {
    await loadMeshCatalog(meshPathHint || state.currentMeshPath || 'Mesh\\CP010102.msh');
  }

  if (!state.fieldCatalog.length) {
    await loadFieldCatalog(fieldNameHint || state.currentFieldName || 'Field0815');
  }

  let targetMesh = meshPathHint ? normalizeMeshPath(meshPathHint) : state.currentMeshPath;
  if (!targetMesh) {
    targetMesh = 'Mesh\\CP010102.msh';
  }

  const targetField = (fieldNameHint || state.currentFieldName || '').trim();
  if (targetField) {
    try {
      await loadFieldByName(targetField, { objectLimit: 1400 });
    } catch (error) {
      appendLog(`[Field] falha ao carregar ${targetField}: ${error.message}`);
      setFieldMeta(`Field ${targetField}: falha no carregamento (${error.message}).`);
    }
  }

  await loadMeshByPath(targetMesh);
}

async function bootstrap() {
  try {
    setStatus('Carregando WASM + manifests + catálogo de mesh/field + perfil de animação...');
    await initWasmBridge();
    setStatus('Carregando manifests, catálogo de mesh/field, perfil de animação, ANI/BON e viewer...');
    await runAll();

    if (!state.startedRenderLoop) {
      renderer.start();
      state.startedRenderLoop = true;
    }

    if (state.renderInfoTimer == null) {
      state.renderInfoTimer = setInterval(() => {
        const info = renderer.getViewportInfo();
        setRenderMeta(`Render: css ${info.cssWidth}x${info.cssHeight} | buffer ${info.bufferWidth}x${info.bufferHeight} | DPR ${info.dpr.toFixed(2)}`);
      }, 1000);
    }

    ensureSkinningLoop();
    setStatus('M4 avançado: mesh/skinning + terreno TRN + picking DAT + instancing .msa + runtime de personagem (minimap click-move, colisão, câmera follow).');
  } catch (error) {
    console.error(error);
    setStatus(`Falha na inicialização: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
  }
}

loadMeshButton.addEventListener('click', async () => {
  try {
    setStatus('Carregando mesh selecionado...');
    await loadMeshByPath(meshSelectEl.value);
    setStatus('Mesh carregado com sucesso.');
  } catch (error) {
    setStatus(`Falha ao carregar mesh: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
  }
});

loadFieldButton.addEventListener('click', async () => {
  try {
    setStatus('Carregando field selecionado...');
    await loadFieldByName(fieldSelectEl.value, { objectLimit: 1400 });
    setStatus('Field carregado com sucesso.');
  } catch (error) {
    setStatus(`Falha ao carregar field: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
  }
});

fieldSelectEl.addEventListener('change', () => {
  state.currentFieldName = String(fieldSelectEl.value || '').trim();
});

toggleTerrainEl.addEventListener('change', () => {
  renderer.setTerrainVisible(terrainEnabled());
});

terrainScaleEl.addEventListener('input', () => {
  const scale = Number(terrainScaleEl.value || 0.08);
  renderer.setTerrainScale(scale);
  renderer.setMeshSceneScale(scale);
});

toggleTerrainTextureEl.addEventListener('change', () => {
  renderer.setTerrainTextureVisible(terrainTextureEnabled());
});

terrainTextureBlendEl.addEventListener('input', () => {
  const blend = Number(terrainTextureBlendEl.value || state.startup.terrainTextureBlend);
  renderer.setTerrainTextureBlend(blend);
});

toggleObjectPointsEl.addEventListener('change', () => {
  renderer.setFieldObjectPointsVisible(objectPointsEnabled());
});

objectPointSizeEl.addEventListener('input', () => {
  const size = Number(objectPointSizeEl.value || 2.5);
  renderer.setFieldObjectPointSize(size);
});

toggleObjectMeshesEl.addEventListener('change', () => {
  renderer.setFieldObjectMeshesVisible(objectMeshesEnabled());
  refreshFieldObjectMeshMeta();
});

objectMeshBudgetEl.addEventListener('input', () => {
  scheduleFieldObjectMeshReload(220);
});

objectMeshTypesEl.addEventListener('input', () => {
  scheduleFieldObjectMeshReload(220);
});

togglePlayerModeEl.addEventListener('change', () => {
  setPlayerEnabled(String(togglePlayerModeEl.value || 'off') !== 'off');
});

playerWalkSpeedEl.addEventListener('input', () => {
  const value = Number(playerWalkSpeedEl.value || state.player.walkSpeed);
  state.player.walkSpeed = clamp(Number.isFinite(value) ? value : 3.8, 1.5, 8.0);
  if (state.player.runSpeed <= state.player.walkSpeed) {
    state.player.runSpeed = Math.min(12.0, state.player.walkSpeed + 0.2);
    playerRunSpeedEl.value = state.player.runSpeed.toFixed(1);
  }
  refreshPlayerMeta();
});

playerRunSpeedEl.addEventListener('input', () => {
  const value = Number(playerRunSpeedEl.value || state.player.runSpeed);
  state.player.runSpeed = clamp(Number.isFinite(value) ? value : 7.2, 2.0, 12.0);
  if (state.player.runSpeed <= state.player.walkSpeed) {
    state.player.runSpeed = Math.min(12.0, state.player.walkSpeed + 0.2);
    playerRunSpeedEl.value = state.player.runSpeed.toFixed(1);
  }
  refreshPlayerMeta();
});

if (togglePlayerClickMoveEl) {
  togglePlayerClickMoveEl.addEventListener('change', () => {
    state.player.clickMoveEnabled = String(togglePlayerClickMoveEl.value || 'on') !== 'off';
    refreshPlayerMeta();
  });
}

if (togglePlayerCollisionEl) {
  togglePlayerCollisionEl.addEventListener('change', () => {
    state.player.collisionEnabled = String(togglePlayerCollisionEl.value || 'on') !== 'off';
    refreshPlayerMeta();
  });
}

if (togglePlayerAutoRunEl) {
  togglePlayerAutoRunEl.addEventListener('change', () => {
    state.player.autoRunEnabled = String(togglePlayerAutoRunEl.value || 'on') !== 'off';
    refreshPlayerMeta();
  });
}

if (playerCameraModeEl) {
  playerCameraModeEl.addEventListener('change', () => {
    state.player.cameraMode = String(playerCameraModeEl.value || 'follow') === 'orbit' ? 'orbit' : 'follow';
    renderer.setCameraMode(state.player.enabled ? state.player.cameraMode : 'orbit');
    refreshPlayerMeta();
  });
}

if (playerCameraDistanceEl) {
  playerCameraDistanceEl.addEventListener('input', () => {
    const value = Number(playerCameraDistanceEl.value || state.player.cameraDistance);
    state.player.cameraDistance = clamp(Number.isFinite(value) ? value : 7.2, 3.0, 15.0);
    renderer.setCameraFollowOptions({
      distance: state.player.cameraDistance,
      height: state.player.cameraHeight,
    });
    refreshPlayerMeta();
  });
}

if (playerCameraHeightEl) {
  playerCameraHeightEl.addEventListener('input', () => {
    const value = Number(playerCameraHeightEl.value || state.player.cameraHeight);
    state.player.cameraHeight = clamp(Number.isFinite(value) ? value : 2.8, 1.2, 8.0);
    renderer.setCameraFollowOptions({
      distance: state.player.cameraDistance,
      height: state.player.cameraHeight,
    });
    refreshPlayerMeta();
  });
}

clearObjectSelectionButton.addEventListener('click', () => {
  clearFieldObjectSelection();
});

if (spawnPlayerAtSelectedButton) {
  spawnPlayerAtSelectedButton.addEventListener('click', () => {
    spawnPlayerAtSelectedObject();
  });
}

fieldPreviewCanvas.addEventListener('click', (event) => {
  const summary = state.field.summary;
  if (!summary || !summary.tileMap || !Array.isArray(summary.tileMap.heightGrid) || summary.tileMap.heightGrid.length <= 0) {
    return;
  }

  const rect = fieldPreviewCanvas.getBoundingClientRect();
  if (rect.width <= 0 || rect.height <= 0) {
    return;
  }

  const localX = event.clientX - rect.left;
  const localY = event.clientY - rect.top;
  const scene = previewToScenePosition(localX, localY, rect.width, rect.height);

  if (!state.player.enabled) {
    setPlayerEnabled(true);
  }

  if (event.shiftKey) {
    spawnPlayerAt(scene.worldX, scene.worldZ, { clearDestination: true });
    setStatus(`Teleport: (${scene.worldX.toFixed(2)}, ${scene.worldZ.toFixed(2)})`);
  } else if (state.player.clickMoveEnabled) {
    setPlayerDestination(scene.worldX, scene.worldZ, 'minimap');
    setStatus(`Destino do personagem atualizado: (${scene.worldX.toFixed(2)}, ${scene.worldZ.toFixed(2)}).`);
  }

  drawFieldPreview(summary);
  refreshPlayerMeta();
});

renderer.canvas.addEventListener('click', (event) => {
  if (!state.field.summary || !state.field.summary.objects || !Array.isArray(state.field.summary.objects.items)) {
    return;
  }

  const rect = renderer.canvas.getBoundingClientRect();
  if (rect.width <= 0 || rect.height <= 0) {
    return;
  }

  const x = ((event.clientX - rect.left) / rect.width) * renderer.canvas.width;
  const y = ((event.clientY - rect.top) / rect.height) * renderer.canvas.height;
  const pick = renderer.pickFieldObjectAtCanvas(x, y, { pixelRadius: 15 });

  if (!pick) {
    return;
  }

  applyFieldObjectSelection(pick);
  setStatus(`Objeto de field selecionado: tipo ${pick.item?.objType ?? 'n/d'} (#${pick.index}).`);

  if (event.shiftKey && state.player.enabled) {
    const worldX = fieldCoordToScene(Number(pick.item?.x || 0));
    const worldZ = fieldCoordToScene(Number(pick.item?.y || 0));
    setPlayerDestination(worldX, worldZ, 'object-pick');
    setStatus(`Destino por objeto: type ${pick.item?.objType ?? 'n/d'} (#${pick.index}).`);
  }
});

loadSkinningButton.addEventListener('click', async () => {
  try {
    setStatus('Aplicando bundle ANI/BON...');
    const ok = await applySkinningFromSelectedBundle({ source: 'manual-bundle' });
    if (ok) {
      setStatus('Bundle ANI/BON aplicado.');
    } else {
      setStatus('Bundle ANI/BON não aplicado (ver logs).', true);
    }
  } catch (error) {
    setStatus(`Falha no bundle ANI/BON: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
  }
});

applyProfileAniButton.addEventListener('click', async () => {
  try {
    setStatus('Aplicando ANI por perfil...');
    const ok = await applyProfileAniSelection({ auto: false });
    if (ok) {
      setStatus('ANI por perfil aplicado com sucesso.');
    } else {
      setStatus('ANI por perfil não aplicado (ver logs).', true);
    }
  } catch (error) {
    setStatus(`Falha ao aplicar perfil ANI: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
  }
});

preloadProfileButton.addEventListener('click', async () => {
  try {
    const scope = sequenceScopeEl.value === 'all' ? 'all' : 'weapon';
    const result = await preloadProfileAniBank({ scope, silent: false });
    if (result.ok) {
      setStatus(`Preload concluído (${result.loaded} novos, ${result.failed} falhas).`, result.failed > 0);
    } else {
      setStatus('Preload não executado (sem clips no escopo).', true);
    }
  } catch (error) {
    setStatus(`Falha no preload do perfil: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
  }
});

toggleProfileSeqButton.addEventListener('click', async () => {
  try {
    await toggleProfileSequence();
    if (state.skinning.sequence.active) {
      setStatus('Sequência de motions iniciada.');
    } else {
      setStatus('Sequência de motions parada.');
    }
  } catch (error) {
    setStatus(`Falha na sequência de motions: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
    stopProfileSequence();
  }
});

sequenceScopeEl.addEventListener('change', () => {
  const scope = sequenceScopeEl.value === 'all' ? 'all' : 'weapon';
  state.skinning.sequence.scope = scope;
  state.skinning.sequence.position = 0;

  const profile = state.skinning.profile;
  if (profile) {
    updateSequenceSummary(scope, getRecordsForSequenceScope(profile, scope, aniProfileWeaponEl.value));
  }
});

sequenceIntervalEl.addEventListener('input', () => {
  updateSequenceIntervalMs();
  const sequence = state.skinning.sequence;
  const profile = state.skinning.profile;

  if (profile) {
    updateSequenceSummary(sequence.scope, getRecordsForSequenceScope(profile, sequence.scope, aniProfileWeaponEl.value));
  }

  if (!sequence.active) {
    return;
  }

  if (sequence.timer != null) {
    clearInterval(sequence.timer);
  }
  sequence.timer = setInterval(() => {
    void runProfileSequenceStep();
  }, sequence.intervalMs);
});

aniProfileEntryEl.addEventListener('change', async () => {
  const selectedIndex = Number(aniProfileEntryEl.value);
  if (!Number.isFinite(selectedIndex)) {
    return;
  }

  try {
    const nextProfile = await fetchCatalogEntryProfile(selectedIndex);
    if (!nextProfile) {
      return;
    }

    nextProfile.recommended = pickRecommendedProfileRecord(nextProfile.records);
    setAnimationProfile(nextProfile);

    const autoApplied = await applyProfileAniSelection({ auto: true });
    void preloadProfileAniBank({ scope: 'weapon', silent: true });
    if (autoApplied) {
      setStatus('Perfil BoneAni alternativo aplicado.');
    }
  } catch (error) {
    appendLog(`[Animation Profile] troca de entry falhou: ${error.message}`);
  }
});

aniProfileWeaponEl.addEventListener('change', () => {
  const profile = state.skinning.profile;
  if (!profile || !profile.records || profile.records.length === 0) {
    return;
  }

  const weaponType = Number(aniProfileWeaponEl.value);
  const motions = collectMotionsForWeapon(profile.records, weaponType);
  fillNumberSelect(aniProfileMotionEl, motions, motions[0] ?? 0, motionLabel, '(sem motion)');

  const candidate = findProfileRecord(profile.records, weaponType, Number(aniProfileMotionEl.value));
  if (candidate) {
    setAniProfileMeta(`Selecionado: weapon ${candidate.weaponType} | ${motionLabel(candidate.motionIndex)} | ${candidate.aniPath}`);
  }

  if (state.skinning.sequence.scope === 'weapon') {
    state.skinning.sequence.position = 0;
  }
  updateSequenceSummary(state.skinning.sequence.scope, getRecordsForSequenceScope(profile, state.skinning.sequence.scope, aniProfileWeaponEl.value));
});

aniProfileMotionEl.addEventListener('change', () => {
  const profile = state.skinning.profile;
  if (!profile || !profile.records || profile.records.length === 0) {
    return;
  }

  const candidate = findProfileRecord(
    profile.records,
    Number(aniProfileWeaponEl.value),
    Number(aniProfileMotionEl.value),
  );

  if (candidate) {
    setAniProfileMeta(`Selecionado: weapon ${candidate.weaponType} | ${motionLabel(candidate.motionIndex)} | ${candidate.aniPath}`);
  }
});

toggleAniButton.addEventListener('click', () => {
  const skin = state.skinning;
  if (!skin.context || !skin.ani) {
    setAniMeta('Animação: nenhum bundle ANI ativo.');
    return;
  }

  skin.playing = !skin.playing;
  skin.lastUpdateMs = 0;
  toggleAniButton.textContent = skin.playing ? 'Pausar ANI' : 'Retomar ANI';

  const currentTick = Number(aniFrameEl.value || 0);
  setAniMeta(`ANI ativo: ${skin.aniPath} | BON: ${skin.bonPath || 'n/d'} | frame ${currentTick + 1}/${Math.max(1, skin.ani.tickCount)} | speed ${skin.speed.toFixed(2)}x | ${skin.playing ? 'playing' : 'pausado'}`);
});

aniFrameEl.addEventListener('input', () => {
  const skin = state.skinning;
  if (!skin.context || !skin.ani) {
    return;
  }

  const tick = Number(aniFrameEl.value || 0);
  skin.frameFloat = tick;
  skin.lastUpdateMs = 0;

  const result = applySkinningAtTick(skin.context, tick);
  renderer.updateMeshDeformation(result.positions, result.normals);

  setAniMeta(`ANI ativo: ${skin.aniPath} | BON: ${skin.bonPath || 'n/d'} | frame ${tick + 1}/${Math.max(1, skin.ani.tickCount)} | speed ${skin.speed.toFixed(2)}x | ${skin.playing ? 'playing' : 'pausado'}`);
});

aniSpeedEl.addEventListener('input', () => {
  const value = Number(aniSpeedEl.value || 1);
  state.skinning.speed = Number.isFinite(value) ? value : 1;

  const skin = state.skinning;
  if (!skin.context || !skin.ani) {
    return;
  }

  const currentTick = Number(aniFrameEl.value || 0);
  setAniMeta(`ANI ativo: ${skin.aniPath} | BON: ${skin.bonPath || 'n/d'} | frame ${currentTick + 1}/${Math.max(1, skin.ani.tickCount)} | speed ${skin.speed.toFixed(2)}x | ${skin.playing ? 'playing' : 'pausado'}`);
});

reloadButton.addEventListener('click', async () => {
  try {
    setStatus('Reexecutando probes...');
    await runAll(
      meshSelectEl.value || state.currentMeshPath,
      fieldSelectEl.value || state.currentFieldName,
    );
    setStatus('Probes executados com sucesso.');
  } catch (error) {
    setStatus(`Falha ao recarregar probes: ${error.message}`, true);
    appendLog(`[ERRO] ${error.message}`);
  }
});

function updatePlayerKeyStateByEvent(event, pressed) {
  const key = String(event.key || '').toLowerCase();
  const player = state.player;

  if (key === 'w' || key === 'arrowup') {
    player.keyState.forward = pressed;
    event.preventDefault();
    return true;
  }
  if (key === 's' || key === 'arrowdown') {
    player.keyState.backward = pressed;
    event.preventDefault();
    return true;
  }
  if (key === 'a' || key === 'arrowleft') {
    player.keyState.left = pressed;
    event.preventDefault();
    return true;
  }
  if (key === 'd' || key === 'arrowright') {
    player.keyState.right = pressed;
    event.preventDefault();
    return true;
  }
  if (key === 'shift') {
    player.keyState.run = pressed;
    return true;
  }
  return false;
}

document.addEventListener('keydown', (event) => {
  const tagName = String(event.target?.tagName || '').toUpperCase();
  if (tagName === 'INPUT' || tagName === 'SELECT' || tagName === 'TEXTAREA') {
    return;
  }
  updatePlayerKeyStateByEvent(event, true);
});

document.addEventListener('keyup', (event) => {
  updatePlayerKeyStateByEvent(event, false);
});

document.addEventListener('visibilitychange', () => {
  const skin = state.skinning;
  if (document.hidden) {
    if (skin.context && skin.ani) {
      skin.lastUpdateMs = 0;
    }

    state.player.lastUpdateMs = 0;
    state.player.keyState.forward = false;
    state.player.keyState.backward = false;
    state.player.keyState.left = false;
    state.player.keyState.right = false;
    state.player.keyState.run = false;
    state.player.blockedLastFrame = false;
  }
});

resetAnimationProfileControls();
drawFieldPreview(null);
setFieldObjectMeta('Objetos de field: aguardando...');
setFieldObjectMeshMeta('Meshes estáticos: aguardando field...');
setFieldObjectSelectedMeta('Objeto selecionado: nenhum (clique no canvas 3D para inspecionar).');
setFieldMinimapMeta('Minimap: aguardando field...');
bootstrap();
