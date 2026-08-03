#include "pch.h"

#include "NewApp.h"
#include "OpenWydOptimized.h"
#include "RenderDevice.h"
#include "SControl.h"
#include "SControlContainer.h"
#include "TMGlobal.h"
#include "TMScene.h"

#include <algorithm>
#include <cmath>

namespace
{
WydDisplayMode g_displayMode = WydDisplayMode::Legacy;
WydQualityProfile g_qualityProfile = WydQualityProfile::Auto;
WydViewportMetrics g_viewport{800, 600, 800, 600, 100, 1.0f, 100, 1.0f};

int ClampDimension(int value, int fallback)
{
	return value >= 320 && value <= 16384 ? value : fallback;
}

int ClampQuality(int value)
{
	return std::max(0, std::min(3, value));
}

float CalculateUiScale(int uiScalePercent)
{
	// The optimized viewport may be much larger than 800x600, but the RCs,
	// fonts and hit boxes are still authored in logical client pixels.  A
	// setting of 100% must therefore remain exactly one logical pixel per CSS
	// pixel.  Scaling by the window's fit ratio made the whole interface 1.8x
	// larger at 1920x1080 and also leaked into legacy scene calculations that
	// use RenderDevice::m_fWidthRatio.
	return static_cast<float>(uiScalePercent) / 100.0f;
}

float AnchorFactor(unsigned char anchor)
{
	if (anchor == 2)
		return 1.0f;
	if (anchor == 1)
		return 0.5f;
	return 0.0f;
}

bool KeepAuthoredSceneCompositionTogether()
{
	if (!g_pCurrentScene)
		return false;

	// LoginScene*.bin is composed from several top-level image slices and
	// top-level controls.  Anchoring each slice independently tears the
	// original 800x600 composition apart on a wide viewport.  These historical
	// scenes do not have their original scene classes in this checkout, so keep
	// the complete official RC canvas together and centre it as one unit.
	return g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_LOGIN ||
		g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_CREATE_ACCOUNT;
}

bool IsFullBleedShellBackground(const SControl* control)
{
	if (!control || !g_pCurrentScene || control->m_eCtrlType != CONTROL_TYPE::CTRL_TYPE_PANEL)
		return false;

	if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_LOGIN)
		return control->m_dwControlID >= 305 && control->m_dwControlID <= 312;

	return g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_CREATE_ACCOUNT &&
		control->m_dwControlID == 305;
}

void ApplyFullBleedShellBackground(
	SControl* control,
	const WydViewportMetrics& viewport)
{
	if (!control || !control->m_bOptimizedCoverBackground || !g_pCurrentScene)
		return;

	float sourceX = 0.0f;
	float sourceY = 0.0f;
	float sourceWidth = 798.0f;
	float sourceHeight = 600.0f;
	if (g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_CREATE_ACCOUNT)
	{
		sourceY = 100.0f;
		sourceWidth = 800.0f;
		sourceHeight = 400.0f;
	}

	const float coverScale = std::max(
		static_cast<float>(viewport.cssWidth) / sourceWidth,
		static_cast<float>(viewport.cssHeight) / sourceHeight);
	const float offsetX =
		(static_cast<float>(viewport.cssWidth) - sourceWidth * coverScale) * 0.5f -
		sourceX * coverScale;
	const float offsetY =
		(static_cast<float>(viewport.cssHeight) - sourceHeight * coverScale) * 0.5f -
		sourceY * coverScale;

	control->m_nPosX = offsetX + control->m_fOptimizedAuthoredX * coverScale;
	control->m_nPosY = offsetY + control->m_fOptimizedAuthoredY * coverScale;
	control->m_nWidth = control->m_fOptimizedAuthoredWidth * coverScale;
	control->m_nHeight = control->m_fOptimizedAuthoredHeight * coverScale;
}

unsigned char ClassifyHorizontal(float center)
{
	if (center < 320.0f)
		return 0;
	if (center > 480.0f)
		return 2;
	return 1;
}

unsigned char ClassifyVertical(float center)
{
	if (center < 240.0f)
		return 0;
	if (center > 360.0f)
		return 2;
	return 1;
}

void ScaleControlChildren(SControl* parent, float scaleRatio)
{
	if (!parent || scaleRatio == 1.0f)
		return;

	for (auto* node = parent->m_pDown; node; node = node->m_pNextLink)
	{
		auto* control = static_cast<SControl*>(node);
		control->m_nPosX *= scaleRatio;
		control->m_nPosY *= scaleRatio;
		control->m_nWidth *= scaleRatio;
		control->m_nHeight *= scaleRatio;
		ScaleControlChildren(control, scaleRatio);
	}
}

