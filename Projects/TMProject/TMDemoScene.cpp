#include "pch.h"
#include "TMDemoScene.h"
#include "TMGlobal.h"
#include "ObjectManager.h"
#include "TMHuman.h"
#include "TMGround.h"
#include "TMSky.h"
#include "TMSun.h"
#include "TMObjectContainer.h"
#include "TMCamera.h"
#include "Basedef.h"
#include "TMLog.h"
#include "SControl.h"
#include "SControlContainer.h"
#include "MeshManager.h"
#include "DirShow.h"
#include "NewApp.h"

namespace
{
constexpr unsigned int kDemoCreditsStart = 1000;
constexpr unsigned int kDemoCreditsScrollEnd = 592000;
constexpr unsigned int kDemoEndingMusicStart = 286000;
constexpr unsigned int kDemoExitTime = 623000;
}

TMDemoScene::TMDemoScene()
{
	m_eSceneType = ESCENE_TYPE::ESCENE_DEMO;
	m_dwID = static_cast<unsigned int>(m_eSceneType);
	m_bPlayingBGM = 0;
	m_dwStartTime = 0;
	m_pRain = nullptr;
	m_pSnow = nullptr;
	m_cStartRun = 0;
	m_pCoverPanel = nullptr;
	m_pTextEnd = nullptr;

	for (int i = 0; i < 50; ++i)
		m_pCheckHumanList[i] = nullptr;

	memset(m_stDemoHuman, 0, sizeof(m_stDemoHuman));
	memset(m_stAniList, 0, sizeof(m_stAniList));
	memset(m_cPlayedFlag, 0, sizeof(m_cPlayedFlag));
	memset(m_szEndingString, 0, sizeof(m_szEndingString));
}

TMDemoScene::~TMDemoScene()
{
	if (g_pCursor)
		g_pCursor->SetVisible(1);
}

