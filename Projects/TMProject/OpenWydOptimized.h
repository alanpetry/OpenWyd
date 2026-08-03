#pragma once

class SControl;
class SControlContainer;

enum class WydDisplayMode : int
{
	Legacy = 0,
	Optimized = 1,
};

enum class WydQualityProfile : int
{
	Auto = 0,
	Performance = 1,
	Quality = 2,
	Maximum = 3,
};

struct WydViewportMetrics
{
	int cssWidth;
	int cssHeight;
	int backingWidth;
	int backingHeight;
	int uiScalePercent;
	float uiScale;
	int worldScalePercent;
	float worldScale;
};

bool OpenWydOptimizedEnabled();
WydQualityProfile OpenWydOptimizedQuality();
const WydViewportMetrics& OpenWydOptimizedViewport();
float OpenWydOptimizedUiScale();
void OpenWydOptimizedConfigureRootControl(SControl* control);
void OpenWydOptimizedConfigureCenteredControl(SControl* control);
void OpenWydOptimizedRelayoutControls(
	SControlContainer* controls,
	const WydViewportMetrics& previous,
	const WydViewportMetrics& current);

extern "C"
{
	int wyd_configure_optimized_view(
		int enabled,
		int quality,
		int cssWidth,
		int cssHeight,
		int backingWidth,
		int backingHeight,
		int uiScalePercent,
		int worldScalePercent);
	int wyd_optimized_view_enabled();
	int wyd_optimized_quality_profile();
	int wyd_optimized_css_width();
	int wyd_optimized_css_height();
	int wyd_optimized_backing_width();
	int wyd_optimized_backing_height();
	int wyd_optimized_ui_scale_percent();
	float wyd_optimized_ui_scale();
	float wyd_optimized_world_scale();
}
