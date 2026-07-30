#include "windows.h"

#include "d3d9.h"
#include "d3dx9.h"
#include "CFrame.h"
#include "DirShow.h"
#include "MeshManager.h"
#include "NewApp.h"
#include "ObjectManager.h"
#include "ResourceControl.h"
#include "SControl.h"
#include "SControlContainer.h"
#include "TimerManager.h"
#include "TMGlobal.h"
#include "TMHuman.h"
#include "TMScene.h"
#include "TMSelectCharScene.h"
#include "TMSelectServerScene.h"
#include "TMSkinMesh.h"

#include <cstring>
#include <cstdio>

#if defined(__EMSCRIPTEN__)
#include <emscripten/console.h>
#endif

bool CheckOS();
char ReadNameFiltraDataBase();
char ReadChatFiltraDataBase();

extern char g_szOS[3];
extern ObjectManager* g_pObjectManager;
extern NewApp* g_pApp;
extern char g_pServerList[MAX_SERVERGROUP][MAX_SERVERNUMBER][64];
extern "C" void wyd_wasm_set_direct_state_request(int active);

// Real client entrypoint from Projects/TMProject/NewApp.cpp
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow);

namespace {

NewApp* g_wyd_app = nullptr;
MSG g_wyd_msg{};
bool g_wyd_msg_initialized = false;
bool g_wyd_boot_in_progress = false;
unsigned int g_wyd_compare_present_state_sequence = 0;
bool g_wyd_compare_present_game_state_valid = false;
int g_wyd_compare_present_game_state = 0;
bool g_wyd_compare_present_scene_type_valid = false;
int g_wyd_compare_present_scene_type = 0;

constexpr unsigned int kWydMkShift = 0x0004u;
constexpr unsigned int kWydMkControl = 0x0008u;

void WydBootLog(const char* msg) {
#if defined(__EMSCRIPTEN__)
  emscripten_console_log(msg ? msg : "(null)");
#else
  std::fprintf(stderr, "%s\n", msg ? msg : "(null)");
#endif
}

void WydResetComparePresentState() {
  g_wyd_compare_present_state_sequence = 0;
  g_wyd_compare_present_game_state_valid = false;
  g_wyd_compare_present_game_state = 0;
  g_wyd_compare_present_scene_type_valid = false;
  g_wyd_compare_present_scene_type = 0;
}

}  // namespace

extern "C" void wyd_dinput_mouse_event(unsigned int msg, unsigned int wParam, int x, int y, int wheel_delta);
extern "C" void wyd_dinput_key_event(unsigned int msg, unsigned int key);

extern "C" int wyd_mouse_event(unsigned int msg, unsigned int wParam, int x, int y, int wheel_delta) {
  wyd_dinput_mouse_event(msg, wParam, x, y, wheel_delta);

  if (!g_wyd_app || !g_pEventTranslator) return 0;

  g_pEventTranslator->m_bShift = (wParam & kWydMkShift) ? 1 : 0;
  g_pEventTranslator->m_bCtrl = (wParam & kWydMkControl) ? 1 : 0;

  // Keep the browser on the same source path as the native client: the
  // Win32 message updates cursor/control state where the original MsgProc
  // handles it, while button presses are observed once through DirectInput
  // during ReadInputEventData. Calling OnMouseEvent here as well duplicates
  // a click in scenes which consume both paths.
  g_wyd_app->MsgProc(
      g_wyd_app->m_hWnd,
      msg,
      static_cast<DWORD>(wParam),
      static_cast<int>(
          MAKELONG(static_cast<WORD>(x), static_cast<WORD>(y))));

  return 1;
}

extern "C" int wyd_key_event(unsigned int msg, unsigned int wParam, int lParam) {
  wyd_dinput_key_event(msg, wParam);
  if (!g_wyd_app) return 0;
  return static_cast<int>(
      g_wyd_app->MsgProc(g_wyd_app->m_hWnd, msg, static_cast<WPARAM>(wParam), lParam));
}

static bool WydControlEffectivelyInteractive(
    const SControlContainer* controls,
    const SControl* control) {
  if (!controls || !control) return false;

  const TreeNode* node = control;
  while (node) {
    const auto* current = static_cast<const SControl*>(node);
    if (!current->m_bVisible || !current->m_bEnable) return false;
    if (current == controls->m_pControlRoot) return true;
    node = node->m_pTop;
  }
  return false;
}

extern "C" int wyd_text_input_active() {
  if (!g_pObjectManager) return 0;

  TMScene* scene = g_pObjectManager->GetCurrentScene();
  SControlContainer* controls = scene ? scene->GetCtrlContainer() : nullptr;
  SControl* focus = controls ? controls->m_pFocusControl : nullptr;
  return focus &&
         focus->m_eCtrlType == CONTROL_TYPE::CTRL_TYPE_EDITABLETEXT &&
         focus->m_bFocused &&
         WydControlEffectivelyInteractive(controls, focus);
}

