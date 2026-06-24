#include "pch.h"
#include "TMFont2.h"
#include "TMGlobal.h"

char* TMFont2::m_pBuffer{};
unsigned int TMFont2::m_nLength = 0;

#if defined(__EMSCRIPTEN__)
extern "C" void wyd_d3d9_set_draw_label(const char* label);

namespace {
unsigned int g_wydFont2SetTextCalls = 0;
unsigned int g_wydFont2SetTextNonEmpty = 0;
unsigned int g_wydFont2RenderCalls = 0;
unsigned int g_wydFont2RenderNonEmpty = 0;
unsigned int g_wydFont2TextureCreateFail = 0;
unsigned int g_wydFont2LockCalls = 0;
unsigned int g_wydFont2LastTextLen = 0;
unsigned int g_wydFont2LastLineCount = 0;
unsigned int g_wydFont2LastSize0 = 0;
unsigned int g_wydFont2LastSize1 = 0;
unsigned int g_wydFont2LastSize2 = 0;
unsigned int g_wydFont2LastAlphaPixels = 0;
unsigned int g_wydFont2LastHasBitmap = 0;
unsigned int g_wydFont2LastNonEmptyAlphaPixels = 0;
unsigned int g_wydFont2LastNonEmptyHasBitmap = 0;
unsigned int g_wydFont2LastNonEmptySize0 = 0;
unsigned int g_wydFont2MaxAlphaPixels = 0;
unsigned int g_wydFont2MaxSize0 = 0;
int g_wydFont2LastRenderX = 0;
int g_wydFont2LastRenderY = 0;
int g_wydFont2LastRenderType = 0;
int g_wydFont2LastNonEmptyRenderX = 0;
int g_wydFont2LastNonEmptyRenderY = 0;
int g_wydFont2LastNonEmptyRenderType = 0;
char g_wydFont2LastText[128]{};
char g_wydFont2LastNonEmptyText[128]{};

void WydFont2RememberText(const char* text, int length)
{
	g_wydFont2LastTextLen = length > 0 ? static_cast<unsigned int>(length) : 0;
	memset(g_wydFont2LastText, 0, sizeof(g_wydFont2LastText));
	if (text && length > 0)
	{
		strncpy(g_wydFont2LastText, text, sizeof(g_wydFont2LastText) - 1);
		memset(g_wydFont2LastNonEmptyText, 0, sizeof(g_wydFont2LastNonEmptyText));
		strncpy(g_wydFont2LastNonEmptyText, text, sizeof(g_wydFont2LastNonEmptyText) - 1);
	}
}

void WydFont2SetDrawLabel(const char* text, int x, int y, int renderType, int line, int width)
{
	char label[192]{};
	snprintf(label, sizeof(label), "TMFont2 text=\"%.96s\" x=%d y=%d type=%d line=%d width=%d",
		text ? text : "", x, y, renderType, line, width);
	wyd_d3d9_set_draw_label(label);
}

void WydFont2EnsureBitmap()
{
	if (g_pDevice == nullptr || g_pDevice->m_hbmBitmap != nullptr)
		return;

	memset(&g_pDevice->m_bmi, 0, sizeof(g_pDevice->m_bmi));
	g_pDevice->m_bmi.bmiHeader.biSize = 40;
	g_pDevice->m_bmi.bmiHeader.biWidth = RenderDevice::m_nFontTextureSize;
	g_pDevice->m_bmi.bmiHeader.biHeight = -RenderDevice::m_nFontTextureSize;
	g_pDevice->m_bmi.bmiHeader.biPlanes = 1;
	g_pDevice->m_bmi.bmiHeader.biCompression = 0;
	g_pDevice->m_bmi.bmiHeader.biBitCount = 32;
	g_pDevice->m_hDC = CreateCompatibleDC(0);
	g_pDevice->m_hbmBitmap = CreateDIBSection(
		g_pDevice->m_hDC,
		&g_pDevice->m_bmi,
		0,
		reinterpret_cast<void**>(&g_pDevice->m_pBitmapBits),
		0,
		0);

	sprintf_s(g_szFontName, "Tahoma");
	if (g_nFontBold <= 0)
		g_nFontBold = 500;

	g_pDevice->m_hFont = CreateFont(RenderDevice::m_nFontSize, 0, 0, 0, g_nFontBold, 0, 0, 0, 1, 4, 0, 4, 2, g_szFontName);
	SelectObject(g_pDevice->m_hDC, g_pDevice->m_hbmBitmap);
	SelectObject(g_pDevice->m_hDC, g_pDevice->m_hFont);
	SetTextColor(g_pDevice->m_hDC, static_cast<COLORREF>(0xFFFFFF));
	SetBkColor(g_pDevice->m_hDC, 0);
}
}

