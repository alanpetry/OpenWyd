#include "pch.h"

#if defined(OPENWYD_COMPARE) && defined(_DEBUG) && !defined(__EMSCRIPTEN__)

// pch.h redirects these names for every comparison-build translation unit.
// This implementation needs the operating-system functions as its fallback.
#undef timeGetTime
#undef GetTickCount
#undef rand
#undef srand

#include "OpenWydCompare.h"
#include "TMGlobal.h"
#include "TMCamera.h"

#include <ShlObj.h>
#include <wincodec.h>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

extern "C" const char* wyd_socket_last_host();
extern "C" int wyd_socket_last_port();
extern "C" int wyd_socket_last_connect_result();
extern "C" int wyd_socket_last_error();
extern "C" unsigned int wyd_socket_bytes_sent();
extern "C" unsigned int wyd_socket_bytes_received();
extern "C" unsigned int wyd_socket_last_sent_opcode();
extern "C" unsigned int wyd_socket_last_recv_opcode();

namespace
{
	constexpr DWORD kPipeBufferBytes = 64u * 1024u;
	constexpr size_t kMaximumInputBytes = 64u * 1024u;
	constexpr unsigned int kInputProtocolVersion = 1;
	constexpr WPARAM kInjectedMouseMessage = static_cast<WPARAM>(0x80000000u);
	constexpr LPARAM kInjectedKeyMessage = static_cast<LPARAM>(0x02000000u);
	constexpr unsigned long long kMaximumControlledTime =
		static_cast<unsigned long long>((std::numeric_limits<DWORD>::max)());

	struct QueuedInputMessage
	{
		UINT message = 0;
		WPARAM wParam = 0;
		LPARAM lParam = 0;
	};

	struct CompareState
	{
		bool initialized = false;
		bool enabled = false;
		bool connected = false;
		bool shuttingDown = false;
		bool hasLastAcceptedFrame = false;
		bool stepPending = false;
		bool frameActive = false;
		bool fatalProtocolError = false;
		bool hasQueuedInputFrame = false;
		HANDLE pipe = INVALID_HANDLE_VALUE;
		HWND window = nullptr;
		unsigned int width = 0;
		unsigned int height = 0;
		unsigned long long pendingFrameId = 0;
		unsigned long long activeFrameId = 0;
		unsigned long long lastAcceptedFrameId = 0;
		unsigned long long queuedInputFrameId = 0;
		unsigned long long inputSequence = 0;
		unsigned long long activeInputLastSequence = 0;
		unsigned int queuedInputCount = 0;
		unsigned int activeInputCount = 0;
		DWORD pendingTimeMs = 0;
		DWORD activeTimeMs = 0;
		volatile LONG controlledTimeMs = 0;
		std::string pipePath;
		std::string artifactDirectory;
		std::string serverHostOverride;
		int serverPortOverride = 0;
		bool hasServerHostOverride = false;
		bool hasServerPortOverride = false;
		bool serverEndpointResolved = false;
		std::string resolvedServerHost;
		int resolvedServerPort = 0;
		const char* captureStage = "not_started";
		const char* captureEncoder = "none";
		HRESULT d3dxEncodeResult = S_OK;
		bool presentCapturePending = false;
		HRESULT pendingCaptureResult = S_OK;
		bool pendingSnapshotWritten = false;
		DWORD mouseButtonMask = 0;
		bool keyDown[256]{};
		bool injectedKeyDown[256]{};
		bool mouseHasPosition = false;
		LONG mouseX = 0;
		LONG mouseY = 0;
		LONG mouseDeltaX = 0;
		LONG mouseDeltaY = 0;
		LONG mouseWheel = 0;
		LONG lastConsumedMouseDeltaX = 0;
		LONG lastConsumedMouseDeltaY = 0;
		LONG lastConsumedMouseWheel = 0;
		BYTE injectedMouseButtons[8]{};
		std::string input;
		std::vector<QueuedInputMessage> queuedInputs;
	};

	CompareState g_compare;

	std::string ReadEnvironment(const char* name)
	{
		const DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
		if (required == 0)
			return {};

		std::string value(required, '\0');
		const DWORD written = GetEnvironmentVariableA(name, &value[0], required);
		if (written == 0 || written >= required)
			return {};

		value.resize(written);
		return value;
	}

	void ResetInjectedInputState()
	{
		g_compare.mouseButtonMask = 0;
		memset(g_compare.keyDown, 0, sizeof(g_compare.keyDown));
		memset(g_compare.injectedKeyDown, 0, sizeof(g_compare.injectedKeyDown));
		g_compare.mouseHasPosition = false;
		g_compare.mouseX = 0;
		g_compare.mouseY = 0;
		g_compare.mouseDeltaX = 0;
		g_compare.mouseDeltaY = 0;
		g_compare.mouseWheel = 0;
		g_compare.lastConsumedMouseDeltaX = 0;
		g_compare.lastConsumedMouseDeltaY = 0;
		g_compare.lastConsumedMouseWheel = 0;
		memset(
			g_compare.injectedMouseButtons,
			0,
			sizeof(g_compare.injectedMouseButtons));
	}

	bool StartsWith(const std::string& value, const char* prefix)
	{
		const size_t prefixLength = strlen(prefix);
		return value.size() >= prefixLength &&
			value.compare(0, prefixLength, prefix) == 0;
	}

	bool IsValidServerHost(const std::string& value)
	{
		if (value.empty() || value.size() >= 256)
			return false;

		for (const unsigned char character : value)
		{
			if (!isalnum(character) &&
				character != '.' &&
				character != '-' &&
				character != '_' &&
				character != ':' &&
				character != '[' &&
				character != ']')
			{
				return false;
			}
		}
		return true;
	}

	void ResetConnection()
	{
		if (g_compare.pipe != INVALID_HANDLE_VALUE && g_compare.connected)
			DisconnectNamedPipe(g_compare.pipe);

		g_compare.connected = false;
		g_compare.input.clear();
		g_compare.queuedInputs.clear();
		g_compare.hasQueuedInputFrame = false;
		g_compare.queuedInputCount = 0;
		g_compare.presentCapturePending = false;
		g_compare.pendingCaptureResult = S_OK;
		g_compare.pendingSnapshotWritten = false;
		ResetInjectedInputState();
	}

	bool SendLine(const std::string& line)
	{
		if (!g_compare.connected || g_compare.pipe == INVALID_HANDLE_VALUE)
			return false;

		DWORD written = 0;
		const BOOL result = WriteFile(
			g_compare.pipe,
			line.data(),
			static_cast<DWORD>(line.size()),
			&written,
			nullptr);
		if (!result || written != line.size())
		{
			ResetConnection();
			return false;
		}

		return true;
	}

	void SendError(const char* code)
	{
		std::ostringstream response;
		response << "ERROR " << code << "\n";
		SendLine(response.str());
	}

	bool ParseUnsigned(
		const std::string& token,
		unsigned long long maximum,
		unsigned long long& result)
	{
		if (token.empty() || token[0] == '-')
			return false;

		char* end = nullptr;
		errno = 0;
		const unsigned long long value = _strtoui64(token.c_str(), &end, 10);
		if (errno == ERANGE || end == token.c_str() || *end != '\0' || value > maximum)
			return false;

		result = value;
		return true;
	}