extern "C" const char* wyd_text_input_value() {
  static const char empty[] = "";
  if (!wyd_text_input_active()) return empty;

  TMScene* scene = g_pObjectManager->GetCurrentScene();
  SControlContainer* controls = scene ? scene->GetCtrlContainer() : nullptr;
  SControl* focus = controls ? controls->m_pFocusControl : nullptr;
  return focus ? static_cast<SEditableText*>(focus)->GetText() : empty;
}

extern "C" int wyd_boot_client(int fullscreen) {
  if (g_wyd_boot_in_progress) {
    WydBootLog("[wyd_boot] already in progress");
    return 0;
  }
  g_wyd_boot_in_progress = true;
  WydBootLog("[wyd_boot] begin");

  if (g_wyd_app) {
    g_wyd_boot_in_progress = false;
    return 1;
  }

  WydResetComparePresentState();

  WydBootLog("[wyd_boot] os-check");
  if (CheckOS()) {
    std::sprintf(g_szOS, "98");
  } else {
    std::sprintf(g_szOS, "NT");
  }

  WydBootLog("[wyd_boot] read-name-filter");
  if (!ReadNameFiltraDataBase()) {
    WydBootLog("[wyd_boot] read-name-filter failed");
    g_wyd_boot_in_progress = false;
    return 0;
  }
  WydBootLog("[wyd_boot] read-chat-filter");
  if (!ReadChatFiltraDataBase()) {
    WydBootLog("[wyd_boot] read-chat-filter failed");
    g_wyd_boot_in_progress = false;
    return 0;
  }

  WydBootLog("[wyd_boot] new NewApp");
  g_wyd_app = new NewApp();
  if (!g_wyd_app) {
    WydBootLog("[wyd_boot] new NewApp failed");
    g_wyd_boot_in_progress = false;
    return 0;
  }

  const int full = fullscreen ? 1 : 0;
  WydBootLog("[wyd_boot] Initialize");
  if (!g_wyd_app->Initialize(reinterpret_cast<HINSTANCE>(1), full)) {
    WydBootLog("[wyd_boot] Initialize failed");
    delete g_wyd_app;
    g_wyd_app = nullptr;
    g_wyd_boot_in_progress = false;
    return 0;
  }

  WydBootLog("[wyd_boot] initialize message");
  PeekMessage(&g_wyd_msg, 0, 0, 0, 0);
  g_wyd_msg_initialized = true;
  g_wyd_boot_in_progress = false;
  WydBootLog("[wyd_boot] success");
  return 1;
}

extern "C" int wyd_tick_client() {
  if (!g_wyd_app) return -1;
  if (!g_wyd_msg_initialized) {
    PeekMessage(&g_wyd_msg, 0, 0, 0, 0);
    g_wyd_msg_initialized = true;
  }

  return static_cast<int>(g_wyd_app->RunTick(&g_wyd_msg));
}

extern "C" int wyd_shutdown_client() {
  if (!g_wyd_app) return 0;

  g_wyd_app->Finalize();
  delete g_wyd_app;
  g_wyd_app = nullptr;
  g_wyd_msg_initialized = false;
  std::memset(&g_wyd_msg, 0, sizeof(g_wyd_msg));
  return 1;
}

extern "C" int wyd_get_game_state() {
  if (!g_pObjectManager) return static_cast<int>(ObjectManager::TM_GAME_STATE::TM_NONE_STATE);
  return static_cast<int>(g_pObjectManager->m_eCurrentState);
}

extern "C" void wyd_compare_latch_present_state() {
  g_wyd_compare_present_game_state_valid = g_pObjectManager != nullptr;
  if (g_pObjectManager) {
    g_wyd_compare_present_game_state =
        static_cast<int>(g_pObjectManager->m_eCurrentState);
  }

  g_wyd_compare_present_scene_type_valid = g_pCurrentScene != nullptr;
  if (g_pCurrentScene) {
    g_wyd_compare_present_scene_type =
        static_cast<int>(g_pCurrentScene->m_eSceneType);
  }

  g_wyd_compare_present_state_sequence += 1;
}

extern "C" unsigned int wyd_compare_present_state_sequence() {
  return g_wyd_compare_present_state_sequence;
}

extern "C" int wyd_compare_present_game_state_valid() {
  return g_wyd_compare_present_game_state_valid ? 1 : 0;
}

extern "C" int wyd_compare_present_game_state() {
  return g_wyd_compare_present_game_state;
}

