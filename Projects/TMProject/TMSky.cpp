#include "pch.h"
#include "TMMesh.h"
#include "TMSky.h"
#include "TMGlobal.h"
#include "TMCamera.h"
#include "MeshManager.h"
#include "TMSun.h"
#include "TMObject.h"
#include "TMObjectContainer.h"
#include "TMLight.h"

// NOTE: this is cleary not float values.
D3DCOLORVALUE TMSky::m_LightVal[4] =
{
  {  0.69999999f,  0.69999999f,  0.69999999f,  0.1f },
  {  0.30000001f,  0.30000001f,  0.30000001f,  1.0f },
  {  0.5f,  0.40000001f,  0.2f,  1.0f },
  {  0.25999999f,  0.34f,  0.34f,  1.0f }
};

float TMSky::FogList[16][2] =
{
  {  14.0,  34.0 },
  {  25.0,  34.0 },
  {  55.0,  70.0 },
  {  25.0,  34.0 },
  {  14.0,  23.0 },
  {  14.0,  22.0 },
  {  16.0,  34.0 },
  {  16.0,  34.0 },
  {  39.0,  70.0 },
  {  19.0,  30.0 },
  {  19.0,  30.0 },
  {  19.0,  70.0 },
  {  29.0,  40.0 },
  {  0.0,  0.0 },
  {  0.0,  0.0 },
  {  0.0,  0.0 }
};

static unsigned int g_wydSkyRenderCalls = 0;
static unsigned int g_wydSkyHiddenReturns = 0;
static unsigned int g_wydSkyEligibleCalls = 0;
static unsigned int g_wydSkyBranchSkipped = 0;
static unsigned int g_wydSkyMeshNull = 0;
static unsigned int g_wydSkyMeshDraws = 0;
static int g_wydSkyLastDungeon = -1;
static int g_wydSkyLastState = -1;
static int g_wydSkyLastTextureIndex = -1;
static int g_wydSkyLastMeshTextureIndex = -1;
static int g_wydSkyLastMeshHasVB = -1;
static int g_wydSkyLastMeshHasIB = -1;
static int g_wydSkyLastMeshFVF = -1;
static int g_wydSkyLastMeshAttCount = -1;
static int g_wydSkyLastMeshFaceCount = -1;
static int g_wydSkyLastMeshVertexCount = -1;
static int g_wydSkyLastMeshRenderResult = -1;

#if defined(__EMSCRIPTEN__)
extern "C" unsigned int wyd_d3d9_get_debug_flags();
#endif

static bool WydSkyDebugSkipMeshRender()
{
#if defined(__EMSCRIPTEN__)
    return (wyd_d3d9_get_debug_flags() & (1u << 18)) != 0;
#else
    return false;
#endif
}

static int WydSkyTextureIndexForState(int nState)
{
    int nWeatherState = nState % 10;
    if (nWeatherState < 0)
        nWeatherState = 0;
    if (nWeatherState > 3)
        nWeatherState %= 4;

    return nWeatherState + 67;
}

static bool WydSkyTextureIndexIsValid(int nTextureIndex)
{
    return nTextureIndex >= 67 && nTextureIndex <= 70;
}

static void WydSkyEnsureMeshTexture(TMMesh* pMesh, int nState)
{
    if (pMesh == nullptr)
        return;

    pMesh->m_bEffect = 1;
    if (!WydSkyTextureIndexIsValid(pMesh->m_nTextureIndex[0]))
        pMesh->m_nTextureIndex[0] = WydSkyTextureIndexForState(nState);
}

static void WydSkyForceTexture(unsigned int dwStage, IDirect3DBaseTexture9* pTexture)
{
    if (g_pDevice == nullptr || dwStage >= 8)
        return;

    g_pDevice->m_pTexture[dwStage] = nullptr;
    g_pDevice->SetTexture(dwStage, pTexture);
}

static void WydSkyForceTextureStageState(
    unsigned int dwStage,
    D3DTEXTURESTAGESTATETYPE eType,
    unsigned int dwValue)
{
    if (g_pDevice == nullptr || dwStage >= 8)
        return;

    int nType = static_cast<int>(eType);
    if (nType < 0 || nType >= 29)
        return;

    g_pDevice->m_dwTextureStageStateList[dwStage][nType] = 0xFFFFFFFFu;
    g_pDevice->SetTextureStageState(dwStage, eType, dwValue);
}

