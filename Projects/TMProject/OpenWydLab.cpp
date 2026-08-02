#include "pch.h"

#if defined(OPENWYD_LAB)

#include "OpenWydLab.h"

#include "Basedef.h"
#include "CFrame.h"
#include "MeshManager.h"
#include "NewApp.h"
#include "ObjectManager.h"
#include "OpenWydCompareRandom.h"
#include "RenderDevice.h"
#include "SControlContainer.h"
#include "TMCamera.h"
#include "TMEffectSkinMesh.h"
#include "TMFieldScene.h"
#include "TMGlobal.h"
#include "TMHuman.h"
#include "TMMesh.h"
#include "TMSkinMesh.h"
#include "TimerManager.h"
#include "TreeNode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
extern "C" void wyd_debug_set_fake_time(DWORD ms);
#else
#include <climits>
#include <cstddef>
#include <wincodec.h>
#undef timeGetTime
#undef GetTickCount
#endif

extern "C"
{
	int wyd_lab_scene_type();
	unsigned int wyd_lab_screen_width();
	unsigned int wyd_lab_screen_height();
	float wyd_lab_player_x();
	float wyd_lab_player_y();
	float wyd_lab_player_height();
	int wyd_lab_player_visible();
	int wyd_lab_player_hidden();
	int wyd_lab_player_has_skin();
	int wyd_lab_player_familiar_item();
	int wyd_lab_player_has_familiar();
	int wyd_lab_player_familiar_visible();
	int wyd_lab_player_familiar_has_skin();
	int wyd_lab_player_familiar_visibility_reason();
	int wyd_lab_player_class();
	int wyd_lab_player_motion();
	int wyd_lab_player_skin_type();
	float wyd_lab_player_speed();
	float wyd_lab_player_progress();
	int wyd_lab_player_moving();
	int wyd_lab_player_last_route();
	int wyd_lab_player_max_route();
	unsigned int wyd_lab_player_move_started_ms();
	unsigned int wyd_lab_player_animation_started_ms();
	int wyd_lab_player_animation_index();
	int wyd_lab_player_animation_last_index();
	unsigned int wyd_lab_player_skin_fps();
	unsigned int wyd_lab_player_skin_offset();
	unsigned int wyd_lab_player_skin_start_offset();
	int wyd_lab_player_skin_tick_last();
	int wyd_lab_player_skin_animation_base();
	unsigned int wyd_lab_player_pose_hash();
	float wyd_lab_render_fps();
	float wyd_lab_camera_x();
	float wyd_lab_camera_y();
	float wyd_lab_camera_z();
	float wyd_lab_camera_horizon();
	float wyd_lab_camera_vertical();
	float wyd_lab_camera_length();
	float wyd_lab_camera_height();
}

namespace
{
	constexpr std::uint32_t kMagic = 0x424C574Fu; // "OWLB"
	constexpr std::uint16_t kVersion = 1;
	constexpr std::uint32_t kNoFrame = 0xFFFFFFFFu;
	constexpr std::uint32_t kFnvOffset = 2166136261u;
	constexpr std::uint32_t kFnvPrime = 16777619u;

	enum class ScenarioKind : std::uint16_t
	{
		Field = 1,
		Isolated = 2,
		SelectCharacter = 3,
		Demo = 4,
	};

	enum class EventKind : std::uint16_t
	{
		CreateMob = 1,
		Action = 2,
		Motion = 3,
		Attack = 4,
		Teleport = 5,
	};

#pragma pack(push, 1)
	struct LabHeader
	{
		std::uint32_t magic;
		std::uint16_t version;
		std::uint16_t headerSize;
		std::uint32_t totalSize;
		std::uint16_t kind;
		std::uint16_t flags;
		std::uint32_t seed;
		std::uint32_t startTimeMs;
		std::uint32_t tickMs;
		std::uint32_t clearColor;
		float cameraHorizon;
		float cameraVertical;
		float cameraLength;
		float cameraHeight;
		std::uint16_t actorCount;
		std::uint16_t eventCount;
	};

	struct LabActor
	{
		std::uint16_t id;
		std::int16_t posX;
		std::int16_t posY;
		std::uint8_t classId;
		std::uint8_t guildLevel;
		std::uint16_t guild;
		char name[16];
		std::uint16_t equip[18];
		std::uint8_t equip2[18];
		std::int16_t level;
		std::int32_t ac;
		std::int32_t damage;
		std::int32_t maxHp;
		std::int32_t maxMp;
		std::int32_t hp;
		std::int32_t mp;
		std::int16_t str;
		std::int16_t intel;
		std::int16_t dex;
		std::int16_t con;
	};

	struct LabEvent
	{
		std::uint32_t frame;
		std::uint16_t kind;
		std::uint16_t actor;
		std::int32_t a;
		std::int32_t b;
		std::int32_t c;
		std::int32_t d;
		std::uint8_t data[24];
	};
#pragma pack(pop)

	static_assert(sizeof(LabHeader) == 52, "OWLB header layout changed");
	static_assert(sizeof(LabActor) == 114, "OWLB actor layout changed");
	static_assert(sizeof(LabEvent) == 48, "OWLB event layout changed");

	struct Runtime
	{
		bool enabled = false;
		bool scenarioLoaded = false;
		bool resetRequested = false;
		bool frameExecuting = false;
		bool warmupExecuting = false;
		bool warmupComplete = false;
		bool captureRequested = false;
		bool captureWritten = false;
		int lastResult = 0;
		std::uint32_t targetFrame = 0;
		std::uint32_t currentFrame = kNoFrame;
		std::uint32_t clockMs = 0;
		std::uint32_t packetHash = kFnvOffset;
		std::uint32_t scenarioHash = 0;
		std::uint32_t generation = 0;
		std::uint32_t completedGeneration = 0;
		LabHeader header{};
		std::vector<LabActor> actors;
		std::vector<LabEvent> events;
		std::string status = "idle";
		std::string controlDirectory;
		std::string capturePath;
		std::string scenarioPath;
	};

	Runtime g_lab;

	void ApplyCamera(ObjectManager* objectManager);

	std::uint32_t Fnv1a(
		const void* bytes,
		std::size_t size,
		std::uint32_t seed = kFnvOffset)
	{
		const auto* value = static_cast<const std::uint8_t*>(bytes);
		std::uint32_t hash = seed;
		for (std::size_t i = 0; i < size; ++i)
		{
			hash ^= value[i];
			hash *= kFnvPrime;
		}
		return hash;
	}

	void SetStatus(const char* status, int result)
	{
		g_lab.status = status ? status : "";
		g_lab.lastResult = result;
	}

#if !defined(__EMSCRIPTEN__)
	template <typename Interface>
	void ReleaseInterface(Interface*& value)
	{
		if (value)
		{
			value->Release();
			value = nullptr;
		}
	}

