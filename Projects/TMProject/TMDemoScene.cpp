#include "pch.h"
#include "TMDemoScene.h"
#include "TMGlobal.h"
#include "ObjectManager.h"
#include "TMHuman.h"
#include "TMGround.h"
#include "TMSky.h"
#include "TMObjectContainer.h"
#include "TMCamera.h"
#include "Basedef.h"
#include "TMLog.h"

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
}

int TMDemoScene::InitializeScene()
{
	LoadRC("UI\\DemoScene.txt");

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
		pCamera->m_fHorizonAngle = 1.5707964f;
		pCamera->m_fBackHorizonAngle = pCamera->m_fHorizonAngle;
		pCamera->m_fVerticalAngle = -0.087266468f;
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
		m_pGround->SetMiniMapData();

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

	m_pSky = new TMSky();
	AddChild(static_cast<TreeNode*>(m_pSky));
	if (m_pSky)
		m_pSky->SetWeatherState(3);

	m_pItemContainer = new TreeNode(0);
	AddChild(m_pItemContainer);

	ResetDemoPlayer();
	CamAction();
	m_dwStartTime = g_pTimerManager ? g_pTimerManager->GetServerTime() : 0;
	return 1;
}

int TMDemoScene::OnControlEvent(unsigned int idwControlID, unsigned int idwEvent)
{
	return 0;
}

int TMDemoScene::OnCharEvent(char iCharCode, int lParam)
{
	return 0;
}

int TMDemoScene::OnPacketEvent(unsigned int dwCode, char* buf)
{
	return 0;
}

int TMDemoScene::OnMouseEvent(unsigned int dwFlags, unsigned int wParam, int nX, int nY)
{
	return 0;
}

int TMDemoScene::FrameMove(unsigned int dwServerTime)
{
	TMScene::FrameMove(dwServerTime);
	return 1;
}

void TMDemoScene::ResetDemoPlayer()
{
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
	if (!fp)
		return;

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
	m_nCameraLoop = 1;
	memset(m_stCameraTick, 0, sizeof(m_stCameraTick));

	m_stCameraTick[0].dwTick = 0;
	m_stCameraTick[0].sLocal = 0;
	m_stCameraTick[0].fX = 2364.0f;
	m_stCameraTick[0].fY = 2.0f;
	m_stCameraTick[0].fZ = 2268.0f;
	m_stCameraTick[0].fHorizonAngle = 1.5707964f;
	m_stCameraTick[0].fVerticalAngle = -0.087266468f;
	m_sPlayDemo = 1;
}

void TMDemoScene::ReadStrings()
{
}