	bool QueueInputMessage(
		unsigned long long frameId,
		UINT message,
		WPARAM wParam,
		LPARAM lParam)
	{
		if (g_compare.stepPending || g_compare.frameActive)
		{
			SendError("input_after_step");
			return false;
		}
		if (g_compare.hasLastAcceptedFrame &&
			frameId <= g_compare.lastAcceptedFrameId)
		{
			SendError("input_frame_not_future");
			return false;
		}
		if (g_compare.hasQueuedInputFrame &&
			frameId != g_compare.queuedInputFrameId)
		{
			SendError("input_frame_mismatch");
			return false;
		}
		if (g_compare.inputSequence ==
			(std::numeric_limits<unsigned long long>::max)())
		{
			SendError("input_sequence_exhausted");
			return false;
		}
		if (!g_compare.window)
		{
			SendError("input_post_failed");
			return false;
		}

		g_compare.queuedInputs.push_back({ message, wParam, lParam });
		g_compare.hasQueuedInputFrame = true;
		g_compare.queuedInputFrameId = frameId;
		++g_compare.queuedInputCount;
		++g_compare.inputSequence;

		std::ostringstream response;
		response << "INPUT_QUEUED "
			<< g_compare.inputSequence << " "
			<< frameId << "\n";
		SendLine(response.str());
		return true;
	}