int TMDemoScene::InitializeScene()
{
	const float fScreenWidth = g_pApp
		? static_cast<float>(g_pApp->m_dwScreenWidth) / RenderDevice::m_fWidthRatio
		: 800.0f;
	const float fScreenHeight = g_pApp
		? static_cast<float>(g_pApp->m_dwScreenHeight) / RenderDevice::m_fHeightRatio
		: 600.0f;

	m_pCoverPanel = new SPanel(
		-2,
		0.0f,
		0.0f,
		fScreenWidth,
		fScreenHeight,
		0,
		RENDERCTRLTYPE::RENDER_IMAGE_STRETCH);
	auto pTopPanel = new SPanel(
		-2,
		0.0f,
		0.0f,
		fScreenWidth,
		fScreenHeight * 0.125f,
		0xFF000000,
		RENDERCTRLTYPE::RENDER_IMAGE_STRETCH);
	auto pBottomPanel = new SPanel(
		-2,
		0.0f,
		499.8f,
		fScreenWidth,
		fScreenHeight,
		0xFF000000,
		RENDERCTRLTYPE::RENDER_IMAGE_STRETCH);

	m_pControlContainer->AddItem(pTopPanel);
	m_pControlContainer->AddItem(pBottomPanel);
	m_pControlContainer->AddItem(m_pCoverPanel);

	ReadStrings();
	m_pCoverPanel->SetVisible(0);

	m_pTextEnd = new SText(
		-2,
		"Press anykey to exit.",
		0xFFFFFFFF,
		0.0f,
		30.0f,
		800.0f,
		20.0f,
		1,
		0,
		SText::TEXT_TYPE_SHADOW,
		SText::TEXT_ALIGN_CENTER);
	pBottomPanel->AddChild(m_pTextEnd);
	m_pTextEnd->SetVisible(0);

	g_pDevice->m_dwClearColor = 0x00000000;
	g_pDevice->m_nHeightShift = 0;
	g_HeightPosX = 2304;
	g_HeightPosY = 2176;

	if (g_pObjectManager && g_pObjectManager->m_pCamera)
	{
		auto pCamera = g_pObjectManager->m_pCamera;
		pCamera->SetViewMode(0);
		pCamera->m_bStandAlone = 1;
		pCamera->m_vecCamPos = TMVector2(2364.0f, 2268.0f);
		pCamera->m_cameraPos = TMVector3(2364.0f, 2.0f, 2268.0f);
		pCamera->m_fHorizonAngle = 0.78539819f;
		pCamera->m_fBackHorizonAngle = pCamera->m_fHorizonAngle;
		pCamera->m_fVerticalAngle = 0.0f;
		pCamera->m_fBackVerticalAngle = pCamera->m_fVerticalAngle;
	}

	char szMapPath[128]{};
	char szDataPath[128]{};
	sprintf_s(szMapPath, "env\\Field1817.trn");
	sprintf_s(szDataPath, "env\\Field1817.dat");

	m_pGroundList[0] = new TMGround();
	if (!m_pGroundList[0]->LoadTileMap(szMapPath))
	{
		if (!m_bCriticalError)
			LogMsgCriticalError(13, 0, 0, 0, 0);
		m_bCriticalError = 1;
	}

	m_pGround = m_pGroundList[0];
	if (m_pGround)
	{
		m_pGround->SetMiniMapData();
		for (int nY = 0; nY < 128; ++nY)
			memcpy(m_HeightMapData[nY], m_pGround->m_pMaskData[nY], 128);

		g_HeightPosX = static_cast<int>(m_pGround->m_vecOffset.x);
		g_HeightPosY = static_cast<int>(m_pGround->m_vecOffset.y);
	}

	m_pObjectContainerList[0] = new TMObjectContainer(m_pGround);
	m_pGroundObjectContainer->AddChild(m_pObjectContainerList[0]);

	if (!m_pObjectContainerList[0] || !m_pObjectContainerList[0]->Load(szDataPath))
	{
		LOG_WRITELOG("DataFile Not Found\r\n");
		MessageBoxA(g_pApp->m_hWnd, "DataFile Not Found.", "File Lost", 0);
		PostMessageA(g_pApp->m_hWnd, 0x10u, 0, 0);
		return 0;
	}

	m_pGroundObjectContainer->AddChild(m_pGroundList[0]);

	m_pSun = new TMSun();
	if (m_pSun)
	{
		m_pSun->InitObject();
		m_pEffectContainer->AddChild(m_pSun);
	}

	m_pSky = new TMSky();
	AddChild(static_cast<TreeNode*>(m_pSky));
	if (m_pSky)
	{
		m_pSky->SetWeatherState(0);
		m_pSky->m_dwChangeTime = 20000;
	}

	m_pItemContainer = new TreeNode(0);
	AddChild(m_pItemContainer);

	ResetDemoPlayer();
	CamAction();
	m_dwStartTime = g_pTimerManager ? g_pTimerManager->GetServerTime() : 0;

	m_bPlayingBGM = 0;
	if (g_pApp && g_pApp->m_pBGMManager)
	{
		g_pApp->m_pBGMManager->StopBGM();
		m_bPlayingBGM = 1;
	}
	return 1;
}

int TMDemoScene::OnControlEvent(unsigned int idwControlID, unsigned int idwEvent)
{
	return 1;
}

int TMDemoScene::OnCharEvent(char iCharCode, int lParam)
{
	if (g_pApp)
		PostMessageA(g_pApp->m_hWnd, WM_CLOSE, 0, 0);
	return TMScene::OnCharEvent(iCharCode, lParam);
}

int TMDemoScene::OnPacketEvent(unsigned int dwCode, char* buf)
{
	return TMScene::OnPacketEvent(dwCode, buf) == 1;
}

int TMDemoScene::OnMouseEvent(unsigned int dwFlags, unsigned int wParam, int nX, int nY)
{
	return TMScene::OnMouseEvent(dwFlags, wParam, nX, nY) == 1;
}

