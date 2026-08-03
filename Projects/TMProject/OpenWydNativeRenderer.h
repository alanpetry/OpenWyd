#pragma once

#include <cstdint>
#include <string>

#include <d3dx9.h>

enum class WydRendererBackend : int
{
	LegacyD3D9Bridge = 0,
	NativeWebGL2 = 1,
};

enum class WydRenderPass : std::uint8_t
{
	Unknown = 0,
	Sky,
	OpaqueWorld,
	Vegetation,
	Actor,
	Water,
	Transparent,
	WorldText,
	Ui,
	Text,
};

using WydGeometryHandle = std::uint64_t;
using WydSkeletonPaletteHandle = std::uint64_t;

struct WydMaterialKey
{
	std::uint64_t vertexShaderHash = 0;
	std::uint64_t pixelShaderHash = 0;
	std::uint64_t texture0 = 0;
	std::uint64_t texture1 = 0;
	std::uint32_t fvf = 0;
	std::uint32_t blend = 0;
	std::uint32_t depth = 0;
	std::uint32_t raster = 0;
	std::uint32_t textureStages = 0;
};

struct WydRenderCommand
{
	WydRenderPass pass = WydRenderPass::Unknown;
	WydGeometryHandle geometry = 0;
	WydGeometryHandle indexGeometry = 0;
	WydMaterialKey material{};
	D3DXMATRIX world{};
	D3DXMATRIX view{};
	D3DXMATRIX projection{};
	WydSkeletonPaletteHandle skeleton = 0;
	std::uint32_t geometryGeneration = 0;
	std::uint32_t indexGeneration = 0;
	std::uint32_t firstIndex = 0;
	std::uint32_t indexCount = 0;
	std::uint32_t vertexStride = 0;
	std::uint32_t originalDrawSerial = 0;
	std::uint32_t sceneType = 0;
	std::uint32_t objectIdentity = 0;
};

struct WydRect
{
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;
};

struct WydFontStyle
{
	float logicalSize = 12.0f;
	std::uint32_t color = 0xFFFFFFFFu;
	std::uint32_t outlineColor = 0;
	std::uint16_t weight = 400;
	std::uint8_t italic = 0;
	std::uint8_t shadow = 0;
};

enum class WydTextAlignment : std::uint8_t
{
	Left = 0,
	Center,
	Right,
};

struct WydTextRun
{
	std::string cp1252Text;
	WydFontStyle style{};
	WydRect logicalRect{};
	WydTextAlignment alignment = WydTextAlignment::Left;
};

struct WydNativeRendererFrameStats
{
	std::uint64_t frameId = 0;
	std::uint64_t streamHash = 0;
	std::uint32_t commandCount = 0;
	std::uint32_t supportedDraws = 0;
	std::uint32_t fallbackDraws = 0;
};

bool OpenWydNativeRendererSetBackend(WydRendererBackend backend);
WydRendererBackend OpenWydNativeRendererBackend();
bool OpenWydNativeRendererEnabled();
WydGeometryHandle OpenWydNativeRendererAllocateGeometry();
void OpenWydNativeRendererBeginFrame();
void OpenWydNativeRendererSetPass(WydRenderPass pass);
WydRenderPass OpenWydNativeRendererPass();
void OpenWydNativeRendererRecord(const WydRenderCommand& command, bool supported);
void OpenWydNativeRendererPromoteLastCommand();
void OpenWydNativeRendererEndFrame();
const WydNativeRendererFrameStats& OpenWydNativeRendererLastFrameStats();

extern "C"
{
	int wyd_renderer_set_backend(int backend);
	int wyd_renderer_backend();
	unsigned int wyd_native_renderer_enabled();
	unsigned int wyd_native_renderer_last_command_count();
	unsigned int wyd_native_renderer_last_supported_draws();
	unsigned int wyd_native_renderer_last_fallback_draws();
	unsigned int wyd_native_renderer_last_frame_id_low();
	unsigned int wyd_native_renderer_last_stream_hash_low();
	unsigned int wyd_native_renderer_buffer_uploads();
	unsigned int wyd_native_renderer_buffer_upload_bytes_low();
	unsigned int wyd_native_renderer_resident_draws();
	unsigned int wyd_native_renderer_command_pass(unsigned int index);
	unsigned int wyd_native_renderer_command_fvf(unsigned int index);
	unsigned int wyd_native_renderer_command_vs_hash_low(unsigned int index);
	unsigned int wyd_native_renderer_command_vs_hash_high(unsigned int index);
	unsigned int wyd_native_renderer_command_ps_hash_low(unsigned int index);
	unsigned int wyd_native_renderer_command_ps_hash_high(unsigned int index);
	unsigned int wyd_native_renderer_command_blend(unsigned int index);
	unsigned int wyd_native_renderer_command_depth(unsigned int index);
	unsigned int wyd_native_renderer_command_raster(unsigned int index);
	unsigned int wyd_native_renderer_command_texture_stages(unsigned int index);
	unsigned int wyd_native_renderer_command_stride(unsigned int index);
	unsigned int wyd_native_renderer_command_index_count(unsigned int index);
}