	void HandleInputCommand(std::istringstream& stream)
	{
		std::string versionToken;
		std::string frameToken;
		std::string kind;
		if (!(stream >> versionToken >> frameToken >> kind))
		{
			SendError("invalid_input");
			return;
		}

		unsigned long long version = 0;
		unsigned long long frameId = 0;
		if (!ParseUnsigned(versionToken, 0xFFFF, version) ||
			version != kInputProtocolVersion)
		{
			SendError("unsupported_input_version");
			return;
		}
		if (!ParseUnsigned(
			frameToken,
			(std::numeric_limits<unsigned long long>::max)(),
			frameId))
		{
			SendError("invalid_input_frame");
			return;
		}

		if (kind == "MOUSE_MOVE")
		{
			std::string xToken;
			std::string yToken;
			std::string extra;
			unsigned long long x = 0;
			unsigned long long y = 0;
			if (!(stream >> xToken >> yToken) ||
				(stream >> extra) ||
				!ParseUnsigned(xToken, 0xFFFF, x) ||
				!ParseUnsigned(yToken, 0xFFFF, y) ||
				x >= g_compare.width ||
				y >= g_compare.height)
			{
				SendError("invalid_mouse_coordinates");
				return;
			}

			QueueInputMessage(
				frameId,
				WM_MOUSEMOVE,
				static_cast<WPARAM>(g_compare.mouseButtonMask) |
					kInjectedMouseMessage,
				MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y)));
			return;
		}

		if (kind == "MOUSE_DOWN" || kind == "MOUSE_UP")
		{
			std::string button;
			std::string xToken;
			std::string yToken;
			std::string extra;
			unsigned long long x = 0;
			unsigned long long y = 0;
			if (!(stream >> button >> xToken >> yToken) ||
				(stream >> extra) ||
				!ParseUnsigned(xToken, 0xFFFF, x) ||
				!ParseUnsigned(yToken, 0xFFFF, y) ||
				x >= g_compare.width ||
				y >= g_compare.height)
			{
				SendError("invalid_mouse_input");
				return;
			}

			UINT message = 0;
			DWORD buttonMask = 0;
			if (button == "LEFT")
			{
				message = kind == "MOUSE_DOWN"
					? WM_LBUTTONDOWN
					: WM_LBUTTONUP;
				buttonMask = MK_LBUTTON;
			}
			else if (button == "RIGHT")
			{
				message = kind == "MOUSE_DOWN"
					? WM_RBUTTONDOWN
					: WM_RBUTTONUP;
				buttonMask = MK_RBUTTON;
			}
			else
			{
				SendError("invalid_mouse_button");
				return;
			}

			const bool down = kind == "MOUSE_DOWN";
			const bool alreadyDown =
				(g_compare.mouseButtonMask & buttonMask) != 0;
			if (down == alreadyDown)
			{
				SendError(
					down
						? "mouse_button_already_down"
						: "mouse_button_not_down");
				return;
			}

			const DWORD nextMask = down
				? g_compare.mouseButtonMask | buttonMask
				: g_compare.mouseButtonMask & ~buttonMask;
			const WPARAM messageMask =
				static_cast<WPARAM>(nextMask) | kInjectedMouseMessage;
			if (QueueInputMessage(
				frameId,
				message,
				messageMask,
				MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y))))
			{
				g_compare.mouseButtonMask = nextMask;
			}
			return;
		}

		if (kind == "KEY_DOWN" || kind == "KEY_UP")
		{
			std::string keyToken;
			std::string extra;
			unsigned long long key = 0;
			if (!(stream >> keyToken) ||
				(stream >> extra) ||
				!ParseUnsigned(keyToken, 254, key) ||
				key == 0)
			{
				SendError("invalid_virtual_key");
				return;
			}

			const bool down = kind == "KEY_DOWN";
			if (down == g_compare.keyDown[key])
			{
				SendError(
					down ? "key_already_down" : "key_not_down");
				return;
			}

			const LPARAM keyLParam = down
				? static_cast<LPARAM>(1)
				: static_cast<LPARAM>(0xC0000001u);
			if (QueueInputMessage(
				frameId,
				down ? WM_KEYDOWN : WM_KEYUP,
				static_cast<WPARAM>(key),
				keyLParam | kInjectedKeyMessage))
			{
				g_compare.keyDown[key] = down;
			}
			return;
		}

		if (kind == "CHAR")
		{
			std::string characterToken;
			std::string extra;
			unsigned long long character = 0;
			if (!(stream >> characterToken) ||
				(stream >> extra) ||
				!ParseUnsigned(characterToken, 255, character) ||
				character == 0)
			{
				SendError("invalid_cp1252_character");
				return;
			}

			QueueInputMessage(
				frameId,
				WM_CHAR,
				static_cast<WPARAM>(character),
				static_cast<LPARAM>(1) | kInjectedKeyMessage);
			return;
		}

		SendError("unknown_input_kind");
	}

	void HandleCommand(const std::string& originalLine)
	{
		std::string line = originalLine;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			return;

		std::istringstream stream(line);
		std::string command;
		stream >> command;

		if (command == "PING")
		{
			std::string extra;
			if (stream >> extra)
				SendError("invalid_ping");
			else
				SendLine("PONG\n");
			return;
		}

		if (command == "CLOSE")
		{
			std::string extra;
			if (stream >> extra)
			{
				SendError("invalid_close");
				return;
			}

			SendLine("CLOSING\n");
			if (g_compare.window)
				PostMessageA(g_compare.window, WM_CLOSE, 0, 0);
			return;
		}

		if (g_compare.fatalProtocolError)
		{
			SendError("fatal_protocol_state");
			return;
		}

		if (command == "INPUT")
		{
			HandleInputCommand(stream);
			return;
		}

		if (command == "RANDOM_SEED")
		{
			std::string seedToken;
			std::string extra;
			unsigned long long seed = 0;
			if (!(stream >> seedToken) ||
				(stream >> extra) ||
				!ParseUnsigned(seedToken, 0xFFFFFFFFull, seed))
			{
				SendError("invalid_random_seed");
				return;
			}
			if (g_compare.stepPending || g_compare.frameActive)
			{
				SendError("random_seed_while_busy");
				return;
			}

			// Determinism must begin before NewApp construction. Accept this
			// command as a verification handshake only; arming here would
			// erase evidence of pre-boot random consumption and could leave
			// already-created visual state divergent.
			if (!OpenWydCompareRandomIsArmed())
			{
				SendError("random_seed_not_prearmed");
				return;
			}
			if (OpenWydCompareRandomConfiguredSeed() !=
				static_cast<unsigned int>(seed))
			{
				SendError("random_seed_conflict");
				return;
			}
			std::ostringstream response;
			response << "RANDOM_SEEDED " << seed << "\n";
			SendLine(response.str());
			return;
		}

		if (command != "STEP")
		{
			SendError("unknown_command");
			return;
		}

		std::string frameToken;
		std::string timeToken;
		std::string extra;
		if (!(stream >> frameToken >> timeToken) || (stream >> extra))
		{
			SendError("invalid_step");
			return;
		}

		unsigned long long frameId = 0;
		unsigned long long timeMs = 0;
			if (!ParseUnsigned(
				frameToken,
				(std::numeric_limits<unsigned long long>::max)(),
				frameId) ||
			!ParseUnsigned(timeToken, kMaximumControlledTime, timeMs))
		{
			SendError("invalid_step");
			return;
		}

		if ((g_compare.hasLastAcceptedFrame && frameId <= g_compare.lastAcceptedFrameId) ||
			g_compare.stepPending ||
			g_compare.frameActive)
		{
			SendError("step_not_monotonic_or_busy");
			return;
		}
		if (g_compare.hasQueuedInputFrame &&
			frameId != g_compare.queuedInputFrameId)
		{
			SendError("step_input_frame_mismatch");
			return;
		}

		const unsigned int pendingInputCount = g_compare.queuedInputCount;
		const unsigned long long pendingInputLastSequence =
			g_compare.queuedInputCount != 0
				? g_compare.inputSequence
				: 0;

		for (const QueuedInputMessage& input : g_compare.queuedInputs)
		{
			if (!PostMessageA(
				g_compare.window,
				input.message,
				input.wParam,
				input.lParam))
			{
				// PostMessage has no rollback operation. Once any earlier
				// message is in the HWND queue, retrying this frame could
				// duplicate only a prefix of the input stream. Make the
				// protocol terminal instead of exposing a recoverable-looking
				// state with an indeterminate input sequence.
				g_compare.fatalProtocolError = true;
				g_compare.queuedInputs.clear();
				g_compare.hasQueuedInputFrame = false;
				g_compare.queuedInputCount = 0;
				g_compare.activeInputCount = 0;
				g_compare.activeInputLastSequence = 0;
				SendError("input_post_failed_fatal");
				if (IsWindow(g_compare.window))
					PostMessageA(g_compare.window, WM_CLOSE, 0, 0);
				return;
			}
		}

		// Posted messages cannot run on this thread until HandleCommand
		// returns. Commit the controlled clock and frame only after every
		// post succeeded, while still guaranteeing that every input handler
		// observes this step's time.
		g_compare.hasLastAcceptedFrame = true;
		g_compare.lastAcceptedFrameId = frameId;
		g_compare.pendingFrameId = frameId;
		g_compare.pendingTimeMs = static_cast<DWORD>(timeMs);
		g_compare.activeInputCount = pendingInputCount;
		g_compare.activeInputLastSequence = pendingInputLastSequence;
		g_compare.hasQueuedInputFrame = false;
		g_compare.queuedInputCount = 0;
		InterlockedExchange(
			&g_compare.controlledTimeMs,
			static_cast<LONG>(g_compare.pendingTimeMs));
		g_compare.queuedInputs.clear();
		g_compare.stepPending = true;

		std::ostringstream response;
		response << "STEP_ACCEPTED " << frameId << " " << timeMs << "\n";
		SendLine(response.str());
	}

	void TryAcceptConnection()
	{
		if (!g_compare.enabled || g_compare.connected ||
			g_compare.pipe == INVALID_HANDLE_VALUE)
		{
			return;
		}

		if (ConnectNamedPipe(g_compare.pipe, nullptr))
		{
			g_compare.connected = true;
		}
		else
		{
			const DWORD error = GetLastError();
			if (error == ERROR_PIPE_CONNECTED)
				g_compare.connected = true;
			else if (error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA)
				OutputDebugStringA("OpenWydCompare: ConnectNamedPipe failed.\n");
		}

		if (!g_compare.connected)
			return;

		std::ostringstream ready;
		ready << "READY 1 "
			<< GetCurrentProcessId() << " "
			<< g_compare.width << " "
			<< g_compare.height << " "
			<< (g_compare.artifactDirectory.empty() ? 0 : 1)
			<< "\n";
		SendLine(ready.str());
	}

	void ReadCommands()
	{
		if (!g_compare.connected || g_compare.pipe == INVALID_HANDLE_VALUE)
			return;

		for (;;)
		{
			DWORD available = 0;
			if (!PeekNamedPipe(g_compare.pipe, nullptr, 0, nullptr, &available, nullptr))
			{
				ResetConnection();
				return;
			}
			if (available == 0)
				break;

			char buffer[2048];
			const DWORD requested =
				available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
			DWORD received = 0;
			if (!ReadFile(g_compare.pipe, buffer, requested, &received, nullptr))
			{
				ResetConnection();
				return;
			}
			if (received == 0)
				break;

			g_compare.input.append(buffer, received);
			if (g_compare.input.size() > kMaximumInputBytes)
			{
				SendError("input_too_large");
				g_compare.input.clear();
				break;
			}

			for (;;)
			{
				const size_t newline = g_compare.input.find('\n');
				if (newline == std::string::npos)
					break;

				const std::string line = g_compare.input.substr(0, newline);
				g_compare.input.erase(0, newline + 1);
				HandleCommand(line);
				if (!g_compare.connected)
					return;
			}
		}
	}

	std::string FrameStem(unsigned long long frameId)
	{
		std::ostringstream stem;
		stem << "frame_" << std::setfill('0') << std::setw(20) << frameId;
		return stem.str();
	}

	std::string JoinPath(const std::string& directory, const std::string& filename)
	{
		if (directory.empty())
			return filename;
		const char last = directory.back();
		if (last == '\\' || last == '/')
			return directory + filename;
		return directory + "\\" + filename;
	}

	const char* D3DFormatName(D3DFORMAT format)
	{
		switch (format)
		{
		case D3DFMT_A8R8G8B8:
			return "D3DFMT_A8R8G8B8";
		case D3DFMT_X8R8G8B8:
			return "D3DFMT_X8R8G8B8";
		case D3DFMT_R5G6B5:
			return "D3DFMT_R5G6B5";
		case D3DFMT_X1R5G5B5:
			return "D3DFMT_X1R5G5B5";
		default:
			return "D3DFMT_OTHER";
		}
	}

	void AppendJsonString(std::ostringstream& json, const char* value)
	{
		static constexpr char hex[] = "0123456789ABCDEF";
		json << "\"";
		if (value)
		{
			for (const unsigned char* current =
				reinterpret_cast<const unsigned char*>(value);
				*current;
				++current)
			{
				switch (*current)
				{
				case '\"':
					json << "\\\"";
					break;
				case '\\':
					json << "\\\\";
					break;
				case '\b':
					json << "\\b";
					break;
				case '\f':
					json << "\\f";
					break;
				case '\n':
					json << "\\n";
					break;
				case '\r':
					json << "\\r";
					break;
				case '\t':
					json << "\\t";
					break;
				default:
					if (*current < 0x20)
					{
						json << "\\u00"
							<< hex[*current >> 4]
							<< hex[*current & 0x0F];
					}
					else
					{
						json << static_cast<char>(*current);
					}
					break;
				}
			}
		}
		json << "\"";
	}

	void AppendJsonString(std::ostringstream& json, const std::string& value)
	{
		AppendJsonString(json, value.c_str());
	}

	void AppendFloat(std::ostringstream& json, float value)
	{
		if (std::isfinite(value))
			json << std::setprecision(9) << value;
		else
			json << "null";
	}

	void AppendVector(std::ostringstream& json, const TMVector3& value)
	{
		json << "[";
		AppendFloat(json, value.x);
		json << ",";
		AppendFloat(json, value.y);
		json << ",";
		AppendFloat(json, value.z);
		json << "]";
	}

	void AppendMatrix(std::ostringstream& json, const D3DXMATRIX& matrix)
	{
		json << "[";
		const float* values = &matrix._11;
		for (int index = 0; index < 16; ++index)
		{
			if (index != 0)
				json << ",";
			AppendFloat(json, values[index]);
		}
		json << "]";
	}

	void AppendTransform(
		std::ostringstream& json,
		IDirect3DDevice9* device,
		D3DTRANSFORMSTATETYPE transform)
	{
		D3DXMATRIX matrix{};
		if (device && SUCCEEDED(device->GetTransform(transform, &matrix)))
			AppendMatrix(json, matrix);
		else
			json << "null";
	}

	void AppendRenderState(
		std::ostringstream& json,
		IDirect3DDevice9* device,
		D3DRENDERSTATETYPE state)
	{
		DWORD value = 0;
		if (device && SUCCEEDED(device->GetRenderState(state, &value)))
			json << value;
		else
			json << "null";
	}

	template <typename Interface>
	void ReleaseInterface(Interface*& value)
	{
		if (value)
		{
			value->Release();
			value = nullptr;
		}
	}

	HRESULT SaveSurfaceToPngWithWic(
		IDirect3DSurface9* surface,
		unsigned int width,
		unsigned int height,
		const std::string& path)
	{
		if (!surface || width == 0 || height == 0 || path.empty())
			return E_INVALIDARG;

		const unsigned long long stride64 =
			static_cast<unsigned long long>(width) * 4ull;
		const unsigned long long size64 =
			stride64 * static_cast<unsigned long long>(height);
		if (stride64 > UINT_MAX || size64 > UINT_MAX)
			return E_OUTOFMEMORY;

		D3DLOCKED_RECT locked{};
		HRESULT result = surface->LockRect(&locked, nullptr, D3DLOCK_READONLY);
		if (FAILED(result))
			return result;

		const UINT stride = static_cast<UINT>(stride64);
		const UINT bufferSize = static_cast<UINT>(size64);
		std::vector<BYTE> pixels(bufferSize);
		for (unsigned int y = 0; y < height; ++y)
		{
			const BYTE* source =
				static_cast<const BYTE*>(locked.pBits) +
				static_cast<ptrdiff_t>(y) * locked.Pitch;
			BYTE* destination =
				pixels.data() + static_cast<size_t>(y) * stride;
			memcpy(destination, source, stride);
			for (unsigned int x = 0; x < width; ++x)
				destination[x * 4u + 3u] = 0xFF;
		}
		surface->UnlockRect();

		const HRESULT comResult =
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool uninitializeCom =
			comResult == S_OK || comResult == S_FALSE;
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
			return comResult;

		IWICImagingFactory* factory = nullptr;
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		IPropertyBag2* properties = nullptr;

		result = CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory));

		int required = 0;
		std::wstring widePath;
		if (SUCCEEDED(result))
		{
			required = MultiByteToWideChar(
				CP_ACP,
				0,
				path.c_str(),
				-1,
				nullptr,
				0);
			if (required <= 0)
				result = HRESULT_FROM_WIN32(GetLastError());
		}
		if (SUCCEEDED(result))
		{
			widePath.resize(static_cast<size_t>(required));
			if (MultiByteToWideChar(
				CP_ACP,
				0,
				path.c_str(),
				-1,
				&widePath[0],
				required) <= 0)
			{
				result = HRESULT_FROM_WIN32(GetLastError());
			}
		}
		if (SUCCEEDED(result))
			result = factory->CreateStream(&stream);
		if (SUCCEEDED(result))
			result = stream->InitializeFromFilename(
				widePath.c_str(),
				GENERIC_WRITE);
		if (SUCCEEDED(result))
			result = factory->CreateEncoder(
				GUID_ContainerFormatPng,
				nullptr,
				&encoder);
		if (SUCCEEDED(result))
			result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
		if (SUCCEEDED(result))
			result = encoder->CreateNewFrame(&frame, &properties);
		if (SUCCEEDED(result))
			result = frame->Initialize(properties);
		if (SUCCEEDED(result))
			result = frame->SetSize(width, height);

		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
		if (SUCCEEDED(result))
			result = frame->SetPixelFormat(&pixelFormat);
		if (SUCCEEDED(result) &&
			!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
		{
			result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
		}
		if (SUCCEEDED(result))
			result = frame->WritePixels(
				height,
				stride,
				bufferSize,
				pixels.data());
		if (SUCCEEDED(result))
			result = frame->Commit();
		if (SUCCEEDED(result))
			result = encoder->Commit();

		ReleaseInterface(properties);
		ReleaseInterface(frame);
		ReleaseInterface(encoder);
		ReleaseInterface(stream);
		ReleaseInterface(factory);
		if (uninitializeCom)
			CoUninitialize();
		return result;
	}

	HRESULT CaptureBackbuffer(
		IDirect3DDevice9* device,
		const std::string& pngPath,
		D3DSURFACE_DESC& description)
	{
		if (!device || pngPath.empty())
			return E_INVALIDARG;

		g_compare.captureEncoder = "none";
		g_compare.d3dxEncodeResult = E_PENDING;
		g_compare.captureStage = "get_render_target";
		IDirect3DSurface9* backbuffer = nullptr;
		HRESULT result = device->GetRenderTarget(0, &backbuffer);
		if (FAILED(result) || !backbuffer)
			return result;

		g_compare.captureStage = "get_description";
		result = backbuffer->GetDesc(&description);
		IDirect3DSurface9* resolved = nullptr;
		IDirect3DSurface9* systemMemory = nullptr;
		IDirect3DSurface9* encodeSurface = nullptr;
		IDirect3DSurface9* readSource = nullptr;

		if (SUCCEEDED(result))
		{
			g_compare.captureStage = "create_resolve_target";
			result = device->CreateRenderTarget(
				description.Width,
				description.Height,
				description.Format,
				D3DMULTISAMPLE_NONE,
				0,
				FALSE,
				&resolved,
				nullptr);
			if (SUCCEEDED(result))
			{
				g_compare.captureStage = "resolve_backbuffer";
				result = device->StretchRect(
					backbuffer,
					nullptr,
					resolved,
					nullptr,
					D3DTEXF_NONE);
				if (SUCCEEDED(result))
					readSource = resolved;
			}
		}

		if (SUCCEEDED(result))
		{
			g_compare.captureStage = "create_system_surface";
			result = device->CreateOffscreenPlainSurface(
				description.Width,
				description.Height,
				description.Format,
				D3DPOOL_SYSTEMMEM,
				&systemMemory,
				nullptr);
		}
		if (SUCCEEDED(result))
		{
			g_compare.captureStage = "read_render_target";
			result = device->GetRenderTargetData(readSource, systemMemory);
		}
		if (SUCCEEDED(result) && description.Format != D3DFMT_A8R8G8B8)
		{
			g_compare.captureStage = "create_encode_surface";
			result = device->CreateOffscreenPlainSurface(
				description.Width,
				description.Height,
				D3DFMT_A8R8G8B8,
				D3DPOOL_SYSTEMMEM,
				&encodeSurface,
				nullptr);
			if (SUCCEEDED(result))
			{
				g_compare.captureStage = "convert_encode_surface";
				result = D3DXLoadSurfaceFromSurface(
					encodeSurface,
					nullptr,
					nullptr,
					systemMemory,
					nullptr,
					nullptr,
					D3DX_FILTER_NONE,
					0);
			}
		}
		if (SUCCEEDED(result))
		{
			g_compare.captureStage = "encode_png";
			g_compare.captureEncoder = "d3dx";
			result = D3DXSaveSurfaceToFileA(
				pngPath.c_str(),
				D3DXIFF_PNG,
				encodeSurface ? encodeSurface : systemMemory,
				nullptr,
				nullptr);
			g_compare.d3dxEncodeResult = result;
			if (FAILED(result))
			{
				g_compare.captureStage = "encode_png_wic";
				g_compare.captureEncoder = "wic";
				result = SaveSurfaceToPngWithWic(
					encodeSurface ? encodeSurface : systemMemory,
					description.Width,
					description.Height,
					pngPath);
			}
		}
		if (SUCCEEDED(result))
			g_compare.captureStage = "complete";

		if (encodeSurface)
			encodeSurface->Release();
		if (systemMemory)
			systemMemory->Release();
		if (resolved)
			resolved->Release();
		backbuffer->Release();
		return result;
	}

	bool WriteSnapshot(
		IDirect3DDevice9* device,
		const std::string& jsonPath,
		const std::string& pngFilename,
		const D3DSURFACE_DESC& backbuffer,
		HRESULT captureResult)
	{
		if (jsonPath.empty())
			return false;

		const ObjectManager* objectManager = g_pObjectManager;
		const TimerManager* timerManager = g_pTimerManager;
		const CPSock* socketManager = g_pSocketManager;
		const TMCamera* camera =
			objectManager ? objectManager->m_pCamera : nullptr;

		D3DVIEWPORT9 viewport{};
		const bool hasViewport =
			device && SUCCEEDED(device->GetViewport(&viewport));

		std::ostringstream json;
		json << "{\n"
			<< "  \"schema\":\"openwyd.debug-frame\",\n"
			<< "  \"schema_version\":1,\n"
			<< "  \"frame_id\":" << g_compare.activeFrameId << ",\n"
			<< "  \"state\":{"
			<< "\"game\":";
		if (objectManager)
			json << static_cast<int>(objectManager->m_eCurrentState);
		else
			json << "null";
		json << ",\"scene\":";
		if (g_pCurrentScene)
			json << static_cast<int>(g_pCurrentScene->m_eSceneType);
		else
			json << "null";
		json << "},\n"
			<< "  \"ticks\":{"
			<< "\"compare_frame\":" << g_compare.activeFrameId
			<< ",\"timer_index\":";
		if (timerManager)
			json << timerManager->m_dwCurrentIndexNumber;
		else
			json << "null";
		json << ",\"server_current\":" << CurrentTime
			<< "},\n"
			<< "  \"clock\":{"
			<< "\"controlled_time_ms\":" << g_compare.activeTimeMs
			<< ",\"timer_base_ms\":";
		if (timerManager)
			json << timerManager->m_dwBaseTime;
		else
			json << "null";
		json << ",\"timer_server_base_ms\":";
		if (timerManager)
			json << timerManager->m_dwServerTime;
		else
			json << "null";
		json << ",\"timer_delay_ms\":";
		if (timerManager)
			json << timerManager->m_dwDelayTime;
		else
			json << "null";
		json << "},\n"
			<< "  \"camera\":{";
		if (camera)
		{
			json << "\"position\":";
			AppendVector(json, camera->m_cameraPos);
			json << ",\"look_at\":";
			AppendVector(json, const_cast<TMCamera*>(camera)->GetCameraLookatPos());
			json << ",\"direction\":";
			AppendVector(json, camera->m_vecCamDir);
			json << ",\"horizon_angle\":";
			AppendFloat(json, camera->m_fHorizonAngle);
			json << ",\"vertical_angle\":";
			AppendFloat(json, camera->m_fVerticalAngle);
		}
		json << "},\n"
			<< "  \"matrices\":{"
			<< "\"world\":";
		AppendTransform(json, device, D3DTS_WORLD);
		json << ",\"view\":";
		AppendTransform(json, device, D3DTS_VIEW);
		json << ",\"projection\":";
		AppendTransform(json, device, D3DTS_PROJECTION);
		json << "},\n"
			<< "  \"draws\":[],\n"
			<< "  \"render\":{"
			<< "\"capture_point\":\"after_EndScene_before_Present\""
			<< ",\"width\":" << backbuffer.Width
			<< ",\"height\":" << backbuffer.Height
			<< ",\"format\":" << static_cast<unsigned int>(backbuffer.Format)
			<< ",\"format_name\":";
		AppendJsonString(json, D3DFormatName(backbuffer.Format));
		json << ",\"multisample_type\":"
			<< static_cast<unsigned int>(backbuffer.MultiSampleType)
			<< ",\"multisample_quality\":" << backbuffer.MultiSampleQuality
			<< ",\"viewport\":";
		if (hasViewport)
		{
			json << "{"
				<< "\"x\":" << viewport.X
				<< ",\"y\":" << viewport.Y
				<< ",\"width\":" << viewport.Width
				<< ",\"height\":" << viewport.Height
				<< ",\"min_z\":";
			AppendFloat(json, viewport.MinZ);
			json << ",\"max_z\":";
			AppendFloat(json, viewport.MaxZ);
			json << "}";
		}
		else
		{
			json << "null";
		}
		json << ",\"states\":{"
			<< "\"z_enable\":";
		AppendRenderState(json, device, D3DRS_ZENABLE);
		json << ",\"z_write_enable\":";
		AppendRenderState(json, device, D3DRS_ZWRITEENABLE);
		json << ",\"alpha_blend_enable\":";
		AppendRenderState(json, device, D3DRS_ALPHABLENDENABLE);
		json << ",\"alpha_test_enable\":";
		AppendRenderState(json, device, D3DRS_ALPHATESTENABLE);
		json << ",\"source_blend\":";
		AppendRenderState(json, device, D3DRS_SRCBLEND);
		json << ",\"destination_blend\":";
		AppendRenderState(json, device, D3DRS_DESTBLEND);
		json << ",\"cull_mode\":";
		AppendRenderState(json, device, D3DRS_CULLMODE);
		json << ",\"fog_enable\":";
		AppendRenderState(json, device, D3DRS_FOGENABLE);
		json << ",\"lighting\":";
		AppendRenderState(json, device, D3DRS_LIGHTING);
		json << "}},\n"
			<< "  \"network\":{";
		if (socketManager)
		{
			json << "\"socket\":" << socketManager->Sock
				<< ",\"send_buffered_bytes\":" << socketManager->nSendPosition
				<< ",\"send_offset_bytes\":" << socketManager->nSentPosition
				<< ",\"receive_buffered_bytes\":" << socketManager->nRecvPosition
				<< ",\"receive_processed_bytes\":" << socketManager->nProcPosition
				<< ",\"send_keyword_count\":" << socketManager->SendCount
				<< ",\"receive_keyword_count\":" << socketManager->RecvCount
				<< ",\"error_count\":" << socketManager->ErrCount
				<< ",";
		}
		json << "\"host\":";
		AppendJsonString(json, wyd_socket_last_host());
		json << ",\"port\":" << wyd_socket_last_port()
			<< ",\"connect_result\":" << wyd_socket_last_connect_result()
			<< ",\"last_error\":" << wyd_socket_last_error()
			<< ",\"bytes_sent\":" << wyd_socket_bytes_sent()
			<< ",\"bytes_received\":" << wyd_socket_bytes_received()
			<< ",\"last_sent_opcode\":" << wyd_socket_last_sent_opcode()
			<< ",\"last_received_opcode\":" << wyd_socket_last_recv_opcode();
		if (g_compare.serverEndpointResolved)
		{
			json << ",\"resolved_server_host\":";
			AppendJsonString(json, g_compare.resolvedServerHost);
			json << ",\"resolved_server_port\":"
				<< g_compare.resolvedServerPort
				<< ",\"server_endpoint_override\":"
				<< ((g_compare.hasServerHostOverride ||
					g_compare.hasServerPortOverride) ? "true" : "false");
		}
		json << "},\n"
			<< "  \"extensions\":{"
			<< "\"native\":{"
			<< "\"protocol_version\":1"
			<< ",\"process_id\":" << GetCurrentProcessId()
			<< ",\"png\":";
		AppendJsonString(json, pngFilename);
		json << ",\"capture_hresult\":" << static_cast<long>(captureResult)
			<< ",\"capture_stage\":";
		AppendJsonString(json, g_compare.captureStage);
		json << ",\"capture_encoder\":";
		AppendJsonString(json, g_compare.captureEncoder);
		json
			<< ",\"d3dx_encode_hresult\":"
			<< static_cast<long>(g_compare.d3dxEncodeResult)
			<< ",\"input_count\":" << g_compare.activeInputCount
			<< ",\"input_last_sequence\":"
			<< g_compare.activeInputLastSequence
			<< ",\"mouse\":{"
			<< "\"has_position\":"
			<< (g_compare.mouseHasPosition ? "true" : "false")
			<< ",\"x\":" << g_compare.mouseX
			<< ",\"y\":" << g_compare.mouseY
			<< ",\"delta_x\":" << g_compare.lastConsumedMouseDeltaX
			<< ",\"delta_y\":" << g_compare.lastConsumedMouseDeltaY
			<< ",\"wheel\":" << g_compare.lastConsumedMouseWheel
			<< ",\"buttons\":["
			<< ((g_compare.injectedMouseButtons[0] & 0x80) ? 1 : 0)
			<< ","
			<< ((g_compare.injectedMouseButtons[1] & 0x80) ? 1 : 0)
			<< ","
			<< ((g_compare.injectedMouseButtons[2] & 0x80) ? 1 : 0)
			<< "]}"
			<< ",\"modifiers\":{"
			<< "\"control\":"
			<< ((g_compare.injectedKeyDown[VK_CONTROL] ||
				g_compare.injectedKeyDown[VK_LCONTROL] ||
				g_compare.injectedKeyDown[VK_RCONTROL]) ? "true" : "false")
			<< ",\"shift\":"
			<< ((g_compare.injectedKeyDown[VK_SHIFT] ||
				g_compare.injectedKeyDown[VK_LSHIFT] ||
				g_compare.injectedKeyDown[VK_RSHIFT]) ? "true" : "false")
			<< "}"
			<< ",\"random\":{"
			<< "\"armed\":"
			<< (OpenWydCompareRandomIsArmed() ? "true" : "false")
			<< ",\"configured_seed\":"
			<< OpenWydCompareRandomConfiguredSeed()
			<< ",\"state\":" << OpenWydCompareRandomState()
			<< ",\"rand_calls\":" << OpenWydCompareRandomRandCalls()
			<< ",\"srand_calls\":" << OpenWydCompareRandomSrandCalls()
			<< ",\"last_requested_seed\":"
			<< OpenWydCompareRandomLastRequestedSeed()
			<< "}"
			<< ",\"draw_capture_available\":false"
			<< ",\"packet_opcode_hash_available\":false"
			<< "}}\n"
			<< "}\n";

		std::ofstream output(jsonPath, std::ios::binary | std::ios::trunc);
		if (!output)
			return false;
		const std::string encoded = json.str();
		output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
		output.flush();
		return output.good();
	}
}

