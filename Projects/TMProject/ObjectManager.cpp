#include "pch.h"
#include "ObjectManager.h"
#include "TMGlobal.h"
#include "TMCamera.h"
#include "TMFieldScene.h"
#include "TMSelectCharScene.h"
#include "TMSelectServerScene.h"
#include "TMScene.h"
#include "GeomObject.h"
#include "SControlContainer.h"
#include "SControl.h"
#include "TMObject.h"
#include "TMEffectBillBoard2.h"
#include "TMDemoScene.h"
#include "TMMesh.h"
#include "TMItem.h"
#include "TMGround.h"

#if defined(__EMSCRIPTEN__)
#include "RenderDevice.h"
#include "TMRain.h"
#include "TMSnow.h"
#include <emscripten/console.h>
#include <cstring>
#include <cstdio>
#endif

#if defined(__EMSCRIPTEN__)
namespace {
bool g_wasmStateIsPlaceholder = false;
ESCENE_TYPE g_wasmStateSceneType = ESCENE_TYPE::ESCENE_NONE;
char g_wasmStateDebugLabel[128] = "None";
int g_wasmFieldMode = 1;
bool g_wasmFieldDebugFixtureUsed = false;

void WasmStateLog(const char* msg)
{
	emscripten_console_log(msg ? msg : "(null)");
}

void WasmStateLogValue(const char* prefix, int value)
{
	char buf[128]{};
	std::snprintf(buf, sizeof(buf), "%s%d", prefix ? prefix : "", value);
	emscripten_console_log(buf);
}

const char* WasmStateName(ObjectManager::TM_GAME_STATE state)
{
	switch (state)
	{
	case ObjectManager::TM_GAME_STATE::TM_NONE_STATE:
		return "None";
	case ObjectManager::TM_GAME_STATE::TM_FIELD_STATE:
		return "Field";
	case ObjectManager::TM_GAME_STATE::TM_TEST2_STATE:
		return "Test2";
	case ObjectManager::TM_GAME_STATE::TM_SEA_STATE:
		return "Sea";
	case ObjectManager::TM_GAME_STATE::TM_LOGIN_STATE:
		return "Login";
	case ObjectManager::TM_GAME_STATE::TM_CREATEID_STATE:
		return "Create ID";
	case ObjectManager::TM_GAME_STATE::TM_SELECTCHAR_STATE:
		return "Select Character";
	case ObjectManager::TM_GAME_STATE::TM_CREATECHAR_STATE:
		return "Create Character";
	case ObjectManager::TM_GAME_STATE::TM_SELECTSERVER_STATE:
		return "Select Server";
	case ObjectManager::TM_GAME_STATE::TM_DEMO_STATE:
		return "Demo";
	case ObjectManager::TM_GAME_STATE::TM_FIELD2_STATE:
		return "Field2";
	default:
		return "Unknown";
	}
}

ESCENE_TYPE WasmSceneTypeForState(ObjectManager::TM_GAME_STATE state)
{
	switch (state)
	{
	case ObjectManager::TM_GAME_STATE::TM_FIELD_STATE:
		return g_wasmFieldMode == 0 ? ESCENE_TYPE::ESCENE_NONE : ESCENE_TYPE::ESCENE_FIELD;
	case ObjectManager::TM_GAME_STATE::TM_SELECTCHAR_STATE:
	case ObjectManager::TM_GAME_STATE::TM_CREATECHAR_STATE:
		return ESCENE_TYPE::ESCENE_SELCHAR;
	case ObjectManager::TM_GAME_STATE::TM_LOGIN_STATE:
		return ESCENE_TYPE::ESCENE_LOGIN;
	case ObjectManager::TM_GAME_STATE::TM_CREATEID_STATE:
		return ESCENE_TYPE::ESCENE_CREATE_ACCOUNT;
	case ObjectManager::TM_GAME_STATE::TM_SELECTSERVER_STATE:
		return ESCENE_TYPE::ESCENE_SELECT_SERVER;
	case ObjectManager::TM_GAME_STATE::TM_DEMO_STATE:
		return ESCENE_TYPE::ESCENE_DEMO;
	default:
		return ESCENE_TYPE::ESCENE_NONE;
	}
}

void WasmRecordStateDebug(ObjectManager::TM_GAME_STATE state, ESCENE_TYPE sceneType, bool placeholder)
{
	g_wasmStateIsPlaceholder = placeholder;
	g_wasmStateSceneType = sceneType;
	std::snprintf(
		g_wasmStateDebugLabel,
		sizeof(g_wasmStateDebugLabel),
		"%s%s",
		placeholder ? "DEBUG PLACEHOLDER - " : "",
		WasmStateName(state));
}

class TMWasmDebugStateScene final : public TMScene
{
public:
	TMWasmDebugStateScene(ObjectManager::TM_GAME_STATE state, ESCENE_TYPE sceneType)
		: TMScene()
		, m_eDebugState(state)
		, m_eDebugSceneType(sceneType)
		, m_pTitleText(nullptr)
		, m_pDetailText(nullptr)
	{
		m_eSceneType = sceneType;
		m_dwID = static_cast<unsigned int>(sceneType);
		std::snprintf(m_szTitle, sizeof(m_szTitle), "DEBUG PLACEHOLDER - %s", WasmStateName(state));
		std::snprintf(
			m_szDetail,
			sizeof(m_szDetail),
			"state=%d sceneType=%d",
			static_cast<int>(state),
			static_cast<int>(sceneType));
	}