	HRESULT SaveSurfaceToPngWithWic(
		IDirect3DSurface9* surface,
		unsigned int width,
		unsigned int height,
		const std::string& path)
	{
		if (!surface || width == 0 || height == 0 || path.empty())
			return E_INVALIDARG;

		const unsigned long long stride64 =
			static_cast<unsigned long long>(width) * 4ull;
		const unsigned long long size64 =
			stride64 * static_cast<unsigned long long>(height);
		if (stride64 > UINT_MAX || size64 > UINT_MAX)
			return E_OUTOFMEMORY;

		D3DLOCKED_RECT locked{};
		HRESULT result = surface->LockRect(&locked, nullptr, D3DLOCK_READONLY);
		if (FAILED(result))
			return result;

		const UINT stride = static_cast<UINT>(stride64);
		const UINT bufferSize = static_cast<UINT>(size64);
		std::vector<BYTE> pixels(bufferSize);
		for (unsigned int y = 0; y < height; ++y)
		{
			const BYTE* source =
				static_cast<const BYTE*>(locked.pBits) +
				static_cast<ptrdiff_t>(y) * locked.Pitch;
			BYTE* destination =
				pixels.data() + static_cast<size_t>(y) * stride;
			std::memcpy(destination, source, stride);
			for (unsigned int x = 0; x < width; ++x)
				destination[x * 4u + 3u] = 0xFF;
		}
		surface->UnlockRect();

		const HRESULT comResult =
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool uninitializeCom =
			comResult == S_OK || comResult == S_FALSE;
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
			return comResult;

		IWICImagingFactory* factory = nullptr;
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		IPropertyBag2* properties = nullptr;

		result = CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory));

		int required = 0;
		std::wstring widePath;
		if (SUCCEEDED(result))
		{
			required = MultiByteToWideChar(
				CP_ACP,
				0,
				path.c_str(),
				-1,
				nullptr,
				0);
			if (required <= 0)
				result = HRESULT_FROM_WIN32(GetLastError());
		}
		if (SUCCEEDED(result))
		{
			widePath.resize(static_cast<size_t>(required));
			if (MultiByteToWideChar(
				CP_ACP,
				0,
				path.c_str(),
				-1,
				&widePath[0],
				required) <= 0)
			{
				result = HRESULT_FROM_WIN32(GetLastError());
			}
		}
		if (SUCCEEDED(result))
			result = factory->CreateStream(&stream);
		if (SUCCEEDED(result))
			result = stream->InitializeFromFilename(
				widePath.c_str(),
				GENERIC_WRITE);
		if (SUCCEEDED(result))
			result = factory->CreateEncoder(
				GUID_ContainerFormatPng,
				nullptr,
				&encoder);
		if (SUCCEEDED(result))
			result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
		if (SUCCEEDED(result))
			result = encoder->CreateNewFrame(&frame, &properties);
		if (SUCCEEDED(result))
			result = frame->Initialize(properties);
		if (SUCCEEDED(result))
			result = frame->SetSize(width, height);

		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
		if (SUCCEEDED(result))
			result = frame->SetPixelFormat(&pixelFormat);
		if (SUCCEEDED(result) &&
			!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
		{
			result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
		}
		if (SUCCEEDED(result))
			result = frame->WritePixels(
				height,
				stride,
				bufferSize,
				pixels.data());
		if (SUCCEEDED(result))
			result = frame->Commit();
		if (SUCCEEDED(result))
			result = encoder->Commit();

		ReleaseInterface(properties);
		ReleaseInterface(frame);
		ReleaseInterface(encoder);
		ReleaseInterface(stream);
		ReleaseInterface(factory);
		if (uninitializeCom)
			CoUninitialize();
		return result;
	}

	HRESULT CaptureBackbuffer(
		IDirect3DDevice9* device,
		const std::string& path,
		std::string& failedStage)
	{
		failedStage = "get-render-target";
		IDirect3DSurface9* backbuffer = nullptr;
		HRESULT result = device->GetRenderTarget(0, &backbuffer);
		if (FAILED(result) || !backbuffer)
			return result;

		D3DSURFACE_DESC description{};
		IDirect3DSurface9* resolved = nullptr;
		IDirect3DSurface9* systemMemory = nullptr;
		IDirect3DSurface9* encodeSurface = nullptr;
		IDirect3DSurface9* readSource = nullptr;

		failedStage = "get-description";
		result = backbuffer->GetDesc(&description);
		if (SUCCEEDED(result))
		{
			failedStage = "create-resolve-target";
			result = device->CreateRenderTarget(
				description.Width,
				description.Height,
				description.Format,
				D3DMULTISAMPLE_NONE,
				0,
				FALSE,
				&resolved,
				nullptr);
		}
		if (SUCCEEDED(result))
		{
			failedStage = "resolve-backbuffer";
			result = device->StretchRect(
				backbuffer,
				nullptr,
				resolved,
				nullptr,
				D3DTEXF_NONE);
			if (SUCCEEDED(result))
				readSource = resolved;
		}
		if (SUCCEEDED(result))
		{
			failedStage = "create-system-surface";
			result = device->CreateOffscreenPlainSurface(
				description.Width,
				description.Height,
				description.Format,
				D3DPOOL_SYSTEMMEM,
				&systemMemory,
				nullptr);
		}
		if (SUCCEEDED(result))
		{
			failedStage = "read-render-target";
			result = device->GetRenderTargetData(readSource, systemMemory);
		}
		if (SUCCEEDED(result) && description.Format != D3DFMT_A8R8G8B8)
		{
			failedStage = "create-encode-surface";
			result = device->CreateOffscreenPlainSurface(
				description.Width,
				description.Height,
				D3DFMT_A8R8G8B8,
				D3DPOOL_SYSTEMMEM,
				&encodeSurface,
				nullptr);
			if (SUCCEEDED(result))
			{
				failedStage = "convert-encode-surface";
				result = D3DXLoadSurfaceFromSurface(
					encodeSurface,
					nullptr,
					nullptr,
					systemMemory,
					nullptr,
					nullptr,
					D3DX_FILTER_NONE,
					0);
			}
		}
		if (SUCCEEDED(result))
		{
			failedStage = "encode-png";
			IDirect3DSurface9* source =
				encodeSurface ? encodeSurface : systemMemory;
			result = D3DXSaveSurfaceToFileA(
				path.c_str(),
				D3DXIFF_PNG,
				source,
				nullptr,
				nullptr);
			if (FAILED(result))
			{
				failedStage = "encode-png-wic";
				result = SaveSurfaceToPngWithWic(
					source,
					description.Width,
					description.Height,
					path);
			}
		}

		ReleaseInterface(encodeSurface);
		ReleaseInterface(systemMemory);
		ReleaseInterface(resolved);
		ReleaseInterface(backbuffer);
		if (SUCCEEDED(result))
			failedStage = "complete";
		return result;
	}
