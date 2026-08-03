#include "pch.h"

#include "OpenWydNativeRenderer.h"

#include <atomic>
#include <cstring>
#include <vector>

namespace
{
WydRendererBackend g_backend = WydRendererBackend::LegacyD3D9Bridge;
WydRenderPass g_pass = WydRenderPass::Unknown;
std::atomic<std::uint64_t> g_nextGeometry{1};
std::uint64_t g_nextFrameId = 1;
std::vector<WydRenderCommand> g_commands;
WydNativeRendererFrameStats g_current{};
WydNativeRendererFrameStats g_last{};

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(std::uint64_t* hash, const void* bytes, std::size_t size)
{
	if (!hash || !bytes)
		return;
	const auto* cursor = static_cast<const unsigned char*>(bytes);
	for (std::size_t index = 0; index < size; ++index)
	{
		*hash ^= cursor[index];
		*hash *= kFnvPrime;
	}
}

void HashCommand(std::uint64_t* hash, const WydRenderCommand& command)
{
	const std::uint8_t pass = static_cast<std::uint8_t>(command.pass);
	HashBytes(hash, &pass, sizeof(pass));
	HashBytes(hash, &command.geometry, sizeof(command.geometry));
	HashBytes(hash, &command.indexGeometry, sizeof(command.indexGeometry));
	HashBytes(hash, &command.material, sizeof(command.material));
	HashBytes(hash, &command.world, sizeof(command.world));
	HashBytes(hash, &command.view, sizeof(command.view));
	HashBytes(hash, &command.projection, sizeof(command.projection));
	HashBytes(hash, &command.skeleton, sizeof(command.skeleton));
	HashBytes(hash, &command.geometryGeneration, sizeof(command.geometryGeneration));
	HashBytes(hash, &command.indexGeneration, sizeof(command.indexGeneration));
	HashBytes(hash, &command.firstIndex, sizeof(command.firstIndex));
	HashBytes(hash, &command.indexCount, sizeof(command.indexCount));
	HashBytes(hash, &command.vertexStride, sizeof(command.vertexStride));
	HashBytes(hash, &command.originalDrawSerial, sizeof(command.originalDrawSerial));
	HashBytes(hash, &command.sceneType, sizeof(command.sceneType));
	HashBytes(hash, &command.objectIdentity, sizeof(command.objectIdentity));
}
}

bool OpenWydNativeRendererSetBackend(WydRendererBackend backend)
{
#if !defined(__EMSCRIPTEN__)
	if (backend == WydRendererBackend::NativeWebGL2)
		return false;
#endif
	g_backend = backend;
	return true;
}

WydRendererBackend OpenWydNativeRendererBackend()
{
	return g_backend;
}

bool OpenWydNativeRendererEnabled()
{
	return g_backend == WydRendererBackend::NativeWebGL2;
}

WydGeometryHandle OpenWydNativeRendererAllocateGeometry()
{
	return g_nextGeometry.fetch_add(1, std::memory_order_relaxed);
}

void OpenWydNativeRendererBeginFrame()
{
	if (!OpenWydNativeRendererEnabled())
		return;
	g_commands.clear();
	if (g_commands.capacity() < 8192)
		g_commands.reserve(8192);
	g_current = {};
	g_current.frameId = g_nextFrameId++;
	g_current.streamHash = kFnvOffset;
	g_pass = WydRenderPass::Unknown;
}

void OpenWydNativeRendererSetPass(WydRenderPass pass)
{
	if (OpenWydNativeRendererEnabled())
		g_pass = pass;
}

WydRenderPass OpenWydNativeRendererPass()
{
	return g_pass;
}

void OpenWydNativeRendererRecord(const WydRenderCommand& source, bool supported)
{
	if (!OpenWydNativeRendererEnabled())
		return;
	WydRenderCommand command = source;
	if (command.pass == WydRenderPass::Unknown)
		command.pass = g_pass;
	g_commands.push_back(command);
	HashCommand(&g_current.streamHash, command);
	++g_current.commandCount;
	if (supported)
		++g_current.supportedDraws;
	else
		++g_current.fallbackDraws;
}

void OpenWydNativeRendererPromoteLastCommand()
{
	if (!OpenWydNativeRendererEnabled() || g_commands.empty())
		return;
	if (g_current.fallbackDraws > 0)
		--g_current.fallbackDraws;
	++g_current.supportedDraws;
}