static void WydSkyApplyRenderState(TMMesh* pMesh, int nState, int nTextureIndex)
{
    if (pMesh == nullptr || g_pTextureManager == nullptr)
        return;

    WydSkyForceTexture(0, g_pTextureManager->GetEffectTexture(pMesh->m_nTextureIndex[0], 5000));
    if (g_pDevice->m_bVoodoo == 1)
    {
        WydSkyForceTextureStageState(1u, D3DTSS_TEXCOORDINDEX, 1u);
        WydSkyForceTextureStageState(1u, D3DTSS_COLOROP, D3DTOP_DISABLE);
        return;
    }

    WydSkyForceTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    WydSkyForceTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    WydSkyForceTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    WydSkyForceTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    WydSkyForceTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    WydSkyForceTextureStageState(1u, D3DTSS_TEXCOORDINDEX, 0);
    WydSkyForceTextureStageState(
        1u,
        D3DTSS_COLOROP,
        (nState / 10) ? D3DTOP_BLENDDIFFUSEALPHA : D3DTOP_DISABLE);
    WydSkyForceTexture(1u, g_pTextureManager->GetEffectTexture(nTextureIndex, 5000));
}

static void WydSkyResetRenderState()
{
    WydSkyForceTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    WydSkyForceTextureStageState(1u, D3DTSS_TEXCOORDINDEX, 1u);
    WydSkyForceTextureStageState(1u, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

extern "C" unsigned int wyd_sky_render_calls()
{
    return g_wydSkyRenderCalls;
}

extern "C" unsigned int wyd_sky_hidden_returns()
{
    return g_wydSkyHiddenReturns;
}

extern "C" unsigned int wyd_sky_eligible_calls()
{
    return g_wydSkyEligibleCalls;
}

extern "C" unsigned int wyd_sky_branch_skipped()
{
    return g_wydSkyBranchSkipped;
}

extern "C" unsigned int wyd_sky_mesh_null()
{
    return g_wydSkyMeshNull;
}

extern "C" unsigned int wyd_sky_mesh_draws()
{
    return g_wydSkyMeshDraws;
}

extern "C" int wyd_sky_last_dungeon()
{
    return g_wydSkyLastDungeon;
}

extern "C" int wyd_sky_last_state()
{
    return g_wydSkyLastState;
}

extern "C" int wyd_sky_last_texture_index()
{
    return g_wydSkyLastTextureIndex;
}

extern "C" int wyd_sky_last_mesh_texture_index()
{
    return g_wydSkyLastMeshTextureIndex;
}

extern "C" int wyd_sky_last_mesh_has_vb()
{
    return g_wydSkyLastMeshHasVB;
}

extern "C" int wyd_sky_last_mesh_has_ib()
{
    return g_wydSkyLastMeshHasIB;
}

extern "C" int wyd_sky_last_mesh_fvf()
{
    return g_wydSkyLastMeshFVF;
}

extern "C" int wyd_sky_last_mesh_att_count()
{
    return g_wydSkyLastMeshAttCount;
}

extern "C" int wyd_sky_last_mesh_face_count()
{
    return g_wydSkyLastMeshFaceCount;
}

extern "C" int wyd_sky_last_mesh_vertex_count()
{
    return g_wydSkyLastMeshVertexCount;
}

extern "C" int wyd_sky_last_mesh_render_result()
{
    return g_wydSkyLastMeshRenderResult;
}

extern "C" void wyd_sky_reset_debug_counters()
{
    g_wydSkyRenderCalls = 0;
    g_wydSkyHiddenReturns = 0;
    g_wydSkyEligibleCalls = 0;
    g_wydSkyBranchSkipped = 0;
    g_wydSkyMeshNull = 0;
    g_wydSkyMeshDraws = 0;
    g_wydSkyLastDungeon = -1;
    g_wydSkyLastState = -1;
    g_wydSkyLastTextureIndex = -1;
    g_wydSkyLastMeshTextureIndex = -1;
    g_wydSkyLastMeshHasVB = -1;
    g_wydSkyLastMeshHasIB = -1;
    g_wydSkyLastMeshFVF = -1;
    g_wydSkyLastMeshAttCount = -1;
    g_wydSkyLastMeshFaceCount = -1;
    g_wydSkyLastMeshVertexCount = -1;
    g_wydSkyLastMeshRenderResult = -1;
}

TMSky::TMSky()
{
    n_pMeshMilkyway = 0;
    m_dwObjType = 1;
    m_fHeight = -5.0f;
    m_nTextureIndex = 0;
    m_fScale = 0.5f;
    m_fAngle = D3DXToRadian(180);
    m_dwStartTime = 0;
    m_nState = 0;
    m_dwChangeTime = 2000;

    if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_DEMO)
    {
        g_pDevice->m_fFogStart = 19.0f;
        g_pDevice->m_fFogEnd = 30.0f;
        m_fAngle = D3DXToRadian(90);
    }

    m_dwR[0] = 28;
    m_dwG[0] = 120;
    m_dwB[0] = 189;
    m_dwR[1] = 51;
    m_dwG[1] = 54;
    m_dwB[1] = 42;
    m_dwR[2] = 98;
    m_dwG[2] = 47;
    m_dwB[2] = 4;
    m_dwR[3] = 19;
    m_dwG[3] = 46;
    m_dwB[3] = 51;

    unsigned int dwColortemp[5]{ 0xFFFFFFFF, 0xFF8888AA, 0xFFFF9955, 0xFFFFFFAA, 0xFFAAFFAA };
    for (int i = 0; i < 20; ++i)
    {
        m_ebStars[i].m_nTextureIndex = 56;
        m_ebStars[i].m_dwLifeTime = 0;
        m_ebStars[i].m_vecScale.x = 0.1f;
        m_ebStars[i].m_vecScale.y = 0.1f;
        m_ebStars[i].m_vecScale.z = 0.1f;
        m_ebStars[i].m_vecPosition.x = (float)(rand() % 12000 - 6000) * 0.003f;
        m_ebStars[i].m_vecPosition.z = (float)(rand() % 12000 - 6000) * 0.003f;

        m_ebStars[i].m_vecPosition.y = (0.5f -
            (sqrtf((m_ebStars[i].m_vecPosition.x * m_ebStars[i].m_vecPosition.x) + (m_ebStars[i].m_vecPosition.z * m_ebStars[i].m_vecPosition.z)) / 50.0f) 
            * 20.0f);

        m_ebStars[i].SetColor(dwColortemp[i % 5]);
        m_ebStars[i].m_fBaseAlpha = 0.03f;

        if (i % 2)
            m_ebStars[i].m_nFade = 0;
        else
            m_ebStars[i].m_nFade = 2;

        m_ebStars[i].m_efAlphaType = EEFFECT_ALPHATYPE::EF_BRIGHT;
    }

    m_ebMoon[0].m_nTextureIndex = 214;
    m_ebMoon[1].m_nTextureIndex = 2;

    m_ebMoon[0].m_vecPosition = TMVector3(-1.0f, 0.5f, 0.3f) * 15.0f;
    m_ebMoon[1].m_vecPosition = TMVector3(-1.0f, 0.5f, 0.3f) * 15.0f;

    m_ebMoon[0].m_dwLifeTime = 0;
    m_ebMoon[1].m_dwLifeTime = 0;
    m_ebMoon[0].m_vecScale.x = 5.0f;
    m_ebMoon[0].m_vecScale.y = 5.0f;
    m_ebMoon[0].m_vecScale.z = 5.0f;
    m_ebMoon[1].m_vecScale.x = 12.0f;
    m_ebMoon[1].m_vecScale.y = 12.0f;
    m_ebMoon[1].m_vecScale.z = 12.0f;
    m_ebMoon[0].SetColor(0xFFFFFFFF);
    m_ebMoon[1].SetColor(0x00554433);
    m_ebMoon[0].m_nFade = 0;
    m_ebMoon[1].m_nFade = 0;
    m_ebMoon[0].m_efAlphaType = EEFFECT_ALPHATYPE::EF_DEFAULT;
    m_ebMoon[1].m_efAlphaType = EEFFECT_ALPHATYPE::EF_BRIGHT;
}

TMSky::~TMSky()
{
}

int TMSky::Render()
{
    ++g_wydSkyRenderCalls;
    g_wydSkyLastDungeon = RenderDevice::m_bDungeon;
    g_wydSkyLastState = m_nState;
    g_wydSkyLastTextureIndex = m_nTextureIndex;

    if (g_bHideBackground)
    {
        ++g_wydSkyHiddenReturns;
        return 0;
    }

    if ((RenderDevice::m_bDungeon == 0 || RenderDevice::m_bDungeon == 3 || RenderDevice::m_bDungeon == 4) && 
        g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_SELCHAR)
    {
       ++g_wydSkyEligibleCalls;
       TMCamera* pCam = g_pObjectManager->m_pCamera;
       D3DMATERIAL9 materials{};
       D3DCOLORVALUE color{};

       color.r = 0.69f;
       color.g = 0.69f;
       color.b = 0.69f;
       materials.Emissive.r = 0.3f;
       materials.Emissive.g = 0.3f;
       materials.Emissive.b = 0.3f;
       materials.Diffuse.r = color.r;
       materials.Diffuse.g = color.g;
       materials.Diffuse.b = color.b;
       materials.Diffuse.a = color.a;
       materials.Specular.r = materials.Diffuse.r;
       materials.Specular.g = materials.Diffuse.g;
       materials.Specular.b = materials.Diffuse.b;
       materials.Specular.a = materials.Diffuse.a;
       materials.Power = 0.0;

       g_pDevice->m_pd3dDevice->SetMaterial(&materials);

       TMVector3 vecCam = pCam->m_cameraPos;        
       TMScene* pScene = g_pCurrentScene;
       D3DXMATRIX mat;
       D3DXMATRIX matPosition;
       D3DXMATRIX matScale;

       D3DXMatrixTranslation(&matPosition, vecCam.x, m_fHeight + 1.0f, vecCam.z);
       D3DXMatrixRotationYawPitchRoll(&mat, m_fAngle, -D3DXToRadian(90), 0);
       D3DXMatrixScaling(&matScale, m_fScale, m_fScale * 0.5f, m_fScale);
       D3DXMatrixMultiply(&mat, &g_pDevice->m_matWorld, &mat);
       D3DXMatrixMultiply(&mat, &mat, &matScale);
       D3DXMatrixMultiply(&mat, &mat, &matPosition);

       g_pDevice->m_pd3dDevice->SetTransform(D3DTS_WORLD, &mat);
       g_pDevice->SetRenderState(D3DRS_LIGHTING, 0);
       g_pDevice->SetRenderState(D3DRS_FOGENABLE, 0);
       g_pDevice->SetRenderState(D3DRS_DESTBLEND, 6u);
       TMMesh* pMesh = g_pMeshManager->GetCommonMesh(m_dwObjType, 1, 3_min);
       if (pMesh == nullptr)
       {
           ++g_wydSkyMeshNull;
           return 0;
       }

       WydSkyEnsureMeshTexture(pMesh, m_nState);
       g_wydSkyLastMeshTextureIndex = pMesh->m_nTextureIndex[0];
       g_wydSkyLastMeshHasVB = pMesh->m_pVB != nullptr ? 1 : 0;
       g_wydSkyLastMeshHasIB = pMesh->m_pIB != nullptr ? 1 : 0;
       g_wydSkyLastMeshFVF = static_cast<int>(pMesh->m_dwFVF);
       g_wydSkyLastMeshAttCount = static_cast<int>(pMesh->m_dwAttCount);
       if (pMesh->m_dwAttCount > 0)
       {
           g_wydSkyLastMeshFaceCount = static_cast<int>(pMesh->m_AttRange[0].FaceCount);
           g_wydSkyLastMeshVertexCount = static_cast<int>(pMesh->m_AttRange[0].VertexCount);
       }
       else
       {
           g_wydSkyLastMeshFaceCount = -1;
           g_wydSkyLastMeshVertexCount = -1;
       }
       WydSkyApplyRenderState(pMesh, m_nState, m_nTextureIndex);
       ++g_wydSkyMeshDraws;
       if (WydSkyDebugSkipMeshRender())
           g_wydSkyLastMeshRenderResult = 1;
       else
           g_wydSkyLastMeshRenderResult = pMesh->Render(0, 0);

       WydSkyResetRenderState();
       g_pDevice->SetRenderState(D3DRS_FOGENABLE, g_pDevice->m_bFog);
       g_pDevice->SetRenderState(D3DRS_LIGHTING, 1u);

       if (m_nTextureIndex != 68 && pMesh->m_nTextureIndex[0] != 68)
       {
           if ((m_nState == 3 || m_nState == 13 ||m_nState == 10) && m_nTextureIndex == 67)
           {
               for (int i = 0; i < 20; ++i)
               {
                   m_ebStars[i].Render();
                   m_ebMoon[0].Render();
                   m_ebMoon[1].Render();
               }
           }
           return 1;
       }
    }
    else
    {
        ++g_wydSkyBranchSkipped;
        g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, 4u);
        g_pDevice->SetRenderState(D3DRS_FOGVERTEXMODE, 3u);
        return 0;
    }

    return 1;
}