bool OpenWydCompareArmRandomFromEnvironment()
{
	static bool attempted = false;
	if (attempted)
		return OpenWydCompareRandomIsArmed();
	attempted = true;

	const std::string value =
		ReadEnvironment("OPENWYD_COMPARE_RANDOM_SEED");
	if (value.empty())
		return false;

	unsigned long long seed = 0;
	if (!ParseUnsigned(value, 0xFFFFFFFFull, seed))
	{
		OutputDebugStringA(
			"OpenWydCompare: invalid OPENWYD_COMPARE_RANDOM_SEED.\n");
		return false;
	}

	OpenWydCompareRandomArm(static_cast<unsigned int>(seed));
	return true;
}

bool OpenWydCompareInitialize(HWND hWnd, unsigned int width, unsigned int height)
{
	if (g_compare.initialized)
	{
		g_compare.window = hWnd;
		g_compare.width = width;
		g_compare.height = height;
		return g_compare.enabled;
	}

	g_compare.initialized = true;
	g_compare.window = hWnd;
	g_compare.width = width;
	g_compare.height = height;

	std::string pipeName = ReadEnvironment("OPENWYD_COMPARE_PIPE");
	if (pipeName.empty())
		return false;
	if (!StartsWith(pipeName, "\\\\.\\pipe\\"))
		pipeName = "\\\\.\\pipe\\" + pipeName;
	g_compare.pipePath = pipeName;

	g_compare.artifactDirectory =
		ReadEnvironment("OPENWYD_COMPARE_ARTIFACTS");
	if (!g_compare.artifactDirectory.empty())
	{
		const int createResult =
			SHCreateDirectoryExA(nullptr, g_compare.artifactDirectory.c_str(), nullptr);
		if (createResult != ERROR_SUCCESS &&
			createResult != ERROR_FILE_EXISTS &&
			createResult != ERROR_ALREADY_EXISTS)
		{
			g_compare.artifactDirectory.clear();
			OutputDebugStringA("OpenWydCompare: artifact directory creation failed.\n");
		}
	}

	DWORD startTime = 0;
	const std::string startTimeValue =
		ReadEnvironment("OPENWYD_COMPARE_START_TIME_MS");
	if (!startTimeValue.empty())
	{
		unsigned long long parsed = 0;
		if (ParseUnsigned(startTimeValue, kMaximumControlledTime, parsed))
			startTime = static_cast<DWORD>(parsed);
	}
	InterlockedExchange(
		&g_compare.controlledTimeMs,
		static_cast<LONG>(startTime));

	const std::string serverHost =
		ReadEnvironment("OPENWYD_COMPARE_SERVER_HOST");
	if (!serverHost.empty())
	{
		if (IsValidServerHost(serverHost))
		{
			g_compare.serverHostOverride = serverHost;
			g_compare.hasServerHostOverride = true;
		}
		else
		{
			OutputDebugStringA("OpenWydCompare: invalid server host override ignored.\n");
		}
	}

	const std::string serverPort =
		ReadEnvironment("OPENWYD_COMPARE_SERVER_PORT");
	if (!serverPort.empty())
	{
		unsigned long long parsed = 0;
		if (ParseUnsigned(serverPort, 65535, parsed) && parsed != 0)
		{
			g_compare.serverPortOverride = static_cast<int>(parsed);
			g_compare.hasServerPortOverride = true;
		}
		else
		{
			OutputDebugStringA("OpenWydCompare: invalid server port override ignored.\n");
		}
	}

	g_compare.pipe = CreateNamedPipeA(
		g_compare.pipePath.c_str(),
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
		1,
		kPipeBufferBytes,
		kPipeBufferBytes,
		0,
		nullptr);
	if (g_compare.pipe == INVALID_HANDLE_VALUE)
	{
		OutputDebugStringA("OpenWydCompare: CreateNamedPipe failed.\n");
		return false;
	}

	g_compare.enabled = true;
	return true;
}