	int InitializeScene() override
	{
		m_eSceneType = m_eDebugSceneType;
		m_dwID = static_cast<unsigned int>(m_eDebugSceneType);

		if (m_pControlContainer)
		{
			m_pTitleText = new SText(
				-2,
				m_szTitle,
				0xFFFFFFFF,
				140.0f,
				260.0f,
				520.0f,
				24.0f,
				0,
				0,
				SText::TEXT_TYPE_SHADOW,
				SText::TEXT_ALIGN_CENTER);
			m_pDetailText = new SText(
				-2,
				m_szDetail,
				0xFFBBDDEE,
				140.0f,
				288.0f,
				520.0f,
				18.0f,
				0,
				0,
				SText::TEXT_TYPE_SHADOW,
				SText::TEXT_ALIGN_CENTER);
			m_pControlContainer->AddItem(static_cast<SControl*>(m_pTitleText));
			m_pControlContainer->AddItem(static_cast<SControl*>(m_pDetailText));
		}

		return 1;
	}

	int OnControlEvent(unsigned int idwControlID, unsigned int idwEvent) override
	{
		return 0;
	}

	int Render() override
	{
		if (!g_pDevice)
			return 1;

		g_pDevice->SetMatrixForUI();
		g_pDevice->SetRenderStateBlock(0);
		g_pDevice->RenderRectNoTex(
			0.0f,
			0.0f,
			static_cast<float>(g_pDevice->m_dwScreenWidth),
			static_cast<float>(g_pDevice->m_dwScreenHeight),
			0xFF101820,
			0);

		const float panelW = 560.0f * RenderDevice::m_fWidthRatio;
		const float panelH = 170.0f * RenderDevice::m_fHeightRatio;
		const float panelX = (static_cast<float>(g_pDevice->m_dwScreenWidth) - panelW) * 0.5f;
		const float panelY = (static_cast<float>(g_pDevice->m_dwScreenHeight) - panelH) * 0.5f;
		g_pDevice->RenderRectNoTex(panelX, panelY, panelW, panelH, 0xFF243447, 0);
		g_pDevice->RenderRectNoTex(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, 2.0f, 0xFF6FA8CC, 0);
		return 1;
	}

private:
	ObjectManager::TM_GAME_STATE m_eDebugState;
	ESCENE_TYPE m_eDebugSceneType;
	SText* m_pTitleText;
	SText* m_pDetailText;
	char m_szTitle[96];
	char m_szDetail[96];
};

TMScene* WasmCreateDebugScene(ObjectManager::TM_GAME_STATE state)
{
	return new TMWasmDebugStateScene(state, WasmSceneTypeForState(state));
}

void WasmEnsureFieldDebugMobData(ObjectManager* objectManager)
{
	g_wasmFieldDebugFixtureUsed = false;

	if (!objectManager)
		return;

	const bool missingMob = objectManager->m_stMobData.MobName[0] == 0;
	const bool missingMap = objectManager->m_stMobData.HomeTownX == 0 || objectManager->m_stMobData.HomeTownY == 0;
	const bool missingSlot = objectManager->m_cCharacterSlot < 0 || objectManager->m_cCharacterSlot >= 4;

	if (!missingMob && !missingMap && !missingSlot)
		return;

	g_wasmFieldDebugFixtureUsed = true;

	if (missingMob)
	{
		std::memset(&objectManager->m_stMobData, 0, sizeof(objectManager->m_stMobData));
		std::memset(&objectManager->m_stSelCharData, 0, sizeof(objectManager->m_stSelCharData));
		std::memset(objectManager->m_stItemCargo, 0, sizeof(objectManager->m_stItemCargo));

		std::snprintf(objectManager->m_stMobData.MobName, sizeof(objectManager->m_stMobData.MobName), "OpenWYD");
		objectManager->m_stMobData.Class = 0;
		objectManager->m_stMobData.Coin = 1000;
		objectManager->m_stMobData.Exp = 0;

		objectManager->m_stMobData.CurrentScore.Level = 0;
		objectManager->m_stMobData.CurrentScore.Ac = 12;
		objectManager->m_stMobData.CurrentScore.Damage = 18;
		objectManager->m_stMobData.CurrentScore.AttackRun = 3;
		objectManager->m_stMobData.CurrentScore.MaxHp = 320;
		objectManager->m_stMobData.CurrentScore.Hp = 320;
		objectManager->m_stMobData.CurrentScore.MaxMp = 140;
		objectManager->m_stMobData.CurrentScore.Mp = 140;
		objectManager->m_stMobData.CurrentScore.Str = 12;
		objectManager->m_stMobData.CurrentScore.Int = 10;
		objectManager->m_stMobData.CurrentScore.Dex = 12;
		objectManager->m_stMobData.CurrentScore.Con = 10;
		objectManager->m_stMobData.BaseScore = objectManager->m_stMobData.CurrentScore;

		objectManager->m_stMobData.Equip[0].sIndex = 6;
		objectManager->m_stMobData.Equip[1].sIndex = 1417;
		objectManager->m_stMobData.Equip[2].sIndex = 1230;
		objectManager->m_stMobData.Equip[3].sIndex = 1230;
		objectManager->m_stMobData.Equip[4].sIndex = 1230;
		objectManager->m_stMobData.Equip[5].sIndex = 1230;
		objectManager->m_stMobData.Equip[6].sIndex = 3605;
		objectManager->m_stMobData.Equip[7].sIndex = 0;

		std::memset(objectManager->m_stMobData.ShortSkill, -1, sizeof(objectManager->m_stMobData.ShortSkill));
		for (int i = 0; i < 20; ++i)
			objectManager->m_cShortSkill[i] = -1;
	}

	objectManager->m_cCharacterSlot = 0;
	objectManager->m_dwCharID = objectManager->m_dwCharID ? objectManager->m_dwCharID : 1;
	objectManager->m_nServerGroupIndex = objectManager->m_nServerGroupIndex < 0 ? 0 : objectManager->m_nServerGroupIndex;
	objectManager->m_nServerIndex = objectManager->m_nServerIndex < 0 ? 0 : objectManager->m_nServerIndex;
	objectManager->m_usWarGuild = 0xFFFF;
	objectManager->m_usAllyGuild = 0;

	objectManager->m_stMobData.HomeTownX = 2096;
	objectManager->m_stMobData.HomeTownY = 2092;

	std::snprintf(
		objectManager->m_stSelCharData.MobName[0],
		sizeof(objectManager->m_stSelCharData.MobName[0]),
		"%s",
		objectManager->m_stMobData.MobName);
	objectManager->m_stSelCharData.HomeTownX[0] = objectManager->m_stMobData.HomeTownX;
	objectManager->m_stSelCharData.HomeTownY[0] = objectManager->m_stMobData.HomeTownY;
	objectManager->m_stSelCharData.Score[0] = objectManager->m_stMobData.CurrentScore;
	std::memcpy(
		objectManager->m_stSelCharData.Equip[0],
		objectManager->m_stMobData.Equip,
		sizeof(objectManager->m_stSelCharData.Equip[0]));
	objectManager->m_stSelCharData.Guild[0] = objectManager->m_stMobData.Guild;
	objectManager->m_stSelCharData.Coin[0] = objectManager->m_stMobData.Coin;
	objectManager->m_stSelCharData.Exp[0] = objectManager->m_stMobData.Exp;

	WasmStateLog("[obj:field] using WASM debug login fixture for real TMFieldScene");
}
} // namespace