extern "C" int wyd_compare_present_scene_type_valid() {
  return g_wyd_compare_present_scene_type_valid ? 1 : 0;
}

extern "C" int wyd_compare_present_scene_type() {
  return g_wyd_compare_present_scene_type;
}

extern "C" int wyd_cursor_visible() {
  return g_pCursor && g_pCursor->m_bVisible ? 1 : 0;
}

extern "C" int wyd_set_game_state(int state) {
  if (!g_pObjectManager) return 0;
  if (state < static_cast<int>(ObjectManager::TM_GAME_STATE::TM_NONE_STATE) ||
      state > static_cast<int>(ObjectManager::TM_GAME_STATE::TM_FIELD2_STATE)) {
    return 0;
  }
  if (static_cast<int>(g_pObjectManager->m_eCurrentState) != state) {
    // Direct harness navigation can bypass the original transition packet
    // that invalidates the cached BGM index. Let the destination scene choose
    // and start its official track again.
    DS_SOUND_MANAGER::m_nMusicIndex = -1;
  }
  wyd_wasm_set_direct_state_request(1);
  g_pObjectManager->SetCurrentState(static_cast<ObjectManager::TM_GAME_STATE>(state));
  wyd_wasm_set_direct_state_request(0);
  return 1;
}

extern "C" int wyd_selchar_initialized() {
  if (!g_pObjectManager) return 0;
  TMScene* scene = g_pObjectManager->GetCurrentScene();
  return scene && scene->GetSceneType() == ESCENE_TYPE::ESCENE_SELCHAR ? 1 : 0;
}

extern "C" int wyd_selchar_char_count() {
  if (!g_pObjectManager) return 0;
  int count = 0;
  for (int i = 0; i < 4; ++i) {
    if (g_pObjectManager->m_stSelCharData.MobName[i][0] != 0) ++count;
  }
  return count;
}

extern "C" int wyd_selchar_human_present(int slot) {
  if (!g_pObjectManager || slot < 0 || slot >= 4) return 0;
  TMScene* scene = g_pObjectManager->GetCurrentScene();
  if (!scene || scene->GetSceneType() != ESCENE_TYPE::ESCENE_SELCHAR) return 0;
  auto* sel_char = static_cast<TMSelectCharScene*>(scene);
  return sel_char->m_pHuman[slot] != nullptr ? 1 : 0;
}

extern "C" const char* wyd_selchar_name(int slot) {
  static char empty[1] = {0};
  if (!g_pObjectManager || slot < 0 || slot >= 4) return empty;
  return g_pObjectManager->m_stSelCharData.MobName[slot];
}

static TMHuman* WydSelectCharSampleHuman(int slot) {
  if (!g_pObjectManager || slot < 0 || slot >= 4) return nullptr;
  TMScene* scene = g_pObjectManager->GetCurrentScene();
  if (!scene || scene->GetSceneType() != ESCENE_TYPE::ESCENE_SELCHAR) return nullptr;
  return static_cast<TMSelectCharScene*>(scene)->m_pSampleHuman[slot];
}

static int WydCountFrameMeshes(CFrame* frame) {
  int count = 0;
  for (CFrame* node = frame; node; node = node->m_pSibling) {
    if (node->m_pMesh) ++count;
    if (node->m_pFirstChild) count += WydCountFrameMeshes(node->m_pFirstChild);
  }
  return count;
}

extern "C" int wyd_selchar_sample_present(int slot) {
  return WydSelectCharSampleHuman(slot) ? 1 : 0;
}

extern "C" int wyd_selchar_sample_skin_present(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human && human->m_pSkinMesh ? 1 : 0;
}

extern "C" int wyd_selchar_sample_visible(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human ? static_cast<int>(human->m_bVisible) : 0;
}

extern "C" float wyd_selchar_sample_x(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human ? human->m_vecPosition.x : 0.0f;
}

extern "C" float wyd_selchar_sample_y(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human ? human->m_vecPosition.y : 0.0f;
}

extern "C" float wyd_selchar_sample_height(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human ? human->m_fHeight : 0.0f;
}

extern "C" int wyd_selchar_sample_animation(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human && human->m_pSkinMesh ? human->m_pSkinMesh->m_nAniIndex : -1;
}

extern "C" int wyd_selchar_sample_mesh_type(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human ? human->m_nSkinMeshType : -1;
}

extern "C" int wyd_selchar_sample_mesh_generated(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human && human->m_pSkinMesh ? human->m_pSkinMesh->m_bMeshGenerated : 0;
}

extern "C" int wyd_selchar_sample_frame_meshes(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human && human->m_pSkinMesh ? WydCountFrameMeshes(human->m_pSkinMesh->m_pRoot) : 0;
}