void RelayoutRootControl(
	SControl* control,
	const WydViewportMetrics& previous,
	const WydViewportMetrics& current)
{
	if (!control)
		return;
	if (control->m_bOptimizedCoverBackground)
	{
		ApplyFullBleedShellBackground(control, current);
		return;
	}

	const float previousScale = std::max(0.001f, previous.uiScale);
	const float currentScale = std::max(0.001f, current.uiScale);
	const float ratio = currentScale / previousScale;
	const float previousExtraWidth =
		static_cast<float>(previous.cssWidth) - 800.0f * previousScale;
	const float previousExtraHeight =
		static_cast<float>(previous.cssHeight) - 600.0f * previousScale;
	const float currentExtraWidth =
		static_cast<float>(current.cssWidth) - 800.0f * currentScale;
	const float currentExtraHeight =
		static_cast<float>(current.cssHeight) - 600.0f * currentScale;

	const float previousOffsetX =
		previousExtraWidth * AnchorFactor(control->m_cOptimizedAnchorX);
	const float previousOffsetY =
		previousExtraHeight * AnchorFactor(control->m_cOptimizedAnchorY);
	const float currentOffsetX =
		currentExtraWidth * AnchorFactor(control->m_cOptimizedAnchorX);
	const float currentOffsetY =
		currentExtraHeight * AnchorFactor(control->m_cOptimizedAnchorY);

	control->m_nPosX =
		(control->m_nPosX - previousOffsetX) * ratio + currentOffsetX;
	control->m_nPosY =
		(control->m_nPosY - previousOffsetY) * ratio + currentOffsetY;
	control->m_nWidth *= ratio;
	control->m_nHeight *= ratio;
	ScaleControlChildren(control, ratio);
}

void ApplyRuntimeViewport(const WydViewportMetrics& previous)
{
	if (!g_pApp || !g_pDevice)
		return;

	g_pApp->m_dwScreenWidth = static_cast<unsigned int>(g_viewport.cssWidth);
	g_pApp->m_dwScreenHeight = static_cast<unsigned int>(g_viewport.cssHeight);
	g_pDevice->m_dwScreenWidth = static_cast<unsigned int>(g_viewport.cssWidth);
	g_pDevice->m_dwScreenHeight = static_cast<unsigned int>(g_viewport.cssHeight);
	g_pDevice->m_d3dsdBackBuffer.Width = static_cast<unsigned int>(g_viewport.cssWidth);
	g_pDevice->m_d3dsdBackBuffer.Height = static_cast<unsigned int>(g_viewport.cssHeight);
	g_pDevice->m_d3dpp.BackBufferWidth = static_cast<unsigned int>(g_viewport.cssWidth);
	g_pDevice->m_d3dpp.BackBufferHeight = static_cast<unsigned int>(g_viewport.cssHeight);
	RenderDevice::m_fWidthRatio = g_viewport.uiScale;
	RenderDevice::m_fHeightRatio = g_viewport.uiScale;

	if (g_pCurrentScene && g_pCurrentScene->m_pControlContainer)
	{
		OpenWydOptimizedRelayoutControls(
			g_pCurrentScene->m_pControlContainer,
			previous,
			g_viewport);
	}

	g_pDevice->SetViewPort(0, 0, g_viewport.cssWidth, g_viewport.cssHeight);
	g_pDevice->SetProjectionMatrix();
}
}

bool OpenWydOptimizedEnabled()
{
	return g_displayMode == WydDisplayMode::Optimized;
}

WydQualityProfile OpenWydOptimizedQuality()
{
	return g_qualityProfile;
}

const WydViewportMetrics& OpenWydOptimizedViewport()
{
	return g_viewport;
}

float OpenWydOptimizedUiScale()
{
	return OpenWydOptimizedEnabled() ? g_viewport.uiScale : 1.0f;
}

void OpenWydOptimizedConfigureRootControl(SControl* control)
{
	if (!OpenWydOptimizedEnabled() || !control || control->m_bOptimizedRootLayout)
		return;

	const float scale = std::max(0.001f, g_viewport.uiScale);
	control->m_fOptimizedAuthoredX = control->m_nPosX / scale;
	control->m_fOptimizedAuthoredY = control->m_nPosY / scale;
	control->m_fOptimizedAuthoredWidth = control->m_nWidth / scale;
	control->m_fOptimizedAuthoredHeight = control->m_nHeight / scale;
	control->m_bOptimizedCoverBackground = IsFullBleedShellBackground(control) ? 1 : 0;
	if (control->m_bOptimizedCoverBackground)
	{
		control->m_cOptimizedAnchorX = 1;
		control->m_cOptimizedAnchorY = 1;
		control->m_bOptimizedRootLayout = 1;
		ApplyFullBleedShellBackground(control, g_viewport);
		return;
	}
	if (KeepAuthoredSceneCompositionTogether())
	{
		control->m_cOptimizedAnchorX = 1;
		control->m_cOptimizedAnchorY = 1;
	}
	else
	{
		const float centerX = control->m_nPosX / scale + control->m_nWidth / scale * 0.5f;
		const float centerY = control->m_nPosY / scale + control->m_nHeight / scale * 0.5f;
		control->m_cOptimizedAnchorX = ClassifyHorizontal(centerX);
		control->m_cOptimizedAnchorY = ClassifyVertical(centerY);
	}
	control->m_bOptimizedRootLayout = 1;

	const float extraWidth = static_cast<float>(g_viewport.cssWidth) - 800.0f * scale;
	const float extraHeight = static_cast<float>(g_viewport.cssHeight) - 600.0f * scale;
	control->m_nPosX += extraWidth * AnchorFactor(control->m_cOptimizedAnchorX);
	control->m_nPosY += extraHeight * AnchorFactor(control->m_cOptimizedAnchorY);
}