#endif

	bool ParseScenario(const void* bytes, std::size_t size)
	{
		if (!bytes || size < sizeof(LabHeader))
		{
			SetStatus("scenario-too-small", -1);
			return false;
		}

		LabHeader header{};
		std::memcpy(&header, bytes, sizeof(header));
		const std::size_t expected =
			sizeof(LabHeader) +
			static_cast<std::size_t>(header.actorCount) * sizeof(LabActor) +
			static_cast<std::size_t>(header.eventCount) * sizeof(LabEvent);
		if (header.magic != kMagic ||
			header.version != kVersion ||
			header.headerSize != sizeof(LabHeader) ||
			header.totalSize != size ||
			expected != size ||
			header.actorCount == 0 ||
			header.actorCount > 64 ||
			header.eventCount > 1024 ||
			header.tickMs == 0)
		{
			SetStatus("invalid-scenario", -2);
			return false;
		}

		const auto* cursor =
			static_cast<const std::uint8_t*>(bytes) + sizeof(LabHeader);
		std::vector<LabActor> actors(header.actorCount);
		std::memcpy(
			actors.data(),
			cursor,
			actors.size() * sizeof(LabActor));
		cursor += actors.size() * sizeof(LabActor);
		std::vector<LabEvent> events(header.eventCount);
		if (!events.empty())
		{
			std::memcpy(
				events.data(),
				cursor,
				events.size() * sizeof(LabEvent));
		}
		std::stable_sort(
			events.begin(),
			events.end(),
			[](const LabEvent& left, const LabEvent& right)
			{
				return left.frame < right.frame;
			});

		g_lab.header = header;
		g_lab.actors.swap(actors);
		g_lab.events.swap(events);
		g_lab.scenarioHash = Fnv1a(bytes, size);
		g_lab.packetHash = kFnvOffset;
		g_lab.scenarioLoaded = true;
		g_lab.enabled = true;
		g_lab.resetRequested = true;
		g_lab.currentFrame = kNoFrame;
		g_lab.captureWritten = false;
		SetStatus("scenario-loaded", 1);
		return true;
	}

	void FillScore(STRUCT_SCORE& score, const LabActor& actor)
	{
		std::memset(&score, 0, sizeof(score));
		score.Level = actor.level;
		score.Ac = actor.ac;
		score.Damage = actor.damage;
		score.AttackRun = 3;
		score.MaxHp = actor.maxHp;
		score.MaxMp = actor.maxMp;
		score.Hp = actor.hp;
		score.Mp = actor.mp;
		score.Str = actor.str;
		score.Int = actor.intel;
		score.Dex = actor.dex;
		score.Con = actor.con;
	}

	void FillOwnMob(ObjectManager* objectManager, const LabActor& actor)
	{
		std::memset(&objectManager->m_stMobData, 0, sizeof(objectManager->m_stMobData));
		std::memset(&objectManager->m_stSelCharData, 0, sizeof(objectManager->m_stSelCharData));
		std::memset(objectManager->m_stItemCargo, 0, sizeof(objectManager->m_stItemCargo));
		std::memcpy(
			objectManager->m_stMobData.MobName,
			actor.name,
			sizeof(objectManager->m_stMobData.MobName));
		objectManager->m_stMobData.MobName[15] = 0;
		objectManager->m_stMobData.Class = static_cast<char>(actor.classId);
		objectManager->m_stMobData.Guild = actor.guild;
		objectManager->m_stMobData.GuildLevel =
			static_cast<char>(actor.guildLevel);
		objectManager->m_stMobData.Coin = 1000;
		objectManager->m_stMobData.HomeTownX =
			static_cast<unsigned short>(actor.posX);
		objectManager->m_stMobData.HomeTownY =
			static_cast<unsigned short>(actor.posY);
		FillScore(objectManager->m_stMobData.CurrentScore, actor);
		objectManager->m_stMobData.BaseScore =
			objectManager->m_stMobData.CurrentScore;
		std::memset(
			objectManager->m_stMobData.ShortSkill,
			-1,
			sizeof(objectManager->m_stMobData.ShortSkill));
		std::memset(
			objectManager->m_cShortSkill,
			-1,
			sizeof(objectManager->m_cShortSkill));

		for (int i = 0; i < 18; ++i)
		{
			objectManager->m_stMobData.Equip[i].sIndex =
				static_cast<short>(actor.equip[i]);
		}
		// Mount vitality is encoded by the official item ability. The compact
		// Lab actor carries it in equip2[14], then creates the real item effect.
		if (actor.equip[14] && actor.equip2[14])
		{
			objectManager->m_stMobData.Equip[14].stEffect[0].cEffect = 80;
			objectManager->m_stMobData.Equip[14].stEffect[0].cValue =
				actor.equip2[14];
		}

		objectManager->m_cCharacterSlot = 0;
		objectManager->m_dwCharID = actor.id;
		objectManager->m_nServerGroupIndex = 0;
		objectManager->m_nServerIndex = 0;
		objectManager->m_usWarGuild = 0xFFFF;
		objectManager->m_usAllyGuild = 0;
		std::memcpy(
			objectManager->m_stSelCharData.MobName[0],
			objectManager->m_stMobData.MobName,
			16);
		objectManager->m_stSelCharData.HomeTownX[0] =
			objectManager->m_stMobData.HomeTownX;
		objectManager->m_stSelCharData.HomeTownY[0] =
			objectManager->m_stMobData.HomeTownY;
		objectManager->m_stSelCharData.Score[0] =
			objectManager->m_stMobData.CurrentScore;
		std::memcpy(
			objectManager->m_stSelCharData.Equip[0],
			objectManager->m_stMobData.Equip,
			sizeof(objectManager->m_stSelCharData.Equip[0]));
	}

	template <typename T>
	void InitializePacket(T& packet, unsigned short type, unsigned short id)
	{
		std::memset(&packet, 0, sizeof(packet));
		packet.Header.Size = static_cast<unsigned short>(sizeof(packet));
		packet.Header.Type = type;
		packet.Header.ID = id;
		packet.Header.Tick = g_lab.clockMs;
	}

	void InjectPacket(ObjectManager* objectManager, MSG_STANDARD* packet)
	{
		if (!objectManager || !packet || packet->Size < sizeof(MSG_STANDARD))
			return;
		g_lab.packetHash = Fnv1a(packet, packet->Size, g_lab.packetHash);
		objectManager->OnPacketEvent(packet->Type, reinterpret_cast<char*>(packet));
	}

	void InjectCreate(ObjectManager* objectManager, const LabActor& actor)
	{
		MSG_CreateMob packet{};
		InitializePacket(packet, MSG_CreateMob_Opcode, actor.id);
		packet.PosX = actor.posX;
		packet.PosY = actor.posY;
		packet.MobID = actor.id;
		std::memcpy(packet.MobName, actor.name, sizeof(packet.MobName));
		packet.MobName[15] = 0;
		std::memcpy(packet.Equip, actor.equip, sizeof(packet.Equip));
		std::memcpy(packet.Equip2, actor.equip2, sizeof(packet.Equip2));
		packet.Guild = actor.guild;
		packet.GuildLevel = static_cast<char>(actor.guildLevel);
		FillScore(packet.Score, actor);
		InjectPacket(objectManager, &packet.Header);
	}

	void InjectEvent(ObjectManager* objectManager, const LabEvent& event)
	{
		if (!objectManager || event.actor >= g_lab.actors.size())
			return;
		const LabActor& actor = g_lab.actors[event.actor];
		switch (static_cast<EventKind>(event.kind))
		{
		case EventKind::CreateMob:
			InjectCreate(objectManager, actor);
			break;
		case EventKind::Action:
		{
			MSG_Action packet{};
			InitializePacket(packet, MSG_Action_Opcode, actor.id);
			packet.PosX = actor.posX;
			packet.PosY = actor.posY;
			packet.Effect = static_cast<char>(event.a);
			packet.Speed = event.d > 0 ? event.d : 6;
			packet.TargetX = static_cast<unsigned short>(event.b);
			packet.TargetY = static_cast<unsigned short>(event.c);
			std::memcpy(packet.Route, event.data, sizeof(packet.Route));
			InjectPacket(objectManager, &packet.Header);
			break;
		}
		case EventKind::Motion:
		{
			MSG_Motion packet{};
			InitializePacket(packet, MSG_Motion_Opcode, actor.id);
			packet.Motion = static_cast<short>(event.a);
			packet.Parm = static_cast<short>(event.b);
			std::memcpy(&packet.Direction, &event.c, sizeof(packet.Direction));
			InjectPacket(objectManager, &packet.Header);
			break;
		}
		case EventKind::Attack:
		{
			MSG_AttackOne packet{};
			InitializePacket(packet, MSG_Attack_One_Opcode, actor.id);
			packet.PosX = static_cast<unsigned short>(actor.posX);
			packet.PosY = static_cast<unsigned short>(actor.posY);
			packet.TargetX = static_cast<unsigned short>(event.b);
			packet.TargetY = static_cast<unsigned short>(event.c);
			packet.AttackerID = actor.id;
			packet.Motion = static_cast<char>(event.a);
			packet.FlagLocal = 1;
			packet.SkillIndex = static_cast<short>(event.d);
			packet.CurrentMp = actor.mp;
			const std::uint16_t targetActor =
				event.data[0] < g_lab.actors.size() ?
				static_cast<std::uint16_t>(event.data[0]) :
				static_cast<std::uint16_t>(0);
			packet.Dam[0].TargetID = g_lab.actors[targetActor].id;
			packet.Dam[0].Damage = 1;
			InjectPacket(objectManager, &packet.Header);
			break;
		}
		case EventKind::Teleport:
		{
			MSG_CNFCharacterLogin packet{};
			InitializePacket(
				packet,
				MSG_CNFCharacterLogin_Opcode,
				g_lab.actors[0].id);
			packet.PosX = static_cast<short>(event.b);
			packet.PosY = static_cast<short>(event.c);
			packet.ClientID = g_lab.actors[0].id;
			packet.Slot = 0;
			packet.MOB = objectManager->m_stMobData;
			packet.MOB.HomeTownX = static_cast<unsigned short>(event.b);
			packet.MOB.HomeTownY = static_cast<unsigned short>(event.c);
			std::memset(packet.ShortSkill, -1, sizeof(packet.ShortSkill));
			InjectPacket(objectManager, &packet.Header);
			break;
		}
		default:
			break;
		}
	}

	bool ApplyScenario(ObjectManager* objectManager)
	{
		if (!objectManager || g_lab.actors.empty())
			return false;

		// Scene constructors and InitObject stamp animation/effect state. Reset
		// the deterministic clock before creating any of them so a hot swap
		// cannot inherit the previous scenario's final time.
		g_lab.clockMs = g_lab.header.startTimeMs;
#if defined(__EMSCRIPTEN__)
		wyd_debug_set_fake_time(g_lab.clockMs);
#endif
		if (g_pTimerManager)
			g_pTimerManager->SetServerTime(g_lab.clockMs);
		OpenWydCompareRandomArm(g_lab.header.seed);
		g_lab.packetHash = kFnvOffset;
		FillOwnMob(objectManager, g_lab.actors[0]);
		objectManager->m_bVisualControl =
			static_cast<ScenarioKind>(g_lab.header.kind) ==
			ScenarioKind::Isolated ? 0 : 1;

		const auto kind = static_cast<ScenarioKind>(g_lab.header.kind);
		switch (kind)
		{
		case ScenarioKind::Field:
		case ScenarioKind::Isolated:
			objectManager->SetCurrentState(
				g_pCurrentScene ?
				ObjectManager::TM_GAME_STATE::TM_FIELD2_STATE :
				ObjectManager::TM_GAME_STATE::TM_FIELD_STATE);
			break;
		case ScenarioKind::SelectCharacter:
			// The offline Lab account has no secondary-password exchange.
			// Mark it unlocked before InitializeScene so both runtimes follow
			// the same official selection UI path.
			g_AccountLock = 1;
			objectManager->SetCurrentState(
				ObjectManager::TM_GAME_STATE::TM_SELECTCHAR_STATE);
			break;
		case ScenarioKind::Demo:
			objectManager->SetCurrentState(
				ObjectManager::TM_GAME_STATE::TM_DEMO_STATE);
			break;
		default:
			return false;
		}

		if ((kind == ScenarioKind::Field || kind == ScenarioKind::Isolated) &&
			g_pCurrentScene &&
			g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD)
		{
			auto* field = static_cast<TMFieldScene*>(g_pCurrentScene);
			if (kind == ScenarioKind::Isolated &&
				field->m_pControlContainer)
			{
				field->m_pControlContainer->m_bInvisibleUI = 1;
			}
			field->m_vecMyNext.x = g_lab.actors[0].posX;
			field->m_vecMyNext.y = g_lab.actors[0].posY;
			for (std::size_t i = 1; i < g_lab.actors.size(); ++i)
				InjectCreate(objectManager, g_lab.actors[i]);

			// TMHuman::FrameMove deliberately skips objects that have not yet
			// passed the official visibility test. In the normal online flow
			// the Field has already rendered while packets arrive; a direct
			// Lab transition has not. Classify the actors once after applying
			// the deterministic camera before the scene preparation pass.
			ApplyCamera(objectManager);
			objectManager->m_pCamera->GetCameraPos();
			objectManager->m_pCamera->GetCameraLookatDir();
			for (const LabActor& actor : g_lab.actors)
			{
				auto* human = static_cast<TMHuman*>(
					objectManager->GetHumanByID(actor.id));
				if (human)
				{
					human->IsVisible();
				}
			}
		}

		g_lab.currentFrame = kNoFrame;
		g_lab.warmupComplete = false;
		g_lab.resetRequested = false;
		g_lab.captureWritten = false;
		return g_pCurrentScene != nullptr;
	}

	void ApplyCamera(ObjectManager* objectManager)
	{
		if (!objectManager || !objectManager->m_pCamera)
			return;
		TMCamera* camera = objectManager->m_pCamera;
		// Field initialization may leave a one-shot earthquake/transition
		// timestamp armed. GetCameraPos() would then replace the scenario
		// angles with the old backup angles on the first rendered frame only.
		// A Lab reset owns the complete camera state, including that transient.
		camera->m_dwSetTime = 0;
		camera->m_nEarthLevel = 0;
		camera->m_fBackHorizonAngle = g_lab.header.cameraHorizon;
		camera->m_fBackVerticalAngle = g_lab.header.cameraVertical;
		camera->m_fHorizonAngle = g_lab.header.cameraHorizon;
		camera->m_fVerticalAngle = g_lab.header.cameraVertical;
		camera->m_fSightLength = g_lab.header.cameraLength;
		camera->m_fWantLength = g_lab.header.cameraLength;
		camera->m_fCamHeight = g_lab.header.cameraHeight;
		camera->m_fLastSightLength = g_lab.header.cameraLength;
		camera->m_AutoSumLen = 0.0f;
		camera->m_AutoSumLenOutline = 0.0f;
		camera->m_bLockCamera = 1;
	}

