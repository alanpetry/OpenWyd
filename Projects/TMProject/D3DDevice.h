#pragma once

#include "D3DEnumeration.h"
#include "D3DSettings.h"

enum APPMSGTYPE { MSG_NONE, MSGERR_APPMUSTEXIT, MSGWARN_SWITCHEDTOREF };

#define D3DAPPERR_NODIRECT3D          ((HRESULT)0x82000001L)
#define D3DAPPERR_NOWINDOW            ((HRESULT)0x82000002L)
#define D3DAPPERR_NOCOMPATIBLEDEVICES ((HRESULT)0x82000003L)
#define D3DAPPERR_NOWINDOWABLEDEVICES ((HRESULT)0x82000004L)
#define D3DAPPERR_NOHARDWAREDEVICE    ((HRESULT)0x82000005L)
#define D3DAPPERR_HALNOTCOMPATIBLE    ((HRESULT)0x82000006L)
#define D3DAPPERR_NOWINDOWEDHAL       ((HRESULT)0x82000007L)
#define D3DAPPERR_NODESKTOPHAL        ((HRESULT)0x82000008L)
#define D3DAPPERR_NOHALTHISMODE       ((HRESULT)0x82000009L)
#define D3DAPPERR_NONZEROREFCOUNT     ((HRESULT)0x8200000aL)
#define D3DAPPERR_MEDIANOTFOUND       ((HRESULT)0x8200000bL)
#define D3DAPPERR_RESETFAILED         ((HRESULT)0x8200000cL)
#define D3DAPPERR_NULLREFDEVICE       ((HRESULT)0x8200000dL)

class D3DDevice
{
public:	
	D3DDevice();

	virtual int Initialize(HWND hWnd);			
	virtual void Pause(bool bPause);
	virtual ~D3DDevice();
	
	HRESULT DisplayErrorMsg(HRESULT hr, unsigned int dwType);
	// static?
	static bool ConfirmDeviceHelper(D3DCAPS9* pCaps,
		VertexProcessingType vertexProcessingType, D3DFORMAT backBufferFormat);
	void BuildPresentParamsFromSettings();
	char FindBestWindowedMode(bool bRequireHAL, bool bRequireREF);
	char FindBestFullscreenMode(bool bRequireHAL, bool bRequireREF);
	HRESULT ChooseInitialD3DSettings();
	HRESULT Initialize3DEnvironment();
	HRESULT HandlePossibleSizeChange();
	HRESULT Reset3DEnvironment();
	HRESULT ToggleFullscreen();
	HRESULT ForceWindowed();
	HRESULT UserSelectNewDevice();
	void Cleanup3DEnvironment();

	virtual HRESULT AdjustWindowForChange();
	static HRESULT ConfirmDevice(D3DCAPS9* pCaps, DWORD dwBehavior, D3DFORMAT backBufferFormat);
	virtual HRESULT OneTimeSceneInit();
	virtual HRESULT InitDeviceObjects();
	virtual HRESULT RestoreDeviceObjects();
	virtual HRESULT FrameMove();
	virtual HRESULT Render();
	virtual HRESULT InvalidateDeviceObjects();
	virtual HRESULT DeleteDeviceObjects();
	virtual HRESULT FinalCleanup();
	void CaptureScreen();

public:
	CD3DEnumeration m_d3dEnumeration;
	CD3DSettings m_d3dSettings;
	int m_bG400;
	bool m_bWindowed;
	bool m_bDeviceLost;
	bool m_bMinized;
	bool m_bMaximized;
	bool m_bIgnoreSizeChange;
	bool m_bDeviceObjectsInited;
	bool m_bDeviceObjectsRestored;
	unsigned int m_dwScreenWidth;
	unsigned int m_dwScreenHeight;
	unsigned int m_dwBitCount;
	unsigned int m_dwMaxStageNum;
	D3DFORMAT m_eFormat;
	unsigned int m_dwOS;
	int m_iVGAID;
	D3DPRESENT_PARAMETERS m_d3dpp;
	HWND m_hWnd;
	HWND m_hWndFocus;
	HMENU m_hMenu;
	IDirect3D9* m_pD3D;
	IDirect3DDevice9* m_pd3dDevice;
	D3DCAPS9 m_d3dCaps;
	D3DSURFACE_DESC m_d3dsdBackBuffer;
	unsigned int m_dwCreateFlags;
	unsigned int m_dwWindowStyle;
	RECT m_rcWindowBounds;
	RECT m_rcWindowClient;
	float m_fTime;
	float m_fElapsedTime;
	float m_fFPS;
	char m_strDeviceStats[90];
	char m_strFrameStats[90];
	bool m_bShowCursorWhenFullscreen;
	bool m_bClipCursorWhenFullscreen;
	bool m_bStartFullscreen;
	int m_nAntiAliasLevel;
	int m_bSavage;
	int m_bVoodoo;
	int m_bNVIDIA;
	int m_bIntel;
	int m_bDXT1;
	int m_bDXT3;
	int m_bTNT;

public:
	static int m_bDxt;
	static int m_nMipMap;
};