int TMSky::FrameMove(unsigned int dwServerTime)
{
    if (g_bHideBackground)
    {
       g_pDevice->m_dwClearColor = 0;
        return 0;
    }

    TMMesh* pMesh = g_pMeshManager->GetCommonMesh(m_dwObjType, 1, 3_min);
    if (pMesh == nullptr)
        return 0;

    WydSkyEnsureMeshTexture(pMesh, m_nState);
    if (g_pCurrentScene->m_pSun != nullptr)
    {
        g_pCurrentScene->m_pSun->m_bHide = m_nState && m_nState < 10 ? 1 : 0;
        if (m_nTextureIndex == 68 || pMesh->m_nTextureIndex[0] == 68)
            g_pCurrentScene->m_pSun->m_bHide = 1;
    } 

    if (pMesh->m_pVB == nullptr)
        return 0;

    D3DVERTEXBUFFER_DESC vDesc;
    RDLVERTEX* pVertex;

    pMesh->m_pVB->GetDesc(&vDesc);
    pMesh->m_pVB->Lock(0, 0, (void**)&pVertex, 0);
    int nCount = vDesc.Size / 24;
    g_ClipFar = 70.0f;
    unsigned int dwAlpha = 0x00808080;

    if (RenderDevice::m_bDungeon == 1)
    {
        TMVector2 vec(0.0f, 0.0f);

        // NOTE: this code is never reached, they are made and removed for some reason
        if ((int)vec.x >> 7 > 16 && (int)vec.x >> 7 < 20 && (int)vec.y >> 7 > 29)
        {
            g_pDevice->m_colorLight.r = 1.0f;
            g_pDevice->m_colorLight.g = 0.8f;
            g_pDevice->m_colorLight.b = 0.8f;
            if ((int)vec.x >> 7 != 18 || (int)vec.y >> 7 != 30)
            {
                g_pDevice->m_fFogStart = TMSky::FogList[0][0];
                g_pDevice->m_fFogEnd = TMSky::FogList[0][1];
            }
            else
            {
                g_pDevice->m_fFogStart = TMSky::FogList[1][0];
                g_pDevice->m_fFogEnd = TMSky::FogList[1][1];
            }
            g_pDevice->m_colorBackLight.r = 0.3f;
            g_pDevice->m_colorBackLight.g = 0.0;
            g_pDevice->m_colorBackLight.b = 0.0;
            g_pDevice->m_colorLight.a = 1.0f;
            g_pDevice->m_dwClearColor = 0x160000;
        }
        else if ((int)vec.x >> 7 == 31 && (int)vec.y >> 7 == 31)
        {
            g_pDevice->m_colorLight.r = 1.0f;
            g_pDevice->m_colorLight.g = 1.0f;
            g_pDevice->m_colorLight.b = 1.0f;
            g_pDevice->m_fFogStart = TMSky::FogList[2][0];
            g_pDevice->m_fFogEnd = TMSky::FogList[2][1];
            g_pDevice->m_colorBackLight.r = 0.6f;
            g_pDevice->m_colorBackLight.g = 0.6f;
            g_pDevice->m_colorBackLight.b = 0.6f;
            g_pDevice->m_colorLight.a = 1.0f;
        }
        else if ((int)vec.x >> 7 >= 28 && (int)vec.x >> 7 <= 30 && 
                 (int)vec.y >> 7 >= 27 && vec.y <= 28)
        {
            g_pDevice->m_colorLight.r = 0.89f;
            g_pDevice->m_colorLight.g = 0.89f;
            g_pDevice->m_colorLight.b = 0.89f;
            g_pDevice->m_fFogStart = TMSky::FogList[3][0];
            g_pDevice->m_fFogEnd = TMSky::FogList[3][1];
            g_pDevice->m_colorBackLight.r = 0.0f;
            g_pDevice->m_colorBackLight.g = 0.0f;
            g_pDevice->m_colorBackLight.b = 0.0f;
            g_pDevice->m_colorLight.a = 1.0f;
            g_pDevice->m_dwClearColor = 0;
        }
        else
        {
            g_pDevice->m_colorLight.r = 1.0f;
            g_pDevice->m_colorLight.g = 1.0f;
            g_pDevice->m_colorLight.b = 1.0f;
            g_pDevice->m_colorLight.a = 1.0f;
            g_pDevice->m_colorBackLight.r = 0.4f;
            g_pDevice->m_colorBackLight.g = 0.4f;
            g_pDevice->m_colorBackLight.b = 0.4f;
            g_pDevice->m_dwClearColor = 0;
            g_pDevice->m_fFogStart = TMSky::FogList[4][0];
            g_pDevice->m_fFogEnd = TMSky::FogList[4][1];
        }

        if (g_pCurrentScene->m_pSun != nullptr)
            g_pCurrentScene->m_pSun->m_bHide = 1;
    }
    else if (RenderDevice::m_bDungeon == 2)
    {
        g_pDevice->m_dwClearColor = 0;
        g_pDevice->m_fFogStart = TMSky::FogList[5][0];
        g_pDevice->m_fFogEnd = TMSky::FogList[5][1];
        g_pCurrentScene->m_pSun->m_bHide = 1;

        TMVector2 vec = g_pObjectManager->m_pCamera->m_pFocusedObject->m_vecPosition;

        if (((int)vec.x >> 7 == 13 || (int)vec.x >> 7 == 14) && 
            (int)vec.y >> 7 == 28)
        {
            g_pDevice->m_colorLight.r = 0.8f;
            g_pDevice->m_colorLight.g = 0.8f;
            g_pDevice->m_colorLight.b = 0.8f;
            g_pDevice->m_fFogStart = TMSky::FogList[6][0];
            g_pDevice->m_fFogEnd = TMSky::FogList[6][1];
            g_pDevice->m_colorBackLight.r = 0.6f;
            g_pDevice->m_colorBackLight.g = 0.6f;
            g_pDevice->m_colorBackLight.b = 0.6f;
            g_pDevice->m_colorLight.a = 1.0f;
        }
        else
        {
            g_pDevice->m_colorLight.r = 1.0f;
            g_pDevice->m_colorLight.g = 1.0f;
            g_pDevice->m_colorLight.b = 1.0f;
            g_pDevice->m_colorLight.a = 1.0f;
            g_pDevice->m_colorBackLight.r = 0.2f;
            g_pDevice->m_colorBackLight.g = 0.3f;
            g_pDevice->m_colorBackLight.b = 0.2f;
        }
    }
    else if (RenderDevice::m_bDungeon == 5)
    {
        g_pDevice->m_colorLight.r = 1.0f;
        g_pDevice->m_colorLight.g = 1.0f;
        g_pDevice->m_colorLight.b = 1.0f;
        g_pDevice->m_colorLight.a = 1.0f;
        g_pDevice->m_colorBackLight.r = 0.4f;
        g_pDevice->m_colorBackLight.g = 0.4f;
        g_pDevice->m_colorBackLight.b = 0.4f;
        g_pDevice->m_dwClearColor = 0;
        g_pDevice->m_fFogStart = TMSky::FogList[4][0];
        g_pDevice->m_fFogEnd = TMSky::FogList[4][1];
    }
    else
    {
        g_pDevice->m_colorBackLight.r = 0.4f;
        g_pDevice->m_colorBackLight.g = 0.4f;
        g_pDevice->m_colorBackLight.b = 0.4f;
        if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_SELECT_SERVER)
        {
            g_pDevice->m_fFogStart = TMSky::FogList[9][0];
            g_pDevice->m_fFogEnd = TMSky::FogList[9][1];
        }
        if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_DEMO)
        {
            g_pDevice->m_fFogStart = TMSky::FogList[10][0];
            g_pDevice->m_fFogEnd = TMSky::FogList[10][1];
        }
        else if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_SELCHAR)
        {
            g_pDevice->m_fFogStart = TMSky::FogList[11][0];
            g_pDevice->m_fFogEnd = TMSky::FogList[11][1];
            g_pDevice->m_bFog = 0;
        }
        else
        {
            TMVector2 vec(0.0f, 0.0f);
            if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_FIELD)
                vec = g_pObjectManager->m_pCamera->m_pFocusedObject->m_vecPosition;

            if ((int)vec.x >> 7 > 26 && (int)vec.x >> 7 < 31 &&
                (int)vec.y >> 7 > 20 && (int)vec.y >> 7 < 25)
            {
                g_pDevice->m_colorLight.r = 1.0f;
                g_pDevice->m_colorLight.g = 1.0f;
                g_pDevice->m_colorLight.b = 1.0f;
                g_pDevice->m_fFogStart = TMSky::FogList[8][0];
                g_pDevice->m_fFogEnd = TMSky::FogList[8][1];
                g_pDevice->m_colorBackLight.r = 0.6f;
                g_pDevice->m_colorBackLight.g = 0.6f;
                g_pDevice->m_colorBackLight.b = 0.6f;
                g_pDevice->m_colorLight.a = 1.0f;
                m_fScale = 0.89f;
                g_ClipFar = 90.0f;
                g_pDevice->m_dwClearColor = 0xFFFFFFFF;
                if (((int)vec.x >> 7 == 29 || (int)vec.x >> 7 == 30) &&
                    (int)vec.y >> 7 == 22)
                {
                    g_pCurrentScene->m_pSun->m_bHide = 1;
                }
            }
            else
            {
                g_pDevice->m_fFogStart = TMSky::FogList[7][0];
                g_pDevice->m_fFogEnd = TMSky::FogList[7][1];
                m_fScale = 0.5f;
                if (g_pObjectManager->m_pCamera->m_fVerticalAngle <= -0.5f)
                {
                    if ((int)g_pDevice->m_fFogEnd == 24)
                        g_pDevice->m_fFogEnd = 24.0f;
                    else
                        g_pDevice->m_fFogEnd = 24.0f - ((24.0f - g_pDevice->m_fFogEnd) * 0.89f);
                }
                else if ((int)g_pDevice->m_fFogEnd == 34)
                {
                    g_pDevice->m_fFogEnd = 34.0f;
                }
                else
                {
                    g_pDevice->m_fFogEnd = 34.0f - ((34.0f - g_pDevice->m_fFogEnd) * 0.89f);
                }
            }
        }

        g_pDevice->m_fFogStart = g_pDevice->m_fFogStart + 5.0f;
        g_pDevice->m_fFogEnd = g_pDevice->m_fFogEnd + 5.0f;
        if (m_nState / 10 == 1)
        {
            unsigned int dwElapsed = g_pTimerManager->GetServerTime() - m_dwStartTime;
            if (dwElapsed >= m_dwChangeTime)
            {
                SetWeatherState(m_nState % 10);
                g_pDevice->m_colorLight = m_LightVal[m_nState % 10];
            }
            else
            {
                float fProgress = (float)dwElapsed / (float)m_dwChangeTime;
                dwAlpha = 0x888888 | ((unsigned int)(float)(fProgress * 255.0f) << 24);
                if (m_nState == 13)
                {
                    unsigned int dwMoon = 0xFFFFFF | ((unsigned int)(float)(fProgress * 255.0f) << 24);
                    m_ebMoon[0].SetColor(dwMoon);
                }
                else if (m_nState == 10 && m_nTextureIndex == 67)
                {
                    unsigned int dwMoon = 0xFFFFFF | ((unsigned int)(float)((1.0f - fProgress) * 255.0f) << 24);
                    m_ebMoon[0].SetColor(dwMoon);

                    if (g_pCurrentScene->m_pSun != nullptr)
                        g_pCurrentScene->m_pSun->m_fDefSize = fProgress;
                }
                else if (m_nState > 10 && (m_nTextureIndex == 68 || m_nTextureIndex == 69) && g_pCurrentScene->m_pSun != nullptr)
                    g_pCurrentScene->m_pSun->m_fDefSize = 1.0f - fProgress;
                
                // TODO: this code maybe crash. The -264 maybe is - 67.
                unsigned int dwR = static_cast<unsigned int>((unsigned char)((float)m_dwR[pMesh->m_nTextureIndex[0] - 67] * (float)(1.0f - fProgress)) +
                    (float)((float)m_dwR[m_nTextureIndex - 67] * fProgress));
                unsigned int dwG = static_cast<unsigned int>((unsigned char)((float)m_dwG[pMesh->m_nTextureIndex[0] - 67] * (float)(1.0f - fProgress)) +
                    (float)((float)m_dwG[m_nTextureIndex - 67] * fProgress));
                unsigned int dwB = static_cast<unsigned int>((unsigned char)((float)m_dwB[pMesh->m_nTextureIndex[0] - 67] * (float)(1.0f - fProgress)) +
                    (float)((float)m_dwB[m_nTextureIndex - 67] * fProgress));

                g_pDevice->m_dwClearColor = dwB | (dwG << 8) | (dwR << 16);

                g_pDevice->m_colorLight.r = (float)((float)(1.0 - fProgress)
                    * TMSky::m_LightVal[pMesh->m_nTextureIndex[0] - 67].r)
                    + (float)(TMSky::m_LightVal[m_nTextureIndex - 67].r * fProgress);
                g_pDevice->m_colorLight.g = (float)((float)(1.0 - fProgress)
                    * TMSky::m_LightVal[pMesh->m_nTextureIndex[0] - 67].g)
                    + (float)(TMSky::m_LightVal[m_nTextureIndex - 67].g * fProgress);
                g_pDevice->m_colorLight.b = (float)((float)(1.0 - fProgress)
                    * TMSky::m_LightVal[pMesh->m_nTextureIndex[0] - 67].b)
                    + (float)(TMSky::m_LightVal[m_nTextureIndex - 67].b * fProgress);
            }
        }
        else
        {
            SetWeatherState(m_nState % 10);
            g_pDevice->m_colorLight = m_LightVal[m_nState % 10];
        }
    }

    if (g_pObjectManager->m_pCamera->m_bInWater)
    {
        g_pDevice->m_fFogStart = 1.0f;
        g_pDevice->m_fFogEnd = 10.0f;

        if (m_nState == 0|| RenderDevice::m_bDungeon == 1 || RenderDevice::m_bDungeon == 2)
            g_pDevice->m_dwClearColor = 0xFF335599;        
        else if(m_nState == 1)
            g_pDevice->m_dwClearColor = 0xFF333333;
        else if(m_nState == 2)
            g_pDevice->m_dwClearColor = 0xFF441100;
        else if(m_nState == 3)
            g_pDevice->m_dwClearColor = 0xFF222222;
    }

    if (m_nState != 0 && m_nState != 2 && m_nState != 12)
    {
        for (int i = 0; i < 2; ++i)
        {
            TMObjectContainer* pContainer = g_pCurrentScene->m_pObjectContainerList[i];
            if (pContainer != nullptr)
            {
                int nLightIndex = pContainer->m_nLightIndex;
                for (int j = 0; j < nLightIndex; ++j)
                    pContainer->m_pLightContainer[j]->m_bEnable = 1;
            }
        }
    }
    else
    {
        for (int i = 0; i < 2; ++i)
        {
            TMObjectContainer* pContainer = g_pCurrentScene->m_pObjectContainerList[i];
            if (pContainer != nullptr)
            {
                int nLightIndex = pContainer->m_nLightIndex;
                for (int j = 0; j < nLightIndex; ++j)
                    pContainer->m_pLightContainer[j]->m_bEnable = 0;
            }
        }
    }

    if (g_pDevice->m_bVoodoo == 1)
        dwAlpha = 0xFFFFFFFF;

    for (int m = 0; m < nCount; ++m)
        pVertex[m].diffuse = dwAlpha;

    pMesh->m_pVB->Unlock();

    // NOTE: m_nTextIndex maybe [0]?
    if (m_nState == 3 ||m_nState == 13 || m_nState == 10 && pMesh->m_nTextureIndex[1] != 0)
    {
        TMVector3 vecTemp; 
        TMVector3 vecCam = g_pObjectManager->m_pCamera->GetCameraPos();
        for (int m = 0; m < 20; ++m)
        {
            vecTemp = m_ebStars[m].m_vecPosition;
            m_ebStars[m].m_vecPosition.x = m_ebStars[m].m_vecPosition.x + vecCam.x;
            m_ebStars[m].m_vecPosition.y = m_ebStars[m].m_vecPosition.y + vecCam.y;
            m_ebStars[m].m_vecPosition.z = m_ebStars[m].m_vecPosition.z + vecCam.z;
            m_ebStars[m].FrameMove(dwServerTime);
            m_ebStars[m].m_vecPosition = vecTemp;
        }

        vecTemp = m_ebMoon[0].m_vecPosition;
        m_ebMoon[0].m_vecPosition.x = m_ebMoon[0].m_vecPosition.x + vecCam.x;
        m_ebMoon[0].m_vecPosition.y = m_ebMoon[0].m_vecPosition.y + vecCam.y;
        m_ebMoon[0].m_vecPosition.z = m_ebMoon[0].m_vecPosition.z + vecCam.z;
        m_ebMoon[0].FrameMove(dwServerTime);
        m_ebMoon[0].m_vecPosition = vecTemp;

        vecTemp = m_ebMoon[1].m_vecPosition;
        m_ebMoon[1].m_vecPosition.x = m_ebMoon[1].m_vecPosition.x + vecCam.x;
        m_ebMoon[1].m_vecPosition.y = m_ebMoon[1].m_vecPosition.y + vecCam.y;
        m_ebMoon[1].m_vecPosition.z = m_ebMoon[1].m_vecPosition.z + vecCam.z;
        m_ebMoon[1].FrameMove(dwServerTime);
        m_ebMoon[1].m_vecPosition = vecTemp;
    }
    if (g_pObjectManager->m_pCamera->m_fSightLength > 10.0f)
    {
        g_pDevice->m_fFogEnd = (float)(g_pObjectManager->m_pCamera->m_fSightLength - 10.0f)
            + g_pDevice->m_fFogEnd;
    }
        
    return 1;
}