void OpenWydComparePoll()
{
	if (!g_compare.enabled || g_compare.shuttingDown)
		return;

	TryAcceptConnection();
	ReadCommands();
}

bool OpenWydCompareIsEnabled()
{
	return g_compare.enabled && !g_compare.shuttingDown;
}

bool OpenWydCompareTryBeginFrame()
{
	if (!OpenWydCompareIsEnabled())
		return true;
	if (g_compare.frameActive)
		return true;
	if (!g_compare.stepPending)
		return false;

	g_compare.activeFrameId = g_compare.pendingFrameId;
	g_compare.activeTimeMs = g_compare.pendingTimeMs;
	g_compare.stepPending = false;
	g_compare.frameActive = true;
	return true;
}

bool OpenWydCompareTakePausedControlMessage(MSG* message)
{
	if (!OpenWydCompareIsEnabled() ||
		!message ||
		g_compare.frameActive)
	{
		return false;
	}

	// Do not remove game, input, timer, or socket messages while the gate is
	// closed. In particular, WM_USER + 1/+100 must remain queued until a STEP
	// has activated its controlled clock. Shutdown remains possible while
	// paused without opening an implicit game tick.
	if (PeekMessageA(
		message,
		nullptr,
		WM_QUIT,
		WM_QUIT,
		PM_REMOVE))
	{
		return true;
	}

	return g_compare.window &&
		PeekMessageA(
			message,
			g_compare.window,
			WM_CLOSE,
			WM_CLOSE,
			PM_REMOVE);
}