extern "C" unsigned int wyd_font2_set_text_calls() { return g_wydFont2SetTextCalls; }
extern "C" unsigned int wyd_font2_set_text_nonempty() { return g_wydFont2SetTextNonEmpty; }
extern "C" unsigned int wyd_font2_render_calls() { return g_wydFont2RenderCalls; }
extern "C" unsigned int wyd_font2_render_nonempty() { return g_wydFont2RenderNonEmpty; }
extern "C" unsigned int wyd_font2_texture_create_fail() { return g_wydFont2TextureCreateFail; }
extern "C" unsigned int wyd_font2_lock_calls() { return g_wydFont2LockCalls; }
extern "C" unsigned int wyd_font2_last_text_len() { return g_wydFont2LastTextLen; }
extern "C" unsigned int wyd_font2_last_line_count() { return g_wydFont2LastLineCount; }
extern "C" unsigned int wyd_font2_last_size0() { return g_wydFont2LastSize0; }
extern "C" unsigned int wyd_font2_last_size1() { return g_wydFont2LastSize1; }
extern "C" unsigned int wyd_font2_last_size2() { return g_wydFont2LastSize2; }
extern "C" unsigned int wyd_font2_last_alpha_pixels() { return g_wydFont2LastAlphaPixels; }
extern "C" unsigned int wyd_font2_last_has_bitmap() { return g_wydFont2LastHasBitmap; }
extern "C" unsigned int wyd_font2_last_nonempty_alpha_pixels() { return g_wydFont2LastNonEmptyAlphaPixels; }
extern "C" unsigned int wyd_font2_last_nonempty_has_bitmap() { return g_wydFont2LastNonEmptyHasBitmap; }
extern "C" unsigned int wyd_font2_last_nonempty_size0() { return g_wydFont2LastNonEmptySize0; }
extern "C" unsigned int wyd_font2_max_alpha_pixels() { return g_wydFont2MaxAlphaPixels; }
extern "C" unsigned int wyd_font2_max_size0() { return g_wydFont2MaxSize0; }
extern "C" int wyd_font2_last_render_x() { return g_wydFont2LastRenderX; }
extern "C" int wyd_font2_last_render_y() { return g_wydFont2LastRenderY; }
extern "C" int wyd_font2_last_render_type() { return g_wydFont2LastRenderType; }
extern "C" int wyd_font2_last_nonempty_render_x() { return g_wydFont2LastNonEmptyRenderX; }
extern "C" int wyd_font2_last_nonempty_render_y() { return g_wydFont2LastNonEmptyRenderY; }
extern "C" int wyd_font2_last_nonempty_render_type() { return g_wydFont2LastNonEmptyRenderType; }
extern "C" const char* wyd_font2_last_text() { return g_wydFont2LastText; }
extern "C" const char* wyd_font2_last_nonempty_text() { return g_wydFont2LastNonEmptyText; }
#endif

TMFont2::TMFont2()
{
	m_dwShadeColor = 0xFF000000;
	m_dwColor = 0xFFFFFFFF;
	m_pTexture = nullptr;
	m_fSize = 1.0f;
	m_bMultiLine = 0;
	memset(m_szString, 0, sizeof(m_szString));
	memset(m_szStringArray, 0, sizeof(m_szStringArray));
	memset(m_szStringSize, 0, sizeof(m_szStringSize));

	m_nPosX = -1;
	m_nPosY = -1;
}