#if !defined(__EMSCRIPTEN__)
	std::string JoinPath(const std::string& directory, const char* leaf)
	{
		if (directory.empty())
			return leaf ? leaf : "";
		const char last = directory[directory.size() - 1];
		return directory + (last == '\\' || last == '/' ? "" : "\\") +
			(leaf ? leaf : "");
	}

	bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& bytes)
	{
		FILE* stream = nullptr;
		if (fopen_s(&stream, path.c_str(), "rb") != 0 || !stream)
			return false;
		fseek(stream, 0, SEEK_END);
		const long length = ftell(stream);
		fseek(stream, 0, SEEK_SET);
		if (length <= 0)
		{
			fclose(stream);
			return false;
		}
		bytes.resize(static_cast<std::size_t>(length));
		const bool ok =
			fread(bytes.data(), 1, bytes.size(), stream) == bytes.size();
		fclose(stream);
		return ok;
	}

	std::string ReadEnvironment(const char* name)
	{
		char value[32768]{};
		const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
		return length > 0 && length < sizeof(value) ?
			std::string(value, length) :
			std::string();
	}

	void WriteNativeResponse(const char* status)
	{
		if (g_lab.controlDirectory.empty())
			return;
		const std::string temporary =
			JoinPath(g_lab.controlDirectory, "native-response.tmp");
		const std::string destination =
			JoinPath(g_lab.controlDirectory, "native-response.txt");
		FILE* stream = nullptr;
		if (fopen_s(&stream, temporary.c_str(), "wb") != 0 || !stream)
			return;
		std::fprintf(stream, "generation=%u\n", g_lab.generation);
		std::fprintf(stream, "status=%s\n", status ? status : g_lab.status.c_str());
		std::fprintf(stream, "result=%d\n", g_lab.lastResult);
		std::fprintf(stream, "frame=%u\n", g_lab.currentFrame);
		std::fprintf(stream, "clock_ms=%u\n", g_lab.clockMs);
		std::fprintf(stream, "scenario_hash=%08x\n", g_lab.scenarioHash);
		std::fprintf(stream, "packet_hash=%08x\n", g_lab.packetHash);
		std::fprintf(stream, "scene_type=%d\n", wyd_lab_scene_type());
		std::fprintf(stream, "screen_width=%u\n", wyd_lab_screen_width());
		std::fprintf(stream, "screen_height=%u\n", wyd_lab_screen_height());
		std::fprintf(stream, "player_x=%.9g\n", wyd_lab_player_x());
		std::fprintf(stream, "player_y=%.9g\n", wyd_lab_player_y());
		std::fprintf(stream, "player_height=%.9g\n", wyd_lab_player_height());
		std::fprintf(stream, "player_visible=%d\n", wyd_lab_player_visible());
		std::fprintf(stream, "player_hidden=%d\n", wyd_lab_player_hidden());
		std::fprintf(stream, "player_has_skin=%d\n", wyd_lab_player_has_skin());
		std::fprintf(stream, "player_familiar_item=%d\n", wyd_lab_player_familiar_item());
		std::fprintf(stream, "player_has_familiar=%d\n", wyd_lab_player_has_familiar());
		std::fprintf(stream, "player_familiar_visible=%d\n", wyd_lab_player_familiar_visible());
		std::fprintf(stream, "player_familiar_has_skin=%d\n", wyd_lab_player_familiar_has_skin());
		std::fprintf(stream, "player_familiar_visibility_reason=%d\n", wyd_lab_player_familiar_visibility_reason());
		std::fprintf(stream, "player_class=%d\n", wyd_lab_player_class());
		std::fprintf(stream, "player_motion=%d\n", wyd_lab_player_motion());
		std::fprintf(stream, "player_skin_type=%d\n", wyd_lab_player_skin_type());
		std::fprintf(stream, "player_speed=%.9g\n", wyd_lab_player_speed());
		std::fprintf(stream, "player_progress=%.9g\n", wyd_lab_player_progress());
		std::fprintf(stream, "player_moving=%d\n", wyd_lab_player_moving());
		std::fprintf(stream, "player_last_route=%d\n", wyd_lab_player_last_route());
		std::fprintf(stream, "player_max_route=%d\n", wyd_lab_player_max_route());
		std::fprintf(stream, "player_move_started_ms=%u\n", wyd_lab_player_move_started_ms());
		std::fprintf(stream, "player_animation_started_ms=%u\n", wyd_lab_player_animation_started_ms());
		std::fprintf(stream, "player_animation_index=%d\n", wyd_lab_player_animation_index());
		std::fprintf(stream, "player_animation_last_index=%d\n", wyd_lab_player_animation_last_index());
		std::fprintf(stream, "player_skin_fps=%u\n", wyd_lab_player_skin_fps());
		std::fprintf(stream, "player_skin_offset=%u\n", wyd_lab_player_skin_offset());
		std::fprintf(stream, "player_skin_start_offset=%u\n", wyd_lab_player_skin_start_offset());
		std::fprintf(stream, "player_skin_tick_last=%d\n", wyd_lab_player_skin_tick_last());
		std::fprintf(stream, "player_skin_animation_base=%d\n", wyd_lab_player_skin_animation_base());
		std::fprintf(stream, "player_pose_hash=%08x\n", wyd_lab_player_pose_hash());
		std::fprintf(stream, "render_fps=%.9g\n", wyd_lab_render_fps());
		std::fprintf(stream, "camera_x=%.9g\n", wyd_lab_camera_x());
		std::fprintf(stream, "camera_y=%.9g\n", wyd_lab_camera_y());
		std::fprintf(stream, "camera_z=%.9g\n", wyd_lab_camera_z());
		std::fprintf(stream, "camera_horizon=%.9g\n", wyd_lab_camera_horizon());
		std::fprintf(stream, "camera_vertical=%.9g\n", wyd_lab_camera_vertical());
		std::fprintf(stream, "camera_length=%.9g\n", wyd_lab_camera_length());
		std::fprintf(stream, "camera_height=%.9g\n", wyd_lab_camera_height());
		std::fprintf(stream, "png=%s\n", g_lab.capturePath.c_str());
		fclose(stream);
		MoveFileExA(
			temporary.c_str(),
			destination.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	}

	bool ParseRequest(
		const std::string& text,
		std::uint32_t& generation,
		std::string& scenario,
		std::uint32_t& frame,
		std::string& capture,
		bool& quit)
	{
		generation = 0;
		frame = 0;
		quit = false;
		std::size_t offset = 0;
		while (offset < text.size())
		{
			const std::size_t end = text.find('\n', offset);
			std::string line = text.substr(
				offset,
				end == std::string::npos ? std::string::npos : end - offset);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const std::size_t separator = line.find('=');
			const std::string key = line.substr(0, separator);
			const std::string value =
				separator == std::string::npos ?
				std::string() :
				line.substr(separator + 1);
			if (key == "generation")
				generation = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
			else if (key == "scenario")
				scenario = value;
			else if (key == "frame")
				frame = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
			else if (key == "capture")
				capture = value;
			else if (key == "quit")
				quit = value == "1";
			if (end == std::string::npos)
				break;
			offset = end + 1;
		}
		return generation != 0 && (quit || (!scenario.empty() && !capture.empty()));
	}
#endif
}

