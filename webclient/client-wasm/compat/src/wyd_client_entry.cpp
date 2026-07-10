#include "windows.h"

#include "d3d9.h"
#include "d3dx9.h"
#include "NewApp.h"
#include "ObjectManager.h"
#include "ResourceControl.h"
#include "SControl.h"
#include "TimerManager.h"
#include "TMGlobal.h"
#include "TMScene.h"
#include "TMSelectCharScene.h"
#include "TMSelectServerScene.h"

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

// Real client entrypoint from Projects/TMProject/NewApp.cpp
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow);

namespace {

NewApp* g_wyd_app = nullptr;
MSG g_wyd_msg{};
bool g_wyd_msg_initialized = false;
bool g_wyd_boot_in_progress = false;

constexpr unsigned int kWydMouseMove = 0x0200u;
constexpr unsigned int kWydLButtonDown = 0x0201u;
constexpr unsigned int kWydLButtonUp = 0x0202u;
constexpr unsigned int kWydRButtonDown = 0x0204u;
constexpr unsigned int kWydRButtonUp = 0x0205u;
constexpr unsigned int kWydMButtonDown = 0x0207u;
constexpr unsigned int kWydMButtonUp = 0x0208u;
constexpr unsigned int kWydMouseWheel = 0x020Au;
constexpr unsigned int kWydMkShift = 0x0004u;
constexpr unsigned int kWydMkControl = 0x0008u;

void WydBootLog(const char* msg) {
#if defined(__EMSCRIPTEN__)
  emscripten_console_log(msg ? msg : "(null)");
#else
  std::fprintf(stderr, "%s\n", msg ? msg : "(null)");
#endif
}

}  // namespace

extern "C" void wyd_dinput_mouse_event(unsigned int msg, unsigned int wParam, int x, int y, int wheel_delta);

extern "C" int wyd_mouse_event(unsigned int msg, unsigned int wParam, int x, int y, int wheel_delta) {
  wyd_dinput_mouse_event(msg, wParam, x, y, wheel_delta);

  if (!g_pEventTranslator) return 0;

  g_pEventTranslator->m_bShift = (wParam & kWydMkShift) ? 1 : 0;
  g_pEventTranslator->m_bCtrl = (wParam & kWydMkControl) ? 1 : 0;

  switch (msg) {
    case kWydMouseMove:
    case kWydLButtonDown:
    case kWydLButtonUp:
    case kWydRButtonDown:
    case kWydRButtonUp:
    case kWydMButtonDown:
    case kWydMButtonUp:
      g_pEventTranslator->OnMouseEvent(msg, wParam, x, y);
      break;
    case kWydMouseWheel:
      g_pEventTranslator->OnMouseEvent(kWydMouseMove, wParam, x, y);
      break;
    default:
      break;
  }

  return 1;
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

extern "C" int wyd_set_game_state(int state) {
  if (!g_pObjectManager) return 0;
  if (state < static_cast<int>(ObjectManager::TM_GAME_STATE::TM_NONE_STATE) ||
      state > static_cast<int>(ObjectManager::TM_GAME_STATE::TM_FIELD2_STATE)) {
    return 0;
  }
  g_pObjectManager->SetCurrentState(static_cast<ObjectManager::TM_GAME_STATE>(state));
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

extern "C" int wyd_start_client() {
  wchar_t empty_cmdline[] = L"";
  return wWinMain(reinterpret_cast<HINSTANCE>(1), nullptr, empty_cmdline, 1);
}