TMFont2::~TMFont2()
{
	SAFE_RELEASE(m_pTexture);
}

int TMFont2::SetText(const char* szString, unsigned int dwColor, int bCheckZero)
{
	m_nLineNumber = 0;
	int nStrLength = strlen(szString);
#if defined(__EMSCRIPTEN__)
	WydFont2EnsureBitmap();
	++g_wydFont2SetTextCalls;
	if (nStrLength > 0)
		++g_wydFont2SetTextNonEmpty;
	WydFont2RememberText(szString, nStrLength);
#endif

	if (nStrLength < 0 || nStrLength >= MAX_STRLENGTH)
		return 0;

	if (RenderDevice::m_nBright > 58)
	{
		int nValue = RenderDevice::m_nBright - 40;
		int dwA = dwColor & 0xFF000000;

		int nR = WYDCOLOR_RED(dwColor) - nValue;
		int nG = WYDCOLOR_GREEN(dwColor) - nValue;
		int nB = WYDCOLOR_BLUE(dwColor) - nValue;

		if (nR < 0)
			nR = 0;
		if (nG < 0)
			nG = 0;
		if (nB < 0)
			nB = 0;

		dwColor = WYD_RGBA(nR, nG, nB, dwA);
	}

	m_dwColor = dwColor;

	sprintf(m_szString, "%s", szString);
	m_szString[83] = 0;
	m_szString[82] = 0;

	char tempbuff[256]{};
	char* temp = tempbuff;

	strcpy(tempbuff, m_szString);
	int nLen = strlen(m_szString);

	if (nLen > 84)
	{
		if (IsClearString(temp, ')'))
		{
			strncpy(m_szStringArray[0], temp, 42);
			temp += '*';
		}
		else
		{
			strncpy(m_szStringArray[0], temp, 41);
			temp += ')';
		}
		if (IsClearString(temp, ')'))
		{
			strncpy(m_szStringArray[1], temp, 42);
			temp += '*';
			strncpy(m_szStringArray[2], temp, strlen(temp));
		}
		else
		{
			strncpy(m_szStringArray[1], temp, 41);
			temp += ')';
			strcpy(m_szStringArray[2], temp);
		}
		m_nLineNumber = 3;
	}
	else if (nLen > 42)
	{
		memset(m_szStringArray[0], 0, 44);
		memset(m_szStringArray[1], 0, 44);
		if (IsClearString(temp, ')'))
		{
			strncpy(m_szStringArray[0], temp, 42);
			temp += '*';
		}
		else
		{
			strncpy(m_szStringArray[0], temp, 41);
			temp += ')';
		}

		strcpy(m_szStringArray[1], temp);
		m_nLineNumber = 2;
	}
	else
	{
		memset(m_szStringArray[0], 0, 44);
		strncpy(m_szStringArray[0], temp, nLen);
		m_nLineNumber = 1;
	}

	if (g_pDevice->m_hbmBitmap != nullptr)
	{
#if defined(__EMSCRIPTEN__)
		g_wydFont2LastHasBitmap = 1;
#endif
		memset(m_szStringSize, 0, sizeof(m_szStringSize));
		RECT rect;
		rect.left = 0;
		rect.top = 0;
		rect.right = RenderDevice::m_nFontTextureSize;
		rect.bottom = RenderDevice::m_nFontSize * m_nLineNumber;

		FillRect(g_pDevice->m_hDC, &rect, 0);
		for (int nLine = 0; nLine < m_nLineNumber; ++nLine)
		{
			char szTemp[45]{};
			memset(szTemp, ' ', sizeof(szTemp));

			TextOut(g_pDevice->m_hDC, 0, nLine * (RenderDevice::m_nFontSize + 1), szTemp, strlen(szTemp));

			sprintf(szTemp, "%s ", m_szStringArray[nLine]);

			if(strlen(szTemp))
				TextOut(g_pDevice->m_hDC, 0, nLine * (RenderDevice::m_nFontSize + 1), szTemp, strlen(szTemp));

			if (bCheckZero)
			{
				char szTemp2[45]{};

				bool bFind = false;
				int nLenTemp = strlen(szTemp);

				for (int nT = 0; nT < nLenTemp; ++nT)
				{
					if (szTemp[nT] == '0')
					{
						szTemp2[nT] = '/';
						bFind = true;
					}
					else
					{
						szTemp2[nT] = ' ';
					}
				}

				if (bFind)
				{
					SetBkMode(g_pDevice->m_hDC, 1);
					TextOut(g_pDevice->m_hDC, 0, nLine * (RenderDevice::m_nFontSize + 1),
						szTemp2, strlen(szTemp2));
					SetBkMode(g_pDevice->m_hDC, 2);
				}
			}

			SIZE size;
			GetTextExtentPoint32(g_pDevice->m_hDC, m_szStringArray[nLine],
				strlen(m_szStringArray[nLine]), &size);

			m_szStringSize[nLine] = static_cast<short>(size.cx);
		}
	}
#if defined(__EMSCRIPTEN__)
	else
	{
		g_wydFont2LastHasBitmap = 0;
	}

	g_wydFont2LastLineCount = static_cast<unsigned int>(m_nLineNumber);
	g_wydFont2LastSize0 = static_cast<unsigned int>(m_szStringSize[0]);
	g_wydFont2LastSize1 = static_cast<unsigned int>(m_szStringSize[1]);
	g_wydFont2LastSize2 = static_cast<unsigned int>(m_szStringSize[2]);
	if (nStrLength > 0)
	{
		g_wydFont2LastNonEmptyHasBitmap = g_wydFont2LastHasBitmap;
		g_wydFont2LastNonEmptySize0 = g_wydFont2LastSize0;
	}
	if (g_wydFont2LastSize0 > g_wydFont2MaxSize0)
		g_wydFont2MaxSize0 = g_wydFont2LastSize0;
	if (g_pDevice->m_hbmBitmap == nullptr || g_pDevice->m_pBitmapBits == nullptr)
		return TRUE;
#endif

	if (m_pTexture == nullptr)
	{
		if (g_pDevice->m_bSavage == 1)
		{
			D3DXCreateTexture(g_pDevice->m_pd3dDevice,
				RenderDevice::m_nFontTextureSize,
				RenderDevice::m_nFontTextureSize / 8,
				1,
				0,
				D3DFORMAT::D3DFMT_A4R4G4B4,
				D3DPOOL::D3DPOOL_MANAGED,
				&m_pTexture);
		}
		else
		{
			if (TMFont2::m_pBuffer == nullptr)
			{
				int handle = _open(MiniMap_Path, _O_BINARY);

				if (handle == -1)
				{
#if defined(__EMSCRIPTEN__)
					++g_wydFont2TextureCreateFail;
#endif
					return FALSE;
				}

				char szHeader[5] = { 0, };

				_read(handle, szHeader, 4);
				TMFont2::m_nLength = _filelength(handle) - 4;
				TMFont2::m_pBuffer = new char[m_nLength + 18];

				if (TMFont2::m_pBuffer == nullptr)
				{
					_close(handle);
#if defined(__EMSCRIPTEN__)
					++g_wydFont2TextureCreateFail;
#endif
					return FALSE;
				}

				_read(handle, TMFont2::m_pBuffer, TMFont2::m_nLength);
				_close(handle);
				strcpy((char*)&TMFont2::m_pBuffer[TMFont2::m_nLength], "TRUEVISION-XFILE");
			}

			HRESULT rst = D3DXCreateTextureFromFileInMemoryEx(
				g_pDevice->m_pd3dDevice,
				TMFont2::m_pBuffer,
				TMFont2::m_nLength + 18,
				RenderDevice::m_nFontTextureSize,
				RenderDevice::m_nFontTextureSize / 8,
				1,
				0,
				D3DFORMAT::D3DFMT_A4R4G4B4,
				D3DPOOL::D3DPOOL_MANAGED,
				1,
				1,
				0xFF000000,
				NULL,
				NULL,
				&m_pTexture
				);

			if (FAILED(rst))
			{
				m_pTexture = nullptr;
#if defined(__EMSCRIPTEN__)
				++g_wydFont2TextureCreateFail;
#endif
				return FALSE;
			}
		}
	}

	if (m_pTexture == nullptr)
	{
#if defined(__EMSCRIPTEN__)
		++g_wydFont2TextureCreateFail;
#endif
		return 0;
	}

	D3DLOCKED_RECT d3dlr;
	m_pTexture->LockRect(0, &d3dlr, nullptr, 0);
#if defined(__EMSCRIPTEN__)
	++g_wydFont2LockCalls;
	unsigned int nAlphaPixels = 0;
#endif

	char* pDstRow = (char*)d3dlr.pBits;
	unsigned short* pDst16;

	for (int nY = 0; nY < m_nLineNumber * (RenderDevice::m_nFontSize + 1); ++nY)
	{
		pDst16 = (unsigned short*)pDstRow;
		for (int nX = 0; nX < RenderDevice::m_nFontTextureSize; ++nX)
		{
#if defined(__EMSCRIPTEN__)
			// Canvas 2D produces alpha-based anti-aliasing (constant RGB,
			// varying alpha).  GDI ClearType varies RGB channels.  Read
			// the actual alpha byte (>>24 on little-endian uint32).
			unsigned int pixel = g_pDevice->m_pBitmapBits[nX + nY * RenderDevice::m_nFontTextureSize];
			char bAlpha = static_cast<char>((pixel >> 24) & 0xFF) >> 4;
#else
			char bAlpha = (g_pDevice->m_pBitmapBits[nX + nY * RenderDevice::m_nFontTextureSize] & 0xFF) >> 4;
#endif

			if (bAlpha <= 0)
				*pDst16 = 0;
			else
			{
				*pDst16 = ((unsigned char)bAlpha << 12) | 0xFFF;
#if defined(__EMSCRIPTEN__)
				++nAlphaPixels;
#endif
			}

			++pDst16;
		}

		pDstRow += d3dlr.Pitch;
	}

	m_pTexture->UnlockRect(0);
#if defined(__EMSCRIPTEN__)
	g_wydFont2LastAlphaPixels = nAlphaPixels;
	if (nStrLength > 0)
		g_wydFont2LastNonEmptyAlphaPixels = nAlphaPixels;
	if (nAlphaPixels > g_wydFont2MaxAlphaPixels)
		g_wydFont2MaxAlphaPixels = nAlphaPixels;
#endif
	return TRUE;
}