bool OpenWydLabIsEnabled()
{
	return g_lab.enabled;
}

void OpenWydLabPoll()
{
#if !defined(__EMSCRIPTEN__)
	if (g_lab.controlDirectory.empty())
	{
		g_lab.controlDirectory = ReadEnvironment("OPENWYD_LAB_CONTROL_DIR");
		g_lab.enabled = !g_lab.controlDirectory.empty();
		if (g_lab.enabled)
			SetStatus("native-ready", 1);
	}
	if (!g_lab.enabled)
		return;

	const std::string requestPath =
		JoinPath(g_lab.controlDirectory, "native-request.txt");
	std::vector<std::uint8_t> requestBytes;
	if (!ReadWholeFile(requestPath, requestBytes))
		return;
	const std::string text(
		reinterpret_cast<const char*>(requestBytes.data()),
		requestBytes.size());
	std::uint32_t generation = 0;
	std::uint32_t frame = 0;
	std::string scenario;
	std::string capture;
	bool quit = false;
	if (!ParseRequest(text, generation, scenario, frame, capture, quit) ||
		generation <= g_lab.generation)
	{
		return;
	}
	g_lab.generation = generation;
	if (quit)
	{
		SetStatus("quitting", 1);
		WriteNativeResponse("quitting");
		PostQuitMessage(0);
		return;
	}

	std::vector<std::uint8_t> scenarioBytes;
	if (!ReadWholeFile(scenario, scenarioBytes) ||
		!ParseScenario(scenarioBytes.data(), scenarioBytes.size()))
	{
		WriteNativeResponse(g_lab.status.c_str());
		return;
	}
	g_lab.scenarioPath = scenario;
	g_lab.capturePath = capture;
	g_lab.targetFrame = frame;
	g_lab.resetRequested = true;
	g_lab.captureRequested = true;
	SetStatus("pending", 1);
	WriteNativeResponse("accepted");
#endif
}