extern "C" int wyd_get_scene_type()
{
	if (g_pCurrentScene)
		return static_cast<int>(g_pCurrentScene->GetSceneType());
	return static_cast<int>(g_wasmStateSceneType);
}

extern "C" int wyd_state_is_placeholder()
{
	return g_wasmStateIsPlaceholder ? 1 : 0;
}

extern "C" const char* wyd_get_state_debug_label()
{
	return g_wasmStateDebugLabel;
}

extern "C" const char* wyd_get_state_name(int state)
{
	if (state < static_cast<int>(ObjectManager::TM_GAME_STATE::TM_NONE_STATE) ||
		state > static_cast<int>(ObjectManager::TM_GAME_STATE::TM_FIELD2_STATE))
	{
		return "Unknown";
	}
	return WasmStateName(static_cast<ObjectManager::TM_GAME_STATE>(state));
}

extern "C" void wyd_set_field_mode(int mode)
{
	g_wasmFieldMode = mode == 0 ? 0 : 1;
}

extern "C" int wyd_get_field_mode()
{
	return g_wasmFieldMode;
}

extern "C" int wyd_field_debug_fixture_used()
{
	return g_wasmFieldDebugFixtureUsed ? 1 : 0;
}

extern "C" int wyd_field_initialized()
{
	return g_pCurrentScene && g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD ? 1 : 0;
}

extern "C" int wyd_field_has_ground()
{
	return g_pCurrentScene && g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD && g_pCurrentScene->m_pGround ? 1 : 0;
}

extern "C" int wyd_field_has_my_human()
{
	return g_pCurrentScene && g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD && g_pCurrentScene->m_pMyHuman ? 1 : 0;
}

extern "C" int wyd_field_critical_error()
{
	return g_pCurrentScene && g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD ? g_pCurrentScene->m_bCriticalError : 0;
}

extern "C" int wyd_field_map_x()
{
	return g_pObjectManager ? static_cast<int>(g_pObjectManager->m_stMobData.HomeTownX) >> 7 : -1;
}

extern "C" int wyd_field_map_y()
{
	return g_pObjectManager ? static_cast<int>(g_pObjectManager->m_stMobData.HomeTownY) >> 7 : -1;
}

extern "C" float wyd_field_myhuman_x()
{
	return g_pCurrentScene && g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD && g_pCurrentScene->m_pMyHuman
		? g_pCurrentScene->m_pMyHuman->m_vecPosition.x
		: 0.0f;
}

extern "C" float wyd_field_myhuman_y()
{
	return g_pCurrentScene && g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD && g_pCurrentScene->m_pMyHuman
		? g_pCurrentScene->m_pMyHuman->m_vecPosition.y
		: 0.0f;
}

extern "C" int wyd_field_ground_mask_at(int x, int y)
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround)
		return -9999;

	return g_pCurrentScene->GroundGetMask(TMVector2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
}

extern "C" float wyd_field_ground_height_at(int x, int y)
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround)
		return 0.0f;

	return g_pCurrentScene->GroundGetHeight(TMVector2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
}

extern "C" int wyd_field_ground_water_at(int x, int y)
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround)
		return -1;

	float waterHeight = 0.0f;
	return g_pCurrentScene->GroundIsInWater2(TMVector2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f), &waterHeight);
}

extern "C" int wyd_field_weather_active()
{
	return g_pCurrentScene && g_pCurrentScene->GetSceneType() == ESCENE_TYPE::ESCENE_FIELD ? g_nWeather : -1;
}

extern "C" int wyd_field_rain_visible()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto pScene = static_cast<TMFieldScene*>(g_pCurrentScene);
	return pScene->m_pRain && pScene->m_pRain->m_bVisible ? 1 : 0;
}

extern "C" int wyd_field_snow_visible()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto pScene = static_cast<TMFieldScene*>(g_pCurrentScene);
	return pScene->m_pSnow && pScene->m_pSnow->m_bVisible ? 1 : 0;
}