void OpenWydOptimizedConfigureCenteredControl(SControl* control)
{
	if (!OpenWydOptimizedEnabled() || !control || control->m_bOptimizedRootLayout)
		return;

	const float scale = std::max(0.001f, g_viewport.uiScale);
	const float authoredCenterY =
		control->m_nPosY / scale + control->m_nHeight / scale * 0.5f;
	control->m_cOptimizedAnchorX = 1;
	control->m_cOptimizedAnchorY = ClassifyVertical(authoredCenterY);
	control->m_bOptimizedRootLayout = 1;

	const float extraHeight = static_cast<float>(g_viewport.cssHeight) - 600.0f * scale;
	control->m_nPosX =
		(static_cast<float>(g_viewport.cssWidth) - control->m_nWidth) * 0.5f;
	control->m_nPosY += extraHeight * AnchorFactor(control->m_cOptimizedAnchorY);
}

void OpenWydOptimizedRelayoutControls(
	SControlContainer* controls,
	const WydViewportMetrics& previous,
	const WydViewportMetrics& current)
{
	if (!controls || !controls->m_pControlRoot)
		return;

	for (auto* node = controls->m_pControlRoot->m_pDown; node; node = node->m_pNextLink)
	{
		auto* control = static_cast<SControl*>(node);
		if (!control->m_bOptimizedRootLayout)
			OpenWydOptimizedConfigureRootControl(control);
		RelayoutRootControl(control, previous, current);
	}

	if (controls->m_pCursor)
	{
		const float ratio = current.uiScale / std::max(0.001f, previous.uiScale);
		controls->m_pCursor->m_nWidth *= ratio;
		controls->m_pCursor->m_nHeight *= ratio;
	}
}

extern "C" int wyd_configure_optimized_view(
	int enabled,
	int quality,
	int cssWidth,
	int cssHeight,
	int backingWidth,
	int backingHeight,
	int uiScalePercent,
	int worldScalePercent)
{
	const WydViewportMetrics previous = g_viewport;
	g_displayMode = enabled ? WydDisplayMode::Optimized : WydDisplayMode::Legacy;
	g_qualityProfile = static_cast<WydQualityProfile>(ClampQuality(quality));
	g_viewport.cssWidth = ClampDimension(cssWidth, 800);
	g_viewport.cssHeight = ClampDimension(cssHeight, 600);
	g_viewport.backingWidth = ClampDimension(backingWidth, g_viewport.cssWidth);
	g_viewport.backingHeight = ClampDimension(backingHeight, g_viewport.cssHeight);
	// Optimized may compact the official UI on a small viewport, but it never
	// enlarges the authored font/control metrics.
	g_viewport.uiScalePercent = std::max(80, std::min(100, uiScalePercent));
	g_viewport.worldScalePercent = std::max(50, std::min(100, worldScalePercent));
	g_viewport.worldScale = static_cast<float>(g_viewport.worldScalePercent) / 100.0f;
	g_viewport.uiScale = OpenWydOptimizedEnabled()
		? CalculateUiScale(g_viewport.uiScalePercent)
		: static_cast<float>(g_viewport.cssWidth) / 800.0f;

	if (OpenWydOptimizedEnabled())
		ApplyRuntimeViewport(previous);

	return 1;
}

extern "C" int wyd_optimized_view_enabled()
{
	return OpenWydOptimizedEnabled() ? 1 : 0;
}

extern "C" int wyd_optimized_quality_profile()
{
	return static_cast<int>(g_qualityProfile);
}

extern "C" int wyd_optimized_css_width()
{
	return g_viewport.cssWidth;
}

extern "C" int wyd_optimized_css_height()
{
	return g_viewport.cssHeight;
}

extern "C" int wyd_optimized_backing_width()
{
	return g_viewport.backingWidth;
}

extern "C" int wyd_optimized_backing_height()
{
	return g_viewport.backingHeight;
}

extern "C" int wyd_optimized_ui_scale_percent()
{
	return g_viewport.uiScalePercent;
}

extern "C" float wyd_optimized_ui_scale()
{
	return g_viewport.uiScale;
}

extern "C" float wyd_optimized_world_scale()
{
	return g_viewport.worldScale;
}