bool OpenWydCompareShouldDispatchMessage(const MSG* message)
{
	if (!OpenWydCompareIsEnabled() || !message)
		return true;

	switch (message->message)
	{
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
		return (message->wParam & kInjectedMouseMessage) != 0;

	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_XBUTTONDBLCLK:
		return false;

	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_CHAR:
		return (message->lParam & kInjectedKeyMessage) != 0;

	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_SYSCHAR:
	case WM_SYSDEADCHAR:
	case WM_DEADCHAR:
	case WM_UNICHAR:
	case WM_IME_STARTCOMPOSITION:
	case WM_IME_COMPOSITION:
	case WM_IME_ENDCOMPOSITION:
	case WM_IME_CHAR:
		return false;

	default:
		return true;
	}
}

bool OpenWydCompareTakeInjectedMouseMessage(
	UINT message,
	WPARAM* wParam,
	int x,
	int y)
{
	if (!OpenWydCompareIsEnabled() ||
		!wParam ||
		(*wParam & kInjectedMouseMessage) == 0)
	{
		return false;
	}

	*wParam &= ~kInjectedMouseMessage;

	const LONG nextX = static_cast<LONG>(x);
	const LONG nextY = static_cast<LONG>(y);
	if (g_compare.mouseHasPosition)
	{
		g_compare.mouseDeltaX += nextX - g_compare.mouseX;
		g_compare.mouseDeltaY += nextY - g_compare.mouseY;
	}
	else
	{
		g_compare.mouseHasPosition = true;
	}
	g_compare.mouseX = nextX;
	g_compare.mouseY = nextY;

	auto setButton = [](unsigned int index, bool down)
	{
		if (index < sizeof(g_compare.injectedMouseButtons))
		{
			g_compare.injectedMouseButtons[index] =
				down ? static_cast<BYTE>(0x80) : static_cast<BYTE>(0);
		}
	};

	switch (message)
	{
	case WM_LBUTTONDOWN:
		setButton(0, true);
		break;
	case WM_LBUTTONUP:
		setButton(0, false);
		break;
	case WM_RBUTTONDOWN:
		setButton(1, true);
		break;
	case WM_RBUTTONUP:
		setButton(1, false);
		break;
	case WM_MBUTTONDOWN:
		setButton(2, true);
		break;
	case WM_MBUTTONUP:
		setButton(2, false);
		break;
	case WM_MOUSEMOVE:
		setButton(0, (*wParam & MK_LBUTTON) != 0);
		setButton(1, (*wParam & MK_RBUTTON) != 0);
		setButton(2, (*wParam & MK_MBUTTON) != 0);
		break;
	default:
		break;
	}
	return true;
}