bool OpenWydLabShouldRunFrame(bool meshReady)
{
	if (!g_lab.enabled)
		return true;
	if (!meshReady)
		return true;
	return g_lab.resetRequested ||
		(g_lab.scenarioLoaded &&
			(g_lab.currentFrame == kNoFrame ||
				g_lab.currentFrame < g_lab.targetFrame));
}

void OpenWydLabPrepareFrame(
	ObjectManager* objectManager,
	TimerManager* timerManager,
	bool meshReady)
{
	if (!g_lab.enabled || !g_lab.scenarioLoaded || !meshReady)
	{
		g_lab.frameExecuting = false;
		return;
	}
	if (g_pDevice)
	{
		g_pDevice->m_fFPS = g_lab.header.tickMs ?
			1000.0f / static_cast<float>(g_lab.header.tickMs) :
			30.0f;
	}
	if (g_lab.resetRequested && !ApplyScenario(objectManager))
	{
		SetStatus("scenario-apply-failed", -3);
		g_lab.frameExecuting = false;
#if !defined(__EMSCRIPTEN__)
		WriteNativeResponse(g_lab.status.c_str());
#endif
		return;
	}
	if (g_lab.currentFrame == kNoFrame && !g_lab.warmupComplete)
	{
		// Direct scene construction has not passed through the render-time
		// visibility and bone initialization that normally happens while the
		// online transition is loading. Run one deterministic preparation
		// pass with no Lab events and no logical frame number. Frame zero is
		// therefore immediately useful while the packet timeline remains
		// indexed exactly as authored.
		g_lab.warmupExecuting = true;
		g_lab.warmupComplete = true;
		g_lab.frameExecuting = true;
		g_lab.captureWritten = false;
		SetStatus("warming-up", 1);
		return;
	}

	g_lab.currentFrame =
		g_lab.currentFrame == kNoFrame ? 0 : g_lab.currentFrame + 1;
	g_lab.clockMs =
		g_lab.header.startTimeMs +
		g_lab.currentFrame * g_lab.header.tickMs;
#if defined(__EMSCRIPTEN__)
	wyd_debug_set_fake_time(g_lab.clockMs);
#endif
	if (timerManager)
		timerManager->SetServerTime(g_lab.clockMs);
	for (const LabEvent& event : g_lab.events)
	{
		if (event.frame == g_lab.currentFrame)
			InjectEvent(objectManager, event);
	}
	g_lab.frameExecuting = true;
	g_lab.captureWritten = false;
	SetStatus("running", 1);
}