void OpenWydNativeRendererEndFrame()
{
	if (!OpenWydNativeRendererEnabled())
		return;
	g_last = g_current;
}

const WydNativeRendererFrameStats& OpenWydNativeRendererLastFrameStats()
{
	return g_last;
}

const WydRenderCommand* NativeCommandAt(unsigned int index)
{
	return index < g_commands.size() ? &g_commands[index] : nullptr;
}

extern "C" int wyd_renderer_set_backend(int backend)
{
	const WydRendererBackend requested = backend == 1
		? WydRendererBackend::NativeWebGL2
		: WydRendererBackend::LegacyD3D9Bridge;
	return OpenWydNativeRendererSetBackend(requested) ? 1 : 0;
}

extern "C" int wyd_renderer_backend()
{
	return static_cast<int>(OpenWydNativeRendererBackend());
}

extern "C" unsigned int wyd_native_renderer_enabled()
{
	return OpenWydNativeRendererEnabled() ? 1u : 0u;
}

extern "C" unsigned int wyd_native_renderer_last_command_count()
{
	return OpenWydNativeRendererLastFrameStats().commandCount;
}

extern "C" unsigned int wyd_native_renderer_last_supported_draws()
{
	return OpenWydNativeRendererLastFrameStats().supportedDraws;
}

extern "C" unsigned int wyd_native_renderer_last_fallback_draws()
{
	return OpenWydNativeRendererLastFrameStats().fallbackDraws;
}

extern "C" unsigned int wyd_native_renderer_last_frame_id_low()
{
	return static_cast<unsigned int>(OpenWydNativeRendererLastFrameStats().frameId);
}

extern "C" unsigned int wyd_native_renderer_last_stream_hash_low()
{
	return static_cast<unsigned int>(OpenWydNativeRendererLastFrameStats().streamHash);
}

extern "C" unsigned int wyd_native_renderer_command_pass(unsigned int index)
{
	const auto* command = NativeCommandAt(index);
	return command ? static_cast<unsigned int>(command->pass) : 0u;
}

extern "C" unsigned int wyd_native_renderer_command_fvf(unsigned int index)
{
	const auto* command = NativeCommandAt(index);
	return command ? command->material.fvf : 0u;
}

extern "C" unsigned int wyd_native_renderer_command_vs_hash_low(unsigned int index)
{
	const auto* command = NativeCommandAt(index);
	return command ? static_cast<unsigned int>(command->material.vertexShaderHash) : 0u;
}

extern "C" unsigned int wyd_native_renderer_command_vs_hash_high(unsigned int index)
{
	const auto* command = NativeCommandAt(index);
	return command
		? static_cast<unsigned int>(command->material.vertexShaderHash >> 32u)
		: 0u;
}

extern "C" unsigned int wyd_native_renderer_command_ps_hash_low(unsigned int index)
{
	const auto* command = NativeCommandAt(index);
	return command ? static_cast<unsigned int>(command->material.pixelShaderHash) : 0u;
}

extern "C" unsigned int wyd_native_renderer_command_ps_hash_high(unsigned int index)
{
	const auto* command = NativeCommandAt(index);
	return command
		? static_cast<unsigned int>(command->material.pixelShaderHash >> 32u)
		: 0u;
}

#define WYD_NATIVE_COMMAND_FIELD_EXPORT(name, field) \
	extern "C" unsigned int name(unsigned int index) \
	{ \
		const auto* command = NativeCommandAt(index); \
		return command ? static_cast<unsigned int>(command->field) : 0u; \
	}

WYD_NATIVE_COMMAND_FIELD_EXPORT(
	wyd_native_renderer_command_blend, material.blend)
WYD_NATIVE_COMMAND_FIELD_EXPORT(
	wyd_native_renderer_command_depth, material.depth)
WYD_NATIVE_COMMAND_FIELD_EXPORT(
	wyd_native_renderer_command_raster, material.raster)
WYD_NATIVE_COMMAND_FIELD_EXPORT(
	wyd_native_renderer_command_texture_stages, material.textureStages)
WYD_NATIVE_COMMAND_FIELD_EXPORT(
	wyd_native_renderer_command_stride, vertexStride)
WYD_NATIVE_COMMAND_FIELD_EXPORT(
	wyd_native_renderer_command_index_count, indexCount)

#undef WYD_NATIVE_COMMAND_FIELD_EXPORT