extern "C" int wyd_field_snow2_visible()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto pScene = static_cast<TMFieldScene*>(g_pCurrentScene);
	return pScene->m_pSnow2 && pScene->m_pSnow2->m_bVisible ? 1 : 0;
}

extern "C" float wyd_field_ground_height_under_player()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround || !g_pCurrentScene->m_pMyHuman)
		return -9999.0f;

	return g_pCurrentScene->GroundGetHeight(TMVector2(
		g_pCurrentScene->m_pMyHuman->m_vecPosition.x,
		g_pCurrentScene->m_pMyHuman->m_vecPosition.y));
}

extern "C" float wyd_field_myhuman_height()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pMyHuman)
		return -9999.0f;

	return g_pCurrentScene->m_pMyHuman->m_fHeight;
}

extern "C" float wyd_field_myhuman_want_height()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pMyHuman)
		return -9999.0f;

	return g_pCurrentScene->m_pMyHuman->m_fWantHeight;
}

extern "C" int wyd_field_ground_mask_under_player()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround || !g_pCurrentScene->m_pMyHuman)
		return -9999;

	return g_pCurrentScene->GroundGetMask(TMVector2(
		g_pCurrentScene->m_pMyHuman->m_vecPosition.x,
		g_pCurrentScene->m_pMyHuman->m_vecPosition.y));
}

extern "C" float wyd_field_myhuman_height_delta()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround || !g_pCurrentScene->m_pMyHuman)
		return -9999.0f;

	float groundHeight = g_pCurrentScene->GroundGetHeight(TMVector2(
		g_pCurrentScene->m_pMyHuman->m_vecPosition.x,
		g_pCurrentScene->m_pMyHuman->m_vecPosition.y));

	if (groundHeight <= -9000.0f)
		return -9999.0f;

	return g_pCurrentScene->m_pMyHuman->m_fHeight - groundHeight;
}

extern "C" float wyd_field_ground_normal_under_player_x()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround || !g_pCurrentScene->m_pMyHuman)
		return -9999.0f;

	TMGround* pGround = g_pCurrentScene->m_pGround;
	int nTileX = static_cast<int>((g_pCurrentScene->m_pMyHuman->m_vecPosition.x - pGround->m_vecOffset.x) / 2.0f);
	int nTileY = static_cast<int>((g_pCurrentScene->m_pMyHuman->m_vecPosition.y - pGround->m_vecOffset.y) / 2.0f);

	if (nTileX < 0 || nTileX > 64 || nTileY < 0 || nTileY > 64)
		return -9999.0f;

	TMVector3 normal = pGround->GetNormalInGround(nTileX, nTileY);
	return normal.x;
}

extern "C" float wyd_field_ground_normal_under_player_y()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround || !g_pCurrentScene->m_pMyHuman)
		return -9999.0f;

	TMGround* pGround = g_pCurrentScene->m_pGround;
	int nTileX = static_cast<int>((g_pCurrentScene->m_pMyHuman->m_vecPosition.x - pGround->m_vecOffset.x) / 2.0f);
	int nTileY = static_cast<int>((g_pCurrentScene->m_pMyHuman->m_vecPosition.y - pGround->m_vecOffset.y) / 2.0f);

	if (nTileX < 0 || nTileX > 64 || nTileY < 0 || nTileY > 64)
		return -9999.0f;

	TMVector3 normal = pGround->GetNormalInGround(nTileX, nTileY);
	return normal.y;
}

extern "C" float wyd_field_ground_normal_under_player_z()
{
	if (!g_pCurrentScene || g_pCurrentScene->GetSceneType() != ESCENE_TYPE::ESCENE_FIELD || !g_pCurrentScene->m_pGround || !g_pCurrentScene->m_pMyHuman)
		return -9999.0f;

	TMGround* pGround = g_pCurrentScene->m_pGround;
	int nTileX = static_cast<int>((g_pCurrentScene->m_pMyHuman->m_vecPosition.x - pGround->m_vecOffset.x) / 2.0f);
	int nTileY = static_cast<int>((g_pCurrentScene->m_pMyHuman->m_vecPosition.y - pGround->m_vecOffset.y) / 2.0f);

	if (nTileX < 0 || nTileX > 64 || nTileY < 0 || nTileY > 64)
		return -9999.0f;

	TMVector3 normal = pGround->GetNormalInGround(nTileX, nTileY);
	return normal.z;
}

#endif

ObjectManager::ObjectManager()
{
	m_pRoot = nullptr;
	m_pCamera = nullptr;
	m_pPreviousScene = nullptr;

	g_pApp->SetObjectManager(this);

	InitResourceList();
	InitAniSoundTable();

	m_pTargetObject = nullptr;

	m_pRoot = new TreeNode(0);
	m_pCamera = new TMCamera();

	m_pRoot->AddChild(m_pCamera);
	g_pCurrentScene = nullptr;

	m_eCurrentState = TM_GAME_STATE::TM_NONE_STATE;
	m_nServerGroupIndex = -1;
	m_nServerIndex = -1;
	m_bBilling = 0;
	m_nTax = 0;
	m_nAuto = 0;
	m_nFakeExp = 0;
	m_cCharacterSlot = -1;
	m_cSelectShortSkill = 0;

	memset(&m_stSelCharData, 0, sizeof(m_stSelCharData));
	memset(m_stItemCargo, 0, sizeof(m_stItemCargo));
	memset(&m_stMobData, 0, sizeof(m_stMobData));
	memset(m_stCapsuleInfo, 0, sizeof(m_stCapsuleInfo));
	memset(m_strGuildName, 0, sizeof(m_strGuildName));

	for (int i = 0; i < 20; ++i)
		m_cShortSkill[i] = -1;

	m_bCleanUp = 0;
	m_bVisualControl = 1;
	m_bTvControl = 0;
	for (int ia = 0; ia < 4; ++ia)
		m_cAvatar[ia] = 0;

	for (int ib = 0; ib < 64; ++ib)
	{
		memset(m_stPlayTime[ib].strAccount, 0, sizeof(m_stPlayTime[ib].strAccount));
		m_stPlayTime[ib].nServer = 0;
		m_stPlayTime[ib].nYear = 0;
		m_stPlayTime[ib].nMonth = 0;
		m_stPlayTime[ib].nDay = 0;
		m_stPlayTime[ib].nHour = 0;
		m_stPlayTime[ib].nMinute = 0;
		m_stPlayTime[ib].nPlayTime = 0;
	}

	m_bPlayTime = 0;
}