extern "C" int wyd_selchar_sample_bone_animation(int slot) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  return human && human->m_pSkinMesh ? human->m_pSkinMesh->m_nBoneAniIndex : -1;
}

extern "C" int wyd_selchar_sample_look_mesh(int slot, int part) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  if (!human || part < 0 || part >= 8) return -1;
  const short* look = reinterpret_cast<const short*>(&human->m_stLookInfo);
  return static_cast<int>(look[part * 2]);
}

extern "C" int wyd_selchar_sample_look_skin(int slot, int part) {
  TMHuman* human = WydSelectCharSampleHuman(slot);
  if (!human || part < 0 || part >= 8) return -1;
  const short* look = reinterpret_cast<const short*>(&human->m_stLookInfo);
  return static_cast<int>(look[part * 2 + 1]);
}

extern "C" int wyd_skin_animation_num_parts(int bone_animation) {
  if (bone_animation < 0 || bone_animation >= MAX_BONE_ANIMATION_LIST) return -1;
  return static_cast<int>(MeshManager::m_BoneAnimationList[bone_animation].numParts);
}

extern "C" int wyd_skin_animation_num_bones(int bone_animation) {
  if (bone_animation < 0 || bone_animation >= MAX_BONE_ANIMATION_LIST) return -1;
  return static_cast<int>(MeshManager::m_BoneAnimationList[bone_animation].numBone);
}

extern "C" const char* wyd_serverlist_entry(int group, int index) {
  static char empty[1] = {0};
  if (group < 0 || group >= MAX_SERVERGROUP || index < 0 || index >= MAX_SERVERNUMBER) return empty;
  return g_pServerList[group][index];
}

extern "C" int wyd_debug_selectserver_login(const char* account, const char* password, const char* host) {
  if (!g_pObjectManager || !g_pApp || !account || !password) return 0;
  TMScene* scene = g_pObjectManager->GetCurrentScene();
  if (!scene || scene->GetSceneType() != ESCENE_TYPE::ESCENE_SELECT_SERVER) return 0;

  auto* select_server = static_cast<TMSelectServerScene*>(scene);
  if (!select_server->m_pEditID || !select_server->m_pEditPW || !select_server->m_pLoginPanel ||
      !select_server->m_pLoginBtns[0]) {
    return 0;
  }

  if (host && host[0]) {
    std::snprintf(g_pApp->m_szServerIP, sizeof(g_pApp->m_szServerIP), "%s", host);
  }
  if (!g_pApp->m_szServerIP[0]) return 0;

  char account_buf[16] = {0};
  char password_buf[16] = {0};
  std::snprintf(account_buf, sizeof(account_buf), "%s", account);
  std::snprintf(password_buf, sizeof(password_buf), "%s", password);

  select_server->m_pEditID->SetText(account_buf);
  select_server->m_pEditPW->SetText(password_buf);
  select_server->m_pLoginPanel->SetVisible(1);
  select_server->m_pLoginBtns[0]->SetVisible(1);
  select_server->m_pLoginBtns[0]->SetEnable(1);
  static_cast<SControl*>(select_server->m_pEditPW)->SetEnable(1);
  select_server->m_cLogin = 1;
  if (g_pTimerManager && g_pTimerManager->GetServerTime() < 2000) {
    g_pTimerManager->SetServerTime(2000);
  }
  const unsigned int live_time = g_pTimerManager ? g_pTimerManager->GetServerTime() : 2000;
  select_server->LastSendMsgTime = live_time > 1500 ? live_time - 1501 : 0;
  select_server->m_dwLastClickLoginBtnTime = 0;

  return select_server->OnControlEvent(B_LOGIN_OK, 0);
}

extern "C" int wyd_public_demo_unlock_select_character() {
  if (!g_pObjectManager) return 0;
  TMScene* scene = g_pObjectManager->GetCurrentScene();
  if (!scene || scene->GetSceneType() != ESCENE_TYPE::ESCENE_SELCHAR) return 0;

  auto* select_character = static_cast<TMSelectCharScene*>(scene);
  g_AccountLock = 1;
  if (select_character->m_pAccountLockDlg) {
    select_character->m_pAccountLockDlg->SetVisible(0);
  }
  if (select_character->m_pAccountLock) {
    select_character->m_pAccountLock->SetVisible(0);
  }
  if (select_character->m_pInputPWPanel) {
    select_character->m_pInputPWPanel->SetVisible(0);
  }
  return 1;
}

extern "C" int wyd_start_client() {
  wchar_t empty_cmdline[] = L"";
  return wWinMain(reinterpret_cast<HINSTANCE>(1), nullptr, empty_cmdline, 1);
}