int TMDemoScene::FrameMove(unsigned int dwServerTime)
{
	TMScene::FrameMove(dwServerTime);

	for (int nHuman = 0; nHuman < 50; ++nHuman)
	{
		for (int nAction = 0; nAction < 16; ++nAction)
		{
			auto& stAction = m_stAniList[nHuman][nAction];
			if (dwServerTime <= m_dwStartCamTime + stAction.dwTick ||
				m_cPlayedFlag[nHuman][nAction])
			{
				continue;
			}

			m_cPlayedFlag[nHuman][nAction] = 1;
			auto pHuman = m_pCheckHumanList[nHuman];
			if (stAction.cWhat == 'M' && pHuman)
			{
				pHuman->m_fMaxSpeed = static_cast<float>(stAction.sAni);
				pHuman->GetRoute(stAction.vecPos, 32, 0);
			}
			else if (stAction.cWhat == 'A' && pHuman)
			{
				pHuman->SetAnimation(static_cast<ECHAR_MOTION>(stAction.sAni), 1);
			}

			break;
		}
	}

	const unsigned int dwElapsed = dwServerTime - m_dwStartCamTime;
	if (m_pCoverPanel && dwElapsed < kDemoCreditsScrollEnd)
		m_pCoverPanel->SetPos(0.0f, 760.0f - (static_cast<float>(dwElapsed) / 70.0f));

	if (m_pCoverPanel && dwElapsed > kDemoCreditsStart && !m_pCoverPanel->IsVisible())
		m_pCoverPanel->SetVisible(1);

	if (m_pSky)
	{
		if (dwElapsed > 40000 && dwElapsed <= 80000 && m_pSky->m_nState == 0)
			m_pSky->SetWeatherState(12);
		else if (dwElapsed > 80000 && dwElapsed <= 120000 && m_pSky->m_nState == 2)
			m_pSky->SetWeatherState(13);
		else if (dwElapsed > 120000 && dwElapsed <= 160000 && m_pSky->m_nState == 3)
			m_pSky->SetWeatherState(10);
		else if (dwElapsed > 160000 && dwElapsed <= 200000 && m_pSky->m_nState == 0)
			m_pSky->SetWeatherState(11);
		else if (dwElapsed > 200000 && dwElapsed <= 240000 && m_pSky->m_nState == 1)
			m_pSky->SetWeatherState(10);
		else if (dwElapsed > 240000 && dwElapsed <= 280000 && m_pSky->m_nState == 0)
		{
			m_pSky->SetWeatherState(12);
			if (g_pCursor)
				g_pCursor->SetVisible(0);
		}
	}

	if (dwElapsed > kDemoEndingMusicStart && m_bPlayingBGM)
	{
		if (g_pApp && g_pApp->m_pBGMManager)
		{
			g_pApp->m_pBGMManager->StopBGM();
			g_pApp->m_pBGMManager->PlayMusic(14);
		}

		m_bPlayingBGM = 0;
		if (m_pTextEnd)
			m_pTextEnd->SetVisible(1);
	}

	if (dwElapsed > kDemoExitTime && g_pApp)
		PostMessageA(g_pApp->m_hWnd, WM_CLOSE, 0, 0);

	return 1;
}