ObjectManager::~ObjectManager()
{
	SAFE_DELETE(m_pRoot);
	g_pCurrentScene = nullptr;
}

void ObjectManager::Finalize()
{
	;
}

void ObjectManager::OnPacketEvent(unsigned int dwCode, char* buf)
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;
	
	if (g_pCurrentScene != nullptr)
	{
		if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_SELCHAR && buf != nullptr)
		{
			MSG_SendItem* pMsg = (MSG_SendItem*)buf;
			if (pMsg->Header.Type == MSG_SendItem_Opcode)
			{
				if (pMsg->DestType == 2)
					memcpy(&m_stItemCargo[pMsg->DestPos], &pMsg->Item, sizeof(pMsg->Item));

				return;
			}
		}

		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				if (pCurrentNode->OnPacketEvent(dwCode, buf) == 1)
					break;

				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::OnMouseEvent(unsigned int dwFlags, unsigned int wParam, int nX, int nY)
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				if (pCurrentNode->OnMouseEvent(dwFlags, wParam, nX, nY) == 1)
					break;

				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::OnKeyDownEvent(unsigned int nKeyCode)
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				if (pCurrentNode->OnKeyDownEvent(nKeyCode) == 1)
					break;

				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::OnKeyUpEvent(unsigned int nKeyCode)
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				if (pCurrentNode->OnKeyUpEvent(nKeyCode) == 1)
					break;

				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::OnCharEvent(char nCharCode, int lParam)
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				if (pCurrentNode->OnCharEvent(nCharCode, lParam) == 1)
					break;

				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::OnChangeIME()
{
	if (g_pCurrentScene != nullptr)
	{
		if (g_pCurrentScene->OnChangeIME())
		{
			g_pApp->GetEventTranslator()->SetIMEAlphaNumeric();
		}
	}
}

void ObjectManager::OnIMEEvent(char* ipComposeString)
{
	if (g_pCurrentScene != nullptr)
		g_pCurrentScene->OnIMEEvent(ipComposeString);
}

void ObjectManager::OnDataEvent(unsigned int wParam, int lParam)
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				if (pCurrentNode->OnDataEvent(wParam, lParam) == 1)
					break;

				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::FrameMove(unsigned int dwServerTime)
{
	if (g_pCurrentScene != nullptr)
	{
		g_objectnumber = 0;
		TreeNode* pCurrentNode = g_pCurrentScene;
		TreeNode* pRootNode = g_pCurrentScene;

		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				int res = pCurrentNode->FrameMove(dwServerTime);
				++g_objectnumber;

				if (res != 0 && pCurrentNode->m_pDown != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}
			else
				m_bCleanUp = 1;		

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);

		if (g_pDevice->m_bShowEffects)
			EffectFrameMove(g_pCurrentScene->m_pEffectContainer, dwServerTime);

		if (g_nUpdateGuildName >= 0)
			--g_nUpdateGuildName;
	}
}

void ObjectManager::EffectFrameMove(TreeNode* pNode, unsigned int dwServerTime)
{
	g_effectnumber = 0;
	g_totaleffect = 0;
	TreeNode* pCurrentNode = pNode;

	if (pNode != nullptr)
	{
		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				++g_effectnumber;
				++g_totaleffect;

				if (pCurrentNode->m_pDown != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}
			else
				++g_totaleffect;
			
			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pNode && pCurrentNode != nullptr);
	}
}

TMScene* ObjectManager::GetNodeByID(unsigned int dwID)
{
	if (dwID == 0)
		return 0;

	TMScene* pCurrentNode = g_pCurrentScene;

	if (pCurrentNode == nullptr)
		return nullptr;

	do
	{
		if (!pCurrentNode->m_cDeleted)
		{
			if (pCurrentNode->m_dwID == dwID)
				return pCurrentNode;

			if (pCurrentNode->m_pDown != nullptr)
			{
				pCurrentNode = static_cast<TMScene*>(pCurrentNode->m_pDown);
				continue;
			}
		}

		do
		{
			if (pCurrentNode->m_pNextLink != nullptr)
			{
				pCurrentNode = static_cast<TMScene*>(pCurrentNode->m_pNextLink);
				break;
			}

			pCurrentNode = static_cast<TMScene*>(pCurrentNode->m_pTop);
		} while (pCurrentNode != g_pCurrentScene && pCurrentNode != nullptr);
	} while (pCurrentNode != g_pCurrentScene && pCurrentNode != nullptr);

	return nullptr;
}