void TMSky::RestoreDeviceObjects()
{
    ;
}

void TMSky::SetWeatherState(int nState)
{
    TMMesh* pMesh = g_pMeshManager->GetCommonMesh(m_dwObjType, 1, 3_min);
    if (pMesh != nullptr)
    {
        pMesh->m_bEffect = 1;
        if (!(nState / 10))
        {
            //This code is extrem confuse, the compiler did some optimization that make the code weird
            pMesh->m_nTextureIndex[0] = WydSkyTextureIndexForState(nState);
            m_nTextureIndex = pMesh->m_nTextureIndex[0];
            unsigned int dwR = static_cast<unsigned char>(m_dwR[pMesh->m_nTextureIndex[0] - 67]);
            unsigned int dwG = static_cast<unsigned char>(m_dwG[pMesh->m_nTextureIndex[0] - 67]);
            unsigned int dwB = static_cast<unsigned char>(m_dwB[pMesh->m_nTextureIndex[0] - 67]);
            g_pDevice->m_dwClearColor = dwB | (dwG << 8) | (dwR << 16);
        }
        else if (nState / 10 == 1)
        {
            pMesh->m_nTextureIndex[0] = WydSkyTextureIndexForState(m_nState);
            m_nTextureIndex = WydSkyTextureIndexForState(nState);
        }

        m_nState = nState;
        m_dwStartTime = g_pTimerManager->GetServerTime();
    }
}