bool OpenWydCompareConsumeMouseState(
	LONG* deltaX,
	LONG* deltaY,
	LONG* wheel,
	BYTE* buttons,
	unsigned int buttonCapacity)
{
	if (!OpenWydCompareIsEnabled() ||
		!deltaX ||
		!deltaY ||
		!wheel ||
		!buttons)
	{
		return false;
	}

	*deltaX = g_compare.mouseDeltaX;
	*deltaY = g_compare.mouseDeltaY;
	*wheel = g_compare.mouseWheel;
	memset(buttons, 0, buttonCapacity);
	const unsigned int copyCount =
		buttonCapacity < sizeof(g_compare.injectedMouseButtons)
			? buttonCapacity
			: static_cast<unsigned int>(sizeof(g_compare.injectedMouseButtons));
	memcpy(buttons, g_compare.injectedMouseButtons, copyCount);

	g_compare.lastConsumedMouseDeltaX = g_compare.mouseDeltaX;
	g_compare.lastConsumedMouseDeltaY = g_compare.mouseDeltaY;
	g_compare.lastConsumedMouseWheel = g_compare.mouseWheel;
	g_compare.mouseDeltaX = 0;
	g_compare.mouseDeltaY = 0;
	g_compare.mouseWheel = 0;
	return true;
}