void TMDemoScene::ResetDemoPlayer()
{
	if (m_pSky)
		m_pSky->SetWeatherState(0);

	for (int i = 0; i < 50; ++i)
	{
		if (m_pCheckHumanList[i])
		{
			g_pObjectManager->DeleteObject(m_pCheckHumanList[i]);
			m_pCheckHumanList[i] = nullptr;
		}
	}

	memset(m_stDemoHuman, 0, sizeof(m_stDemoHuman));

	FILE* fp = nullptr;
	fopen_s(&fp, "UI\\EndDemo.bin", "rb");
	if (fp)
	{
		for (int i = 0; i < 50; ++i)
		{
			int ret = fread(&m_stDemoHuman[i], 1, sizeof(m_stDemoHuman[i]), fp);
			if (!ret)
				break;

			HUMAN_LOOKINFO stHumanLook{};
			SANC_INFO stSancInfo{};

			const short sFace = static_cast<short>(m_stDemoHuman[i].nFace);
			stHumanLook.FaceMesh = g_pItemList[m_stDemoHuman[i].nFace].nIndexMesh;
			stHumanLook.FaceSkin = g_pItemList[m_stDemoHuman[i].nFace].nIndexTexture;
			stHumanLook.HelmMesh = g_pItemList[m_stDemoHuman[i].Helm].nIndexMesh;
			stHumanLook.HelmSkin = g_pItemList[m_stDemoHuman[i].Helm].nIndexTexture;
			stHumanLook.CoatMesh = g_pItemList[m_stDemoHuman[i].Body].nIndexMesh;
			stHumanLook.CoatSkin = g_pItemList[m_stDemoHuman[i].Body].nIndexTexture;
			stHumanLook.PantsMesh = g_pItemList[m_stDemoHuman[i].Body].nIndexMesh;
			stHumanLook.PantsSkin = g_pItemList[m_stDemoHuman[i].Body].nIndexTexture;
			stHumanLook.GlovesMesh = g_pItemList[m_stDemoHuman[i].Body].nIndexMesh;
			stHumanLook.GlovesSkin = g_pItemList[m_stDemoHuman[i].Body].nIndexTexture;
			stHumanLook.BootsMesh = g_pItemList[m_stDemoHuman[i].Body].nIndexMesh;
			stHumanLook.BootsSkin = g_pItemList[m_stDemoHuman[i].Body].nIndexTexture;
			stHumanLook.RightMesh = g_pItemList[m_stDemoHuman[i].Right].nIndexMesh;
			stHumanLook.RightSkin = g_pItemList[m_stDemoHuman[i].Right].nIndexTexture;
			stHumanLook.LeftMesh = g_pItemList[m_stDemoHuman[i].Left].nIndexMesh;
			stHumanLook.LeftSkin = g_pItemList[m_stDemoHuman[i].Left].nIndexTexture;

			stSancInfo.Sanc7 = static_cast<unsigned char>(m_stDemoHuman[i].nSanc);
			stSancInfo.Sanc6 = stSancInfo.Sanc7;
			stSancInfo.Sanc5 = stSancInfo.Sanc7;
			stSancInfo.Sanc4 = stSancInfo.Sanc7;
			stSancInfo.Sanc3 = stSancInfo.Sanc7;
			stSancInfo.Sanc2 = stSancInfo.Sanc7;
			stSancInfo.Legend7 = static_cast<unsigned char>(g_pItemList[m_stDemoHuman[i].Body].nGrade);
			stSancInfo.Legend6 = stSancInfo.Legend7;
			stSancInfo.Legend5 = stSancInfo.Legend7;
			stSancInfo.Legend4 = stSancInfo.Legend7;
			stSancInfo.Legend3 = stSancInfo.Legend7;
			stSancInfo.Legend2 = stSancInfo.Legend7;

			if (m_stDemoHuman[i].Body == m_stDemoHuman[i].Helm)
			{
				stSancInfo.Sanc1 = stSancInfo.Sanc2;
				stSancInfo.Legend1 = stSancInfo.Legend2;
			}

			if (ret == -1)
				break;

			auto pHuman = new TMHuman(this);
			m_pCheckHumanList[i] = pHuman;
			pHuman->m_stScore.Hp = 1;
			pHuman->m_dwID = 10000;
			sprintf_s(pHuman->m_szName, "person%d", i);

			if ((m_stDemoHuman[i].nMount >= 2360 && m_stDemoHuman[i].nMount < 2390) ||
				(m_stDemoHuman[i].nMount >= 2960 && m_stDemoHuman[i].nMount < 3000))
			{
				pHuman->m_cMount = 1;
				const int sIndex = m_stDemoHuman[i].nMount - 2045;
				STRUCT_ITEM item{};
				item.sIndex = static_cast<short>(sIndex);
				pHuman->m_sMountIndex = static_cast<short>(sIndex - 315);
				const int nClass = BASE_GetItemAbility(&item, 18);
				pHuman->m_nMountSkinMeshType = BASE_DefineSkinMeshType(nClass);
				pHuman->m_stMountLook.Mesh0 = g_pItemList[sIndex].nIndexMesh;
				pHuman->m_stMountLook.Mesh1 = pHuman->m_stMountLook.Mesh0;
				pHuman->m_stMountLook.Skin0 = g_pItemList[sIndex].nIndexTexture;
				pHuman->m_stMountLook.Skin1 = pHuman->m_stMountLook.Skin0;

				if (sIndex >= 321 && sIndex <= 325)
					pHuman->m_stMountLook.Mesh2 = sIndex - 320;
				else if (sIndex >= 326 && sIndex <= 330)
					pHuman->m_stMountLook.Mesh2 = sIndex - 325;
				else
					pHuman->m_stMountLook.Mesh2 = 0;

				pHuman->m_stMountSanc.Sanc0 = 0;
				pHuman->m_stMountSanc.Sanc1 = 0;
				pHuman->m_stMountSanc.Sanc2 = 0;
				pHuman->m_fMountScale = BASE_GetMountScale(
					pHuman->m_nMountSkinMeshType,
					pHuman->m_stMountLook.Mesh0);
			}

			memcpy(&pHuman->m_stLookInfo, &stHumanLook, sizeof(pHuman->m_stLookInfo));
			memcpy(&pHuman->m_stSancInfo, &stSancInfo, sizeof(pHuman->m_stSancInfo));

			if (m_stDemoHuman[i].Mantua > 0)
			{
				pHuman->m_cMantua = 1;
				pHuman->m_wMantuaSkin = g_pItemList[m_stDemoHuman[i].Mantua].nIndexTexture;
				pHuman->m_ucMantuaSanc = static_cast<unsigned char>(m_stDemoHuman[i].nSanc);
				pHuman->m_ucMantuaLegend = static_cast<char>(g_pItemList[m_stDemoHuman[i].Mantua].nGrade);
			}

			pHuman->m_cClone = 1;
			pHuman->SetRace(sFace);
			pHuman->InitObject();
			pHuman->CheckWeapon(m_stDemoHuman[i].Left, m_stDemoHuman[i].Right);
			pHuman->InitAngle(0.0f, (static_cast<float>(m_stDemoHuman[i].nAngle) * 6.2831855f) / 360.0f, 0.0f);
			pHuman->InitPosition(
				static_cast<float>(m_stDemoHuman[i].nX),
				0.0f,
				static_cast<float>(m_stDemoHuman[i].nY));
			pHuman->m_fMaxSpeed = static_cast<float>(m_stDemoHuman[i].nSpeed);
			pHuman->m_bParty = 1;
			m_pHumanContainer->AddChild(pHuman);
		}

		fclose(fp);
	}

	ReadTimeTable();

	for (int i = 0; i < 50; ++i)
	{
		if (m_pCheckHumanList[i])
		{
			m_pCheckHumanList[i]->m_bVisible = 1;
			m_pCheckHumanList[i]->FrameMove(0);
			m_pCheckHumanList[i]->Render();
		}
	}
}