void OpenWydLabPrepareRender(ObjectManager* objectManager)
{
	if (!g_lab.enabled || !g_lab.frameExecuting)
		return;
	const auto kind = static_cast<ScenarioKind>(g_lab.header.kind);
	if (kind == ScenarioKind::Field || kind == ScenarioKind::Isolated)
		ApplyCamera(objectManager);
}

void OpenWydLabOnFrameTickComplete()
{
	if (!g_lab.enabled || !g_lab.frameExecuting)
		return;
	g_lab.frameExecuting = false;
	g_lab.warmupExecuting = false;
}

void OpenWydLabOnBeforePresent(IDirect3DDevice9* device)
{
	if (!g_lab.enabled ||
		!g_lab.captureRequested ||
		g_lab.currentFrame != g_lab.targetFrame ||
		g_lab.captureWritten)
	{
		return;
	}
#if !defined(__EMSCRIPTEN__)
	if (!device || g_lab.capturePath.empty())
	{
		SetStatus("native-capture-missing-device", -4);
		return;
	}
	std::string failedStage;
	const HRESULT saved =
		CaptureBackbuffer(device, g_lab.capturePath, failedStage);
	if (FAILED(saved))
	{
		char detail[160];
		std::snprintf(
			detail,
			sizeof(detail),
			"native-capture-failed-%s-0x%08lx",
			failedStage.c_str(),
			static_cast<unsigned long>(saved));
		SetStatus(detail, -6);
		WriteNativeResponse(g_lab.status.c_str());
		return;
	}
#endif
	g_lab.captureWritten = true;
}

void OpenWydLabOnAfterPresent(long presentResult)
{
	if (!g_lab.enabled ||
		g_lab.currentFrame != g_lab.targetFrame ||
		!g_lab.captureWritten)
	{
		return;
	}
	g_lab.captureRequested = false;
	SetStatus(presentResult >= 0 ? "complete" : "present-failed", presentResult >= 0 ? 1 : -7);
#if !defined(__EMSCRIPTEN__)
	WriteNativeResponse(g_lab.status.c_str());
#endif
}

bool OpenWydLabIsIsolated()
{
	return g_lab.enabled &&
		g_lab.scenarioLoaded &&
		static_cast<ScenarioKind>(g_lab.header.kind) ==
			ScenarioKind::Isolated;
}

unsigned int OpenWydLabClearColor()
{
	return g_lab.header.clearColor;
}

void OpenWydLabRenderSubtree(TreeNode* root)
{
	if (!root)
		return;
	TreeNode* current = root;
	do
	{
		if (!current->m_cDeleted)
		{
			const int descend = current->Render();
			if (descend != 0 && current->m_pDown)
			{
				current = current->m_pDown;
				continue;
			}
		}
		while (current != root)
		{
			if (current->m_pNextLink)
			{
				current = current->m_pNextLink;
				break;
			}
			current = current->m_pTop;
		}
	} while (current != root);
}

#if !defined(__EMSCRIPTEN__)
unsigned long OpenWydLabTimeGetTime()
{
	return g_lab.enabled && g_lab.scenarioLoaded ?
		g_lab.clockMs :
		::timeGetTime();
}

unsigned long OpenWydLabGetTickCount()
{
	return g_lab.enabled && g_lab.scenarioLoaded ?
		g_lab.clockMs :
		::GetTickCount();
}
#endif

extern "C" int wyd_lab_load_scenario(const void* bytes, unsigned int size)
{
	return ParseScenario(bytes, size) ? 1 : 0;
}

extern "C" int wyd_lab_show(unsigned int frame)
{
	if (!g_lab.scenarioLoaded)
		return 0;
	g_lab.targetFrame = frame;
	g_lab.resetRequested = true;
	g_lab.captureRequested = true;
	g_lab.captureWritten = false;
	SetStatus("pending", 1);
	return 1;
}

extern "C" int wyd_lab_is_enabled()
{
	return g_lab.enabled ? 1 : 0;
}

extern "C" int wyd_lab_is_pending()
{
	return g_lab.captureRequested ? 1 : 0;
}

extern "C" int wyd_lab_last_result()
{
	return g_lab.lastResult;
}

extern "C" unsigned int wyd_lab_current_frame()
{
	return g_lab.currentFrame;
}

extern "C" unsigned int wyd_lab_clock_ms()
{
	return g_lab.clockMs;
}

extern "C" unsigned int wyd_lab_packet_hash()
{
	return g_lab.packetHash;
}

extern "C" unsigned int wyd_lab_scenario_hash()
{
	return g_lab.scenarioHash;
}

extern "C" int wyd_lab_scene_type()
{
	return g_pCurrentScene ?
		static_cast<int>(g_pCurrentScene->GetSceneType()) :
		-1;
}

extern "C" unsigned int wyd_lab_screen_width()
{
	return g_pDevice ? g_pDevice->m_dwScreenWidth : 0;
}

extern "C" unsigned int wyd_lab_screen_height()
{
	return g_pDevice ? g_pDevice->m_dwScreenHeight : 0;
}

namespace
{
	TMHuman* LabPlayer()
	{
		return g_pCurrentScene ? g_pCurrentScene->m_pMyHuman : nullptr;
	}

	TMCamera* LabCamera()
	{
		return g_pObjectManager ? g_pObjectManager->m_pCamera : nullptr;
	}
}

extern "C" float wyd_lab_player_x()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_vecPosition.x : 0.0f;
}

extern "C" float wyd_lab_player_y()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_vecPosition.y : 0.0f;
}

extern "C" float wyd_lab_player_height()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_fHeight : 0.0f;
}

extern "C" int wyd_lab_player_visible()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_bVisible : 0;
}

extern "C" int wyd_lab_player_hidden()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_cHide : 0;
}

extern "C" int wyd_lab_player_has_skin()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ? 1 : 0;
}

extern "C" int wyd_lab_player_familiar_item()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_sFamiliar : 0;
}

extern "C" int wyd_lab_player_has_familiar()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pFamiliar ? 1 : 0;
}

extern "C" int wyd_lab_player_familiar_visible()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pFamiliar ? player->m_pFamiliar->m_bVisible : 0;
}

extern "C" int wyd_lab_player_familiar_has_skin()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pFamiliar && player->m_pFamiliar->m_pSkinMesh ? 1 : 0;
}