bool OpenWydCompareTakeInjectedKeyMessage(
	bool down,
	WPARAM wParam,
	LPARAM* lParam)
{
	if (!OpenWydCompareIsEnabled() ||
		!lParam ||
		(*lParam & kInjectedKeyMessage) == 0)
	{
		return false;
	}

	*lParam &= ~kInjectedKeyMessage;
	if (wParam < 256)
		g_compare.injectedKeyDown[wParam] = down;
	return true;
}

bool OpenWydCompareInjectedKeyIsDown(unsigned int virtualKey)
{
	return OpenWydCompareIsEnabled() &&
		virtualKey < sizeof(g_compare.injectedKeyDown) &&
		g_compare.injectedKeyDown[virtualKey];
}

void OpenWydCompareOnBeforePresent(IDirect3DDevice9* device)
{
	if (!OpenWydCompareIsEnabled() ||
		!g_compare.frameActive ||
		g_compare.presentCapturePending)
		return;

	const std::string stem = FrameStem(g_compare.activeFrameId);
	const std::string pngFilename = stem + ".png";
	const std::string jsonFilename = stem + ".json";
	const std::string pngPath = g_compare.artifactDirectory.empty()
		? std::string()
		: JoinPath(g_compare.artifactDirectory, pngFilename);
	const std::string jsonPath = g_compare.artifactDirectory.empty()
		? std::string()
		: JoinPath(g_compare.artifactDirectory, jsonFilename);

	D3DSURFACE_DESC backbuffer{};
	const HRESULT captureResult =
		CaptureBackbuffer(device, pngPath, backbuffer);
	const bool snapshotWritten =
		WriteSnapshot(
			device,
			jsonPath,
			pngFilename,
			backbuffer,
			captureResult);

	g_compare.pendingCaptureResult = captureResult;
	g_compare.pendingSnapshotWritten = snapshotWritten;
	g_compare.presentCapturePending = true;
}

void OpenWydCompareOnAfterPresent(HRESULT presentResult)
{
	if (!OpenWydCompareIsEnabled() ||
		!g_compare.frameActive ||
		!g_compare.presentCapturePending)
	{
		return;
	}

	if (FAILED(presentResult))
	{
		g_compare.fatalProtocolError = true;
		g_compare.presentCapturePending = false;
		g_compare.pendingCaptureResult = S_OK;
		g_compare.pendingSnapshotWritten = false;
		g_compare.frameActive = false;
		SendError("present_failed_fatal");
		OutputDebugStringA("OpenWydCompare: IDirect3DDevice9::Present failed.\n");
		if (IsWindow(g_compare.window))
			PostMessageA(g_compare.window, WM_CLOSE, 0, 0);
		return;
	}

	std::ostringstream response;
	response << "PRESENT "
		<< g_compare.activeFrameId << " "
		<< g_compare.activeTimeMs << " "
		<< "0x" << std::uppercase << std::hex
		<< std::setfill('0') << std::setw(8)
		<< static_cast<unsigned long>(g_compare.pendingCaptureResult)
		<< std::dec << " "
		<< (g_compare.pendingSnapshotWritten ? 1 : 0)
		<< "\n";
	SendLine(response.str());

	g_compare.presentCapturePending = false;
	g_compare.pendingCaptureResult = S_OK;
	g_compare.pendingSnapshotWritten = false;
	g_compare.frameActive = false;
}

void OpenWydCompareOnFrameTickComplete()
{
	if (!OpenWydCompareIsEnabled() || !g_compare.frameActive)
		return;

	// One accepted STEP may execute the active branch exactly once. If that
	// branch returned without a successful Present, keeping frameActive set
	// would let a later RunTick silently continue the same logical frame.
	g_compare.fatalProtocolError = true;
	g_compare.presentCapturePending = false;
	g_compare.pendingCaptureResult = S_OK;
	g_compare.pendingSnapshotWritten = false;
	g_compare.frameActive = false;
	SendError("frame_completed_without_present_fatal");
	OutputDebugStringA(
		"OpenWydCompare: active frame completed without Present.\n");
	if (IsWindow(g_compare.window))
		PostMessageA(g_compare.window, WM_CLOSE, 0, 0);
}

void OpenWydCompareResolveServerEndpoint(
	const char* originalHost,
	int originalPort,
	char* resolvedHost,
	unsigned int resolvedHostCapacity,
	int* resolvedPort)
{
	if (!resolvedHost || resolvedHostCapacity == 0 || !resolvedPort)
		return;

	const char* selectedHost = originalHost ? originalHost : "";
	int selectedPort = originalPort;
	if (OpenWydCompareIsEnabled())
	{
		if (g_compare.hasServerHostOverride)
			selectedHost = g_compare.serverHostOverride.c_str();
		if (g_compare.hasServerPortOverride)
			selectedPort = g_compare.serverPortOverride;
	}

	strncpy_s(
		resolvedHost,
		resolvedHostCapacity,
		selectedHost,
		_TRUNCATE);
	*resolvedPort = selectedPort;

	g_compare.serverEndpointResolved = true;
	g_compare.resolvedServerHost = resolvedHost;
	g_compare.resolvedServerPort = selectedPort;
}

void OpenWydCompareShutdown()
{
	if (!g_compare.initialized || g_compare.shuttingDown)
		return;

	g_compare.shuttingDown = true;
	if (g_compare.connected)
	{
		SendLine("BYE\n");
		if (g_compare.connected)
			FlushFileBuffers(g_compare.pipe);
	}

	if (g_compare.pipe != INVALID_HANDLE_VALUE)
	{
		if (g_compare.connected)
			DisconnectNamedPipe(g_compare.pipe);
		CloseHandle(g_compare.pipe);
		g_compare.pipe = INVALID_HANDLE_VALUE;
	}

	g_compare.connected = false;
	g_compare.enabled = false;
	g_compare.stepPending = false;
	g_compare.frameActive = false;
	g_compare.presentCapturePending = false;
	g_compare.pendingCaptureResult = S_OK;
	g_compare.pendingSnapshotWritten = false;
	g_compare.hasQueuedInputFrame = false;
	g_compare.queuedInputCount = 0;
	ResetInjectedInputState();
	g_compare.input.clear();
	g_compare.queuedInputs.clear();
}

DWORD WINAPI OpenWydCompareTimeGetTime()
{
	if (!g_compare.enabled)
		return ::timeGetTime();
	return static_cast<DWORD>(
		InterlockedCompareExchange(&g_compare.controlledTimeMs, 0, 0));
}

DWORD WINAPI OpenWydCompareGetTickCount()
{
	if (!g_compare.enabled)
		return ::GetTickCount();
	return static_cast<DWORD>(
		InterlockedCompareExchange(&g_compare.controlledTimeMs, 0, 0));
}

#endif