TMHuman* ObjectManager::GetHumanByID(unsigned int dwID)
{
	if (dwID == 0)
		return 0;

	TreeNode* pCurrentNode = g_pCurrentScene->m_pHumanContainer;
	TreeNode* pRootNode = pCurrentNode;

	if (pCurrentNode == nullptr)
		return 0;

	do
	{
		if (!pCurrentNode->m_cDeleted)
		{
			if (pCurrentNode->m_dwID == dwID)
				return static_cast<TMHuman*>(pCurrentNode);

			if (pCurrentNode->m_pDown != nullptr)
			{
				pCurrentNode = pCurrentNode->m_pDown;
				continue;
			}
		}

		do
		{
			if (pCurrentNode->m_pNextLink != nullptr)
			{
				pCurrentNode = pCurrentNode->m_pNextLink;
				break;
			}

			pCurrentNode = pCurrentNode->m_pTop;
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);

	return nullptr;
}

TMItem* ObjectManager::GetItemByID(unsigned int dwID)
{
	if (dwID == 0)
		return 0;

	TreeNode* pCurrentNode = g_pCurrentScene->m_pItemContainer;
	TreeNode* pRootNode = pCurrentNode;

	if (pCurrentNode == nullptr)
		return 0;

	do
	{
		if (!pCurrentNode->m_cDeleted)
		{
			if (pCurrentNode->m_dwID == dwID)
				return static_cast<TMItem*>(pCurrentNode);

			if (pCurrentNode->m_pDown != nullptr)
			{
				pCurrentNode = pCurrentNode->m_pDown;
				continue;
			}
		}

		do
		{
			if (pCurrentNode->m_pNextLink != nullptr)
			{
				pCurrentNode = pCurrentNode->m_pNextLink;
				break;
			}

			pCurrentNode = pCurrentNode->m_pTop;
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);

	return nullptr;
}

void ObjectManager::RestoreDeviceObjects()
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			pCurrentNode->RestoreDeviceObjects();

			if (pCurrentNode->m_pDown)
			{
				pCurrentNode = pCurrentNode->m_pDown;
				continue;
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::InvalidateDeviceObjects()
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = g_pCurrentScene;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			pCurrentNode->InvalidateDeviceObjects();

			if (pCurrentNode->m_pDown)
			{
				pCurrentNode = pCurrentNode->m_pDown;
				continue;
			}

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}
}

void ObjectManager::RenderControl()
{
	if (m_bVisualControl != 0 && g_pCurrentScene != nullptr && g_pCurrentScene->m_pControlContainer != nullptr)
	{
		g_pDevice->SetMatrixForUI();
		g_pDevice->SetTexture(0, nullptr);
		g_pDevice->SetTexture(1, nullptr);
		g_pDevice->SetTexture(2, nullptr);

		stGeomList* pList = g_pCurrentScene->m_pControlContainer->m_pDrawControl;

		g_pDevice->SetRenderState(D3DRENDERSTATETYPE::D3DRS_ZENABLE, 0);
		for (int i = 0; i < MAX_DRAW_CONTROL; ++i)
		{
			if (pList[i].pHeadGeom == nullptr)
				continue;

			GeomControl* pControl = pList[i].pHeadGeom;
			GeomControl* pNext = pControl->m_pNextGeom;

			while (pControl != nullptr)
			{
				g_pDevice->SetRenderState(D3DRS_FOGENABLE, 0);
				g_pDevice->RenderGeomControl(pControl);

				pNext = pControl->m_pNextGeom;
				pControl->m_pNextGeom = nullptr;
				pControl = pNext;
			}

			pList[i].pHeadGeom = nullptr;
			pList[i].pTailGeom = nullptr;
		}
		g_pDevice->SetRenderState(D3DRS_ZENABLE, 1);
	}
}

void ObjectManager::RenderObject()
{
	if (g_pCurrentScene != nullptr)
	{
		TreeNode* pCurrentNode = g_pCurrentScene;
		TreeNode* pRootNode = pCurrentNode;

		do
		{
			if (!pCurrentNode->m_cDeleted)
			{
				int ret = pCurrentNode->Render();

				if (ret != 0 && pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}			

			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
	}	
}

void ObjectManager::RenderTargetObject(float fHeight)
{
	if (m_pTargetObject != nullptr)
	{
		if (SUCCEEDED(g_pDevice->m_pd3dDevice->Clear(0, nullptr, 3, 0, 1.0f, 0)))
		{
			float fAng = 0.0f;
			if (fHeight < 1.0f)
				fAng = 1.0f;

			D3DXVECTOR3 vecLookAt(m_pTargetObject->m_vecPosition.x - 0.1f + (fAng * 0.15f),
				((m_pTargetObject->m_fHeight + fHeight) + fHeight) + fAng,
				m_pTargetObject->m_vecPosition.y);

			D3DXVECTOR3 vecCam(
				cosf((m_pTargetObject->m_fAngle + D3DXToRadian(180)) + (((fAng * D3DXToRadian(180)) * 3.0f) / 4.0f)) * 1.2f,
				0.2f,
				sinf((m_pTargetObject->m_fAngle + D3DXToRadian(180)) + (((fAng * D3DXToRadian(180)) * 3.0f) / 4.0f)) * 1.2f);

			vecCam = vecCam + vecLookAt;

			D3DXMATRIX matViewTemp;
			matViewTemp = g_pDevice->m_matView;

			D3DXMATRIX matView;
			D3DXVECTOR3 upVector(0.0f, 1.0f, 0.0f);
			D3DXMatrixLookAtLH(&g_pDevice->m_matView, &vecCam, &vecLookAt, &upVector);
			g_pDevice->m_pd3dDevice->SetTransform(D3DTS_VIEW, &g_pDevice->m_matView);
			m_pTargetObject->Render();
			g_pDevice->m_matView = matViewTemp;
			g_pDevice->m_pd3dDevice->SetTransform(D3DTS_VIEW, &matViewTemp); // sync bridge state
		}
	}
}

void ObjectManager::SetCurrentState(TM_GAME_STATE ieNewState)
{
#if defined(__EMSCRIPTEN__)
	WasmStateLogValue("[obj:set-state] request=", static_cast<int>(ieNewState));

	if (m_eCurrentState == ieNewState && g_pCurrentScene != nullptr)
		return;

	m_eCurrentState = ieNewState;
#else
	if (ieNewState == TM_GAME_STATE::TM_FIELD2_STATE)
	{
		m_eCurrentState = TM_GAME_STATE::TM_NONE_STATE;
	}
	else
	{
		if (m_eCurrentState == ieNewState)
			return;

		m_eCurrentState = ieNewState;
	}
#endif

	if (m_pCamera)
		m_pCamera->InitCamera();
#if defined(__EMSCRIPTEN__)
	else
		WasmStateLog("[obj:set-state] camera null");
#endif

	TMScene* pScene = nullptr;
#if defined(__EMSCRIPTEN__)
	bool bWasmPlaceholderScene = false;
#endif

	switch (m_eCurrentState)
	{
	case TM_GAME_STATE::TM_FIELD_STATE:
#if defined(__EMSCRIPTEN__)
		if (g_wasmFieldMode == 0)
		{
			WasmStateLog("[obj:set-state] new wasm debug field placeholder scene");
			pScene = WasmCreateDebugScene(m_eCurrentState);
			bWasmPlaceholderScene = true;
		}
		else
		{
			WasmEnsureFieldDebugMobData(this);
			WasmStateLog("[obj:set-state] new real TMFieldScene");
			pScene = new TMFieldScene();
		}
#else
		pScene = new TMFieldScene();
#endif
		break;
	case TM_GAME_STATE::TM_SELECTCHAR_STATE:
#if defined(__EMSCRIPTEN__)
		WasmStateLog("[obj:set-state] new TMSelectCharScene");
#endif
		pScene = new TMSelectCharScene();
		break;
	case TM_GAME_STATE::TM_SELECTSERVER_STATE:
	{
#if defined(__EMSCRIPTEN__)
		WasmStateLog("[obj:set-state] new TMSelectServerScene");
#endif
		pScene = new TMSelectServerScene();
		TMEffectBillBoard2* pEffect2 = new TMEffectBillBoard2(93, 20000, 1000.0f, 1000.0f, 1000.0f, 0.002f, 0);
		if (pEffect2)
		{
			pEffect2->m_efAlphaType = EEFFECT_ALPHATYPE::EF_BRIGHT;
			pEffect2->m_vecPosition = TMVector3(10.0f, 10.0f, 10.0f);
			pEffect2->m_vecPosition.y += 0.1f;
			pScene->m_pEffectContainer->AddChild(pEffect2);
		}
	}
	break;
	case TM_GAME_STATE::TM_DEMO_STATE:
#if defined(__EMSCRIPTEN__)
		WasmStateLog("[obj:set-state] new wasm debug demo scene");
		pScene = WasmCreateDebugScene(m_eCurrentState);
		bWasmPlaceholderScene = true;
#else
		pScene = new TMDemoScene();
#endif
		break;
	default:
#if defined(__EMSCRIPTEN__)
		WasmStateLog("[obj:set-state] new wasm debug placeholder scene");
		pScene = WasmCreateDebugScene(m_eCurrentState);
		bWasmPlaceholderScene = true;
#endif
		break;
	}

	if (pScene != nullptr)
	{
#if defined(__EMSCRIPTEN__)
		WasmStateLog("[obj:set-state] SetCurrentScene");
#endif
		SetCurrentScene(pScene);
		g_pCursor->SetStyle(ECursorStyle::TMC_CURSOR_HAND);

#if defined(__EMSCRIPTEN__)
		WasmStateLog("[obj:set-state] InitializeScene begin");
#endif
		if (!pScene->InitializeScene())
		{
#if defined(__EMSCRIPTEN__)
			WasmStateLog("[obj:set-state] InitializeScene failed");
#endif
			SAFE_DELETE(pScene);
			MessageBox(g_pApp->m_hWnd, "Initialize Scene Fail.", "Error", MB_SYSTEMMODAL);
			PostMessage(g_pApp->m_hWnd, 16, 0, 0);
			return;
		}

#if defined(__EMSCRIPTEN__)
		WasmStateLog("[obj:set-state] InitializeScene ok");
		WasmRecordStateDebug(m_eCurrentState, pScene->GetSceneType(), bWasmPlaceholderScene);
#endif
		m_pRoot->AddChild(pScene);
	}
	else
	{
		MessageBox(g_pApp->m_hWnd, "Create Scene Fail.", "Error", MB_SYSTEMMODAL);
		PostMessage(g_pApp->m_hWnd, 16, 0, 0);
	}
}

void ObjectManager::SetCurrentScene(TMScene* pScene)
{
	m_pPreviousScene = g_pCurrentScene;
	g_pCurrentScene = pScene;

	if (m_pPreviousScene != nullptr)
	{
		m_pPreviousScene->m_cDeleted = 1;
		g_pCurrentScene->m_pMessagePanel->m_pText->SetText(m_pPreviousScene->m_pMessagePanel->m_pText->GetText(), 0);
		g_pCurrentScene->m_pMessagePanel->m_pText2->SetText(m_pPreviousScene->m_pMessagePanel->m_pText2->GetText(), 0);

		g_pCurrentScene->m_pMessagePanel->m_bVisible = m_pPreviousScene->m_pMessagePanel->m_bVisible;
		g_pCurrentScene->m_pMessagePanel->m_dwOldServerTime = m_pPreviousScene->m_pMessagePanel->m_dwOldServerTime;
		g_pCurrentScene->m_pMessagePanel->m_dwLifeTime = m_pPreviousScene->m_pMessagePanel->m_dwLifeTime + 6000;

		DeleteObject(m_pPreviousScene);
	}
}

TMScene* ObjectManager::GetCurrentScene()
{
	return g_pCurrentScene;
}

void ObjectManager::DeleteObject(TreeNode* pNode)
{
	if (pNode == nullptr)
		return;

	pNode->m_cDeleted = 1;
	m_bCleanUp = 1;

	g_pCurrentScene->DeleteOwner((TMObject*)pNode);
}

void ObjectManager::DeleteObject(unsigned int dwID)
{
	TreeNode* pNode = GetNodeByID(dwID);

	if (pNode == nullptr)
		return;

	pNode->m_cDeleted = 1;
	m_bCleanUp = 1;
}

void ObjectManager::DisconnectEffectFromMob(TMHuman* pMob)
{
	if (pMob == nullptr)
		return;

	if (pMob->m_pParentScene->m_eSceneType == ESCENE_TYPE::ESCENE_FIELD)
	{
		TreeNode* pCurrentNode = pMob->m_pParentScene->m_pEffectContainer;
		TreeNode* pRootNode = pCurrentNode;

		if (pCurrentNode != nullptr)
		{
			do
			{
				if (pCurrentNode->m_pOwner == pMob)
					pCurrentNode->m_pOwner = nullptr;

				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}

				do
				{
					if (pCurrentNode->m_pNextLink != nullptr)
					{
						pCurrentNode = pCurrentNode->m_pNextLink;
						break;
					}

					pCurrentNode = pCurrentNode->m_pTop;
				} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		}
	}
}

int ObjectManager::InitResourceList()
{
	memset(m_ResourceList, 0, sizeof(m_ResourceList));
	FILE* fpBin = nullptr;
	fopen_s(&fpBin, "UI\\RC.bin", "rb");

	if (fpBin == nullptr)
		return 1;

	fread(m_ResourceList, sizeof(m_ResourceList), 1, fpBin);
	fclose(fpBin);
	return 0;
}

void ObjectManager::InitAniSoundTable()
{
	FILE* fp = nullptr;
	fopen_s(&fp, "AniSound4.txt", "rt");

	if (fp == nullptr)
		return;
	
	for (int ObjType = 0; ObjType < MAX_ANI_TYPE; ++ObjType)
	{
		char szDummy[128];
		int nObjType;
		fscanf(fp, "%s %d\n", szDummy, &nObjType);

		if (nObjType < 2)
		{
			for (int i = 0; i < MAX_ANI_MOTION; ++i)
			{
				fscanf(
					fp,
					"%s %d %d %d %d %d %d %d %d %d\n",
					szDummy,
					&g_MobAniTableEx[0][nObjType].dwAniTable[i],
					&g_MobAniTableEx[0][nObjType].dwSpeed[i],
					&g_MobAniTableEx[1][nObjType].dwAniTable[i],
					&g_MobAniTableEx[1][nObjType].dwSpeed[i],
					&g_MobAniTableEx[2][nObjType].dwAniTable[i],
					&g_MobAniTableEx[2][nObjType].dwSpeed[i],
					&g_MobAniTableEx[3][nObjType].dwAniTable[i],
					&g_MobAniTableEx[3][nObjType].dwSpeed[i],
					&g_MobAniTable[nObjType].dwSoundTable[i]);
				g_MobAniTable[nObjType].dwAniTable[i] = g_MobAniTableEx[0][nObjType].dwAniTable[i];
				g_MobAniTable[nObjType].dwSpeed[i] = g_MobAniTableEx[0][nObjType].dwSpeed[i];
			}
		}
		else
		{
			for (int j = 0; j < MAX_ANI_MOTION; ++j)
			{
				fscanf(
					fp,
					"%s %d %d %d\n",
					szDummy,
					&g_MobAniTable[nObjType].dwAniTable[j],
					&g_MobAniTable[nObjType].dwSpeed[j],
					&g_MobAniTable[nObjType].dwSoundTable[j]);
			}			
		}
	}

	fclose(fp);
}



TMCamera* ObjectManager::GetCamera()
{
	return m_pCamera;
}

void ObjectManager::CleanUp()
{
	TreeNode* pCurrentNode = g_pCurrentScene;
	TreeNode* pRootNode = pCurrentNode;

	if (g_pCurrentScene != nullptr)
	{
		do
		{
			TreeNode* pTopNode = pCurrentNode->m_pTop;

			if (pCurrentNode->m_cDeleted)
			{
				SAFE_DELETE(pCurrentNode);
				pCurrentNode = pTopNode;
				continue;
			}
			else
			{
				if (pCurrentNode->m_pDown)
				{
					pCurrentNode = pCurrentNode->m_pDown;
					continue;
				}
			}
			do
			{
				if (pCurrentNode->m_pNextLink != nullptr)
				{
					pCurrentNode = pCurrentNode->m_pNextLink;
					break;
				}

				pCurrentNode = pCurrentNode->m_pTop;
			} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);
		} while (pCurrentNode != pRootNode && pCurrentNode != nullptr);

		SAFE_DELETE(m_pPreviousScene);
		m_bCleanUp = 0;
	}
}