extern "C" int wyd_lab_player_familiar_visibility_reason()
{
	TMHuman* player = LabPlayer();
	if (!player || !player->m_pFamiliar || !g_pMeshManager || !g_pObjectManager)
		return 0;

	TMEffectSkinMesh* familiar = player->m_pFamiliar;
	TMMesh* mesh = g_pMeshManager->GetCommonMesh(familiar->m_dwObjType, 0, 3_min);
	if (!mesh)
		return -1;

	TMCamera* camera = g_pObjectManager->m_pCamera;
	if (!camera)
		return -4;

	D3DXVECTOR3 cameraPosition(camera->m_cameraPos.x, camera->m_cameraPos.z, camera->m_cameraPos.y);
	D3DXVECTOR3 objectPosition(familiar->m_vecPosition.x, familiar->m_vecPosition.y, familiar->m_fHeight);
	D3DXVECTOR3 toObject = objectPosition - cameraPosition;
	if (mesh->m_fRadius >= D3DXVec3Length(&toObject))
		return 2;

	D3DXVECTOR3 cameraDirection(camera->m_vecCamDir.x, camera->m_vecCamDir.z, camera->m_vecCamDir.y);
	if (D3DXVec3Dot(&toObject, &cameraDirection) <= 0.0f)
		return -2;

	return familiar->IsInView() ? 3 : -3;
}

extern "C" int wyd_lab_player_class()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_nClass : -1;
}

extern "C" int wyd_lab_player_motion()
{
	TMHuman* player = LabPlayer();
	return player ? static_cast<int>(player->m_eMotion) : -1;
}

extern "C" int wyd_lab_player_skin_type()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_nSkinMeshType : -1;
}

extern "C" float wyd_lab_player_speed()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_fMaxSpeed : 0.0f;
}

extern "C" float wyd_lab_player_progress()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_fProgressRate : 0.0f;
}

extern "C" int wyd_lab_player_moving()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_bMoveing : 0;
}

extern "C" int wyd_lab_player_last_route()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_nLastRouteIndex : -1;
}

extern "C" int wyd_lab_player_max_route()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_nMaxRouteIndex : -1;
}

extern "C" unsigned int wyd_lab_player_move_started_ms()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_dwStartMoveTime : 0;
}

extern "C" unsigned int wyd_lab_player_animation_started_ms()
{
	TMHuman* player = LabPlayer();
	return player ? player->m_dwStartAnimationTime : 0;
}

extern "C" int wyd_lab_player_animation_index()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ?
		player->m_pSkinMesh->m_nAniIndex :
		-1;
}

extern "C" int wyd_lab_player_animation_last_index()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ?
		player->m_pSkinMesh->m_nAniIndexLast :
		-1;
}

extern "C" unsigned int wyd_lab_player_skin_fps()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ?
		player->m_pSkinMesh->m_dwFPS :
		0;
}

extern "C" unsigned int wyd_lab_player_skin_offset()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ?
		player->m_pSkinMesh->m_dwOffset :
		0;
}

extern "C" unsigned int wyd_lab_player_skin_start_offset()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ?
		player->m_pSkinMesh->m_dwStartOffset :
		0;
}

extern "C" int wyd_lab_player_skin_tick_last()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ?
		player->m_pSkinMesh->m_dwTickLast :
		-1;
}

extern "C" int wyd_lab_player_skin_animation_base()
{
	TMHuman* player = LabPlayer();
	return player && player->m_pSkinMesh ?
		player->m_pSkinMesh->m_nAniBaseIndex :
		-1;
}

extern "C" unsigned int wyd_lab_player_pose_hash()
{
	TMHuman* player = LabPlayer();
	if (!player || !player->m_pSkinMesh)
		return 0;

	// Quantize the final per-bone matrices so harmless libm rounding does not
	// hide a real native/WASM pose difference. This hashes the data that is fed
	// to skinning after the official animation and transition code has run.
	std::uint32_t hash = kFnvOffset;
	const int animation = player->m_pSkinMesh->m_nBoneAniIndex;
	if (animation < 0 || animation >= MAX_BONE_ANIMATION_LIST)
		return 0;
	const std::size_t boneCount = std::min<std::size_t>(
		MeshManager::m_BoneAnimationList[animation].numAniFrame,
		MAX_FRAME_TO_ANIMATE);
	for (std::size_t frame = 0; frame < boneCount; ++frame)
	{
		// Character ANI files deliberately fill non-animated attachment bones
		// with the 0xCD debug sentinel. Native D3DX and libc handle arithmetic
		// on those huge values differently, but the bones have no animated
		// geometry. Exclude the sentinels and compare only matrices that can
		// actually influence the rendered skin.
		const D3DXMATRIX* source =
			MeshManager::m_BoneAnimationList[animation].matAnimation +
			frame + boneCount * player->m_pSkinMesh->m_nAniBaseIndex;
		const float* sourceValues = reinterpret_cast<const float*>(source);
		bool animated = true;
		for (std::size_t value = 0; value < 16; ++value)
		{
			if (!std::isfinite(sourceValues[value]) ||
				std::fabs(sourceValues[value]) > 1000.0f)
			{
				animated = false;
				break;
			}
		}
		if (!animated)
			continue;

		CFrame* bone = player->m_pSkinMesh->m_pframeToAnimate[frame];
		if (!bone)
			continue;

		const std::uint32_t frameIndex = static_cast<std::uint32_t>(frame);
		hash = Fnv1a(&frameIndex, sizeof(frameIndex), hash);
		const float* matrix = reinterpret_cast<const float*>(&bone->m_matRot);
		for (std::size_t value = 0; value < 16; ++value)
		{
			const std::int32_t quantized = static_cast<std::int32_t>(
				std::lround(static_cast<double>(matrix[value]) * 1000.0));
			hash = Fnv1a(&quantized, sizeof(quantized), hash);
		}
	}
	return hash;
}

extern "C" float wyd_lab_render_fps()
{
	return g_pDevice ? g_pDevice->m_fFPS : 0.0f;
}

extern "C" float wyd_lab_camera_x()
{
	TMCamera* camera = LabCamera();
	return camera ? camera->m_cameraPos.x : 0.0f;
}

extern "C" float wyd_lab_camera_y()
{
	TMCamera* camera = LabCamera();
	return camera ? camera->m_cameraPos.y : 0.0f;
}

extern "C" float wyd_lab_camera_z()
{
	TMCamera* camera = LabCamera();
	return camera ? camera->m_cameraPos.z : 0.0f;
}

extern "C" float wyd_lab_camera_horizon()
{
	TMCamera* camera = LabCamera();
	return camera ? camera->m_fHorizonAngle : 0.0f;
}

extern "C" float wyd_lab_camera_vertical()
{
	TMCamera* camera = LabCamera();
	return camera ? camera->m_fVerticalAngle : 0.0f;
}

extern "C" float wyd_lab_camera_length()
{
	TMCamera* camera = LabCamera();
	return camera ? camera->m_fSightLength : 0.0f;
}

extern "C" float wyd_lab_camera_height()
{
	TMCamera* camera = LabCamera();
	return camera ? camera->m_fCamHeight : 0.0f;
}

extern "C" const char* wyd_lab_status()
{
	return g_lab.status.c_str();
}

#endif