char* TMFont2::GetText()
{
	return m_szString;
}

int TMFont2::Render(int nPosX, int nPosY, int nRenderType)
{
	if (m_nPosX != -1)
		nPosX = m_nPosX;
	if (m_nPosY != -1)
		nPosY = m_nPosY;
#if defined(__EMSCRIPTEN__)
	++g_wydFont2RenderCalls;
	g_wydFont2LastRenderX = nPosX;
	g_wydFont2LastRenderY = nPosY;
	g_wydFont2LastRenderType = nRenderType;
#endif

	nPosY = nPosY + 1;
	int strLen = strlen(m_szString);
#if defined(__EMSCRIPTEN__)
	if (m_pTexture == nullptr || (strLen > 0 && m_szStringSize[0] <= 0 && g_pDevice->m_hbmBitmap != nullptr))
	{
		char szRefresh[MAX_STRLENGTH]{};
		strncpy(szRefresh, m_szString, sizeof(szRefresh) - 1);
		SetText(szRefresh, m_dwColor, 0);
	}
#else
	if (m_pTexture == nullptr)
		SetText(m_szString, m_dwColor, 0);
#endif

	strLen = strlen(m_szString);

	if (strLen <= 0 || strLen >= MAX_STRRENDER)
		return 0;
#if defined(__EMSCRIPTEN__)
	++g_wydFont2RenderNonEmpty;
	g_wydFont2LastNonEmptyRenderX = nPosX;
	g_wydFont2LastNonEmptyRenderY = nPosY;
	g_wydFont2LastNonEmptyRenderType = nRenderType;
#endif

	g_pDevice->SetRenderStateBlock(2);

	int nLength = 0;
	for (int nLine = 0; nLine < m_nLineNumber; ++nLine)
	{
		int nLocLen = m_szStringSize[nLine];
		if (m_bMultiLine == 1)
		{
			if (nRenderType != (int)RENDERCTRLTYPE::RENDER_TEXT)
			{
#if defined(__EMSCRIPTEN__)
				WydFont2SetDrawLabel(m_szString, nPosX + 1, nPosY + 1 + nLine * RenderDevice::m_nFontSize, nRenderType, nLine, nLocLen);
#endif
				g_pDevice->RenderRectC(
					0.0f,
					(float)nLine * (float)(RenderDevice::m_nFontSize + 1),
					(float)(nLocLen),
					(float)RenderDevice::m_nFontSize,
					(float)(nPosX + 1),
					((float)(nPosY + 1) + (float)(nLine * RenderDevice::m_nFontSize)),
					m_pTexture,
					m_dwShadeColor,
					1.0f,
					1.0f);
			}

#if defined(__EMSCRIPTEN__)
			WydFont2SetDrawLabel(m_szString, nPosX, nPosY + nLine * RenderDevice::m_nFontSize, nRenderType, nLine, nLocLen);
#endif
			g_pDevice->RenderRectC(
				0.0f,
				(float)nLine * (float)(RenderDevice::m_nFontSize + 1),
				(float)nLocLen,
				(float)RenderDevice::m_nFontSize,
				(float)(nPosX),
				(float)((float)(nPosY) + (float)(nLine * RenderDevice::m_nFontSize)),
				m_pTexture,
				m_dwColor,
				1.0f,
				1.0f);
		}
		else
		{
			if (nRenderType != (int)RENDERCTRLTYPE::RENDER_TEXT)
			{
#if defined(__EMSCRIPTEN__)
				WydFont2SetDrawLabel(m_szString, (int)(((float)nPosX + (float)nLength) + 1.0f), nPosY + 1, nRenderType, nLine, nLocLen);
#endif
				g_pDevice->RenderRectC(
					0.0f,
					(float)nLine * (float)(RenderDevice::m_nFontSize + 1),
					(float)nLocLen,
					(float)RenderDevice::m_nFontSize,
					((float)nPosX + (float)nLength) + 1.0f,
					(float)(nPosY + 1),
					m_pTexture,
					m_dwShadeColor,
					m_fSize,
					m_fSize);
			}

#if defined(__EMSCRIPTEN__)
			WydFont2SetDrawLabel(m_szString, (int)((float)nPosX + (float)nLength), nPosY, nRenderType, nLine, nLocLen);
#endif
			g_pDevice->RenderRectC(
				0.0f,
				(float)nLine * (float)(RenderDevice::m_nFontSize + 1),
				(float)nLocLen,
				(float)RenderDevice::m_nFontSize,
				(float)nPosX + (float)nLength,
				(float)nPosY,
				m_pTexture,
				m_dwColor,
				m_fSize,
				m_fSize);
		}

		nLength += nLocLen;
	}

	return TRUE;
}

int TMFont2::StrByteCheck(char* szString)
{
	int value = 0;
	bool byteCheck = false;
	for (size_t i = 0; ; ++i)
	{
		if (i >= strlen(szString))
			break;

		if (szString[i] >= 'A' && szString[i] <= 'z')
		{
			++value;
		}
		else if (byteCheck == 1)
		{
			++value;
			byteCheck = 0;
		}
		else
		{
			byteCheck = 1;
		}
	}

	return value;
}
