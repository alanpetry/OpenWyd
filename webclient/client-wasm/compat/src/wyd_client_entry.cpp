#include "windows.h"

#include "NewApp.h"
#include "ObjectManager.h"
#include "TMHuman.h"
#include "TMSelectServerScene.h"

#include <cstring>
#include <cstdio>

#if defined(__EMSCRIPTEN__)
#include <emscripten/console.h>
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

bool CheckOS();
char ReadNameFiltraDataBase();
char ReadChatFiltraDataBase();

extern char g_szOS[3];
extern ObjectManager* g_pObjectManager;

// Real client entrypoint from Projects/TMProject/NewApp.cpp
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow);

namespace {

NewApp* g_wyd_app = nullptr;
MSG g_wyd_msg{};
bool g_wyd_msg_initialized = false;
bool g_wyd_boot_in_progress = false;

void WydBootLog(const char* msg) {
#if defined(__EMSCRIPTEN__)
  emscripten_console_log(msg ? msg : "(null)");
#else
  std::fprintf(stderr, "%s\n", msg ? msg : "(null)");
#endif
}

TMSelectServerScene* WydSelectServerScene() {
  if (!g_pObjectManager) return nullptr;
  TMScene* scene = g_pObjectManager->GetCurrentScene();
  if (!scene || scene->GetSceneType() != ESCENE_TYPE::ESCENE_SELECT_SERVER) return nullptr;
  return static_cast<TMSelectServerScene*>(scene);
}

TMHuman* WydSelectServerHuman(unsigned int index) {
  if (index >= 50) return nullptr;
  TMSelectServerScene* scene = WydSelectServerScene();
  return scene ? scene->m_pCheckHumanList[index] : nullptr;
}

}  // namespace

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

extern "C" EMSCRIPTEN_KEEPALIVE int wyd_selserver_human_moving(unsigned int index) {
  TMHuman* human = WydSelectServerHuman(index);
  return human ? human->m_bMoveing : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int wyd_selserver_human_last_route_index(unsigned int index) {
  TMHuman* human = WydSelectServerHuman(index);
  return human ? human->m_nLastRouteIndex : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int wyd_selserver_human_max_route_index(unsigned int index) {
  TMHuman* human = WydSelectServerHuman(index);
  return human ? human->m_nMaxRouteIndex : 0;
}

extern "C" int wyd_set_game_state(int state) {
  if (!g_pObjectManager) return 0;
  if (state < static_cast<int>(ObjectManager::TM_GAME_STATE::TM_NONE_STATE) ||
      state > static_cast<int>(ObjectManager::TM_GAME_STATE::TM_FIELD2_STATE)) {
    return 0;
  }
  if (state == static_cast<int>(ObjectManager::TM_GAME_STATE::TM_DEMO_STATE)) {
    // The current wasm bridge does not provide full compatibility for demo-scene
    // startup dependencies yet, and entering this state can trap the runtime.
    // Keep the client alive by redirecting to select-server for now.
    WydBootLog("[wyd_set_state] TM_DEMO_STATE blocked, redirecting to TM_SELECTSERVER_STATE");
    g_pObjectManager->SetCurrentState(ObjectManager::TM_GAME_STATE::TM_SELECTSERVER_STATE);
    return 1;
  }
  g_pObjectManager->SetCurrentState(static_cast<ObjectManager::TM_GAME_STATE>(state));
  return 1;
}

extern "C" int wyd_start_client() {
  wchar_t empty_cmdline[] = L"";
  return wWinMain(reinterpret_cast<HINSTANCE>(1), nullptr, empty_cmdline, 1);
}
