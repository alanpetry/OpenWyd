#pragma once

#if defined(OPENWYD_LAB)

#include <cstddef>

struct IDirect3DDevice9;
class ObjectManager;
class RenderDevice;
class TimerManager;
class TreeNode;

// OPENWYD_LAB is a debug-only, offline scenario driver. The normal client
// never calls these hooks unless the dedicated build flag is present.
bool OpenWydLabIsEnabled();
void OpenWydLabPoll();
bool OpenWydLabShouldRunFrame(bool meshReady);
void OpenWydLabPrepareFrame(
	ObjectManager* objectManager,
	TimerManager* timerManager,
	bool meshReady);
void OpenWydLabPrepareRender(ObjectManager* objectManager);
void OpenWydLabOnFrameTickComplete();
void OpenWydLabOnBeforePresent(IDirect3DDevice9* device);
void OpenWydLabOnAfterPresent(long presentResult);

bool OpenWydLabIsIsolated();
unsigned int OpenWydLabClearColor();
void OpenWydLabRenderSubtree(TreeNode* root);

#if !defined(__EMSCRIPTEN__)
unsigned long OpenWydLabTimeGetTime();
unsigned long OpenWydLabGetTickCount();
#endif

extern "C"
{
	int wyd_lab_load_scenario(const void* bytes, unsigned int size);
	int wyd_lab_show(unsigned int frame);
	int wyd_lab_is_enabled();
	int wyd_lab_is_pending();
	int wyd_lab_last_result();
	unsigned int wyd_lab_current_frame();
	unsigned int wyd_lab_clock_ms();
	unsigned int wyd_lab_packet_hash();
	unsigned int wyd_lab_scenario_hash();
	const char* wyd_lab_status();
}

#endif