void TMDemoScene::ReadTimeTable()
{
	memset(m_cPlayedFlag, 0, sizeof(m_cPlayedFlag));
	memset(m_stAniList, 0, sizeof(m_stAniList));

	FILE* fp = nullptr;
	fopen_s(&fp, "UI\\TimeTable.bin", "rb");
	if (!fp)
		return;

	fread(m_stAniList, 1, sizeof(m_stAniList), fp);
	fclose(fp);
}

void TMDemoScene::CamAction()
{
	auto pCamera = g_pObjectManager ? g_pObjectManager->GetCamera() : nullptr;
	if (!pCamera)
		return;

	pCamera->m_bStandAlone = 1;
	m_dwStartCamTime = g_pTimerManager ? g_pTimerManager->GetServerTime() : 1;
	if (!m_dwStartCamTime)
		m_dwStartCamTime = 1;

	ReadCameraPos("UI\\EndCamAction");
	m_sPlayDemo = 1;
}

void TMDemoScene::ReadStrings()
{
	memset(m_szEndingString, 0, sizeof(m_szEndingString));

	FILE* fp = nullptr;
	fopen_s(&fp, "UI\\Ending.bin", "rb");
	if (!fp)
		return;

	const size_t nRead = fread(m_szEndingString, 1, sizeof(m_szEndingString), fp);
	fclose(fp);
	if (nRead != sizeof(m_szEndingString))
		return;

	auto pBytes = reinterpret_cast<unsigned char*>(m_szEndingString);
	for (size_t i = 0; i < sizeof(m_szEndingString); ++i)
		pBytes[i] = static_cast<unsigned char>(pBytes[i] - i - ObjectMaskEncKeys[i % 170]);

	if (!m_pCoverPanel)
		return;

	for (int i = 0; i < 500; ++i)
	{
		if (strlen(m_szEndingString[i]) <= 1)
			continue;

		auto pText = new SText(
			-2,
			m_szEndingString[i],
			0xFFFFFFFF,
			0.0f,
			static_cast<float>(i * 20),
			800.0f,
			20.0f,
			1,
			0,
			SText::TEXT_TYPE_SHADOW,
			SText::TEXT_ALIGN_CENTER);
		m_pCoverPanel->AddChild(pText);
	}
}
