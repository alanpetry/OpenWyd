#include "Windows.h"
#include "Rpc.h"
#include "io.h"
#include "winsock2.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <glob.h>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace {

enum class EventKind {
    Socket,
    Timer,
    Signal
};

struct EventSource {
    EventKind kind;
    int fd;
    HWND window;
    UINT message;
    std::uintptr_t timer_id;
    long socket_events;
};

struct FileSearch {
    std::vector<std::string> paths;
    std::size_t next_index = 0;
};

std::mutex g_mutex;
std::deque<MSG> g_messages;
std::unordered_map<int, EventSource> g_sources;
std::unordered_map<std::uintptr_t, int> g_timers;
WNDPROC g_window_proc = nullptr;
HWND g_main_window = reinterpret_cast<HWND>(1);
int g_epoll = -1;
int g_signal_fd = -1;
std::atomic<bool> g_quitting{false};
int g_exit_code = 0;

void queue_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_messages.push_back(MSG{window, message, wparam, lparam, timeGetTime(), {0, 0}});
}

std::uint32_t epoll_events(long events) {
    std::uint32_t result = EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    if (events & (FD_READ | FD_ACCEPT))
        result |= EPOLLIN;
    if (events & (FD_WRITE | FD_CONNECT))
        result |= EPOLLOUT;
    if (events & FD_OOB)
        result |= EPOLLPRI;
    return result;
}

WORD selected_event(const EventSource& source, std::uint32_t events) {
    if ((events & EPOLLIN) && (source.socket_events & FD_ACCEPT))
        return FD_ACCEPT;
    if ((events & EPOLLIN) && (source.socket_events & FD_READ))
        return FD_READ;
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        return FD_CLOSE;
    if ((events & EPOLLOUT) && (source.socket_events & FD_CONNECT))
        return FD_CONNECT;
    if ((events & EPOLLOUT) && (source.socket_events & FD_WRITE))
        return FD_WRITE;
    return 0;
}

void initialize_event_loop() {
    if (g_epoll >= 0)
        return;

    g_epoll = epoll_create1(EPOLL_CLOEXEC);
    if (g_epoll < 0) {
        std::perror("epoll_create1");
        std::exit(2);
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    g_signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (g_signal_fd >= 0) {
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = g_signal_fd;
        epoll_ctl(g_epoll, EPOLL_CTL_ADD, g_signal_fd, &event);
        g_sources.emplace(g_signal_fd, EventSource{EventKind::Signal, g_signal_fd, g_main_window, WM_CLOSE, 0, 0});
    }
}

bool pop_message(MSG* message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_messages.empty())
        return false;
    *message = g_messages.front();
    g_messages.pop_front();
    return true;
}

void fill_find_data(const std::string& path, WIN32_FIND_DATA* data) {
    if (!data)
        return;
    std::memset(data, 0, sizeof(*data));
    const std::size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::snprintf(data->cFileName, sizeof(data->cFileName), "%s", name.c_str());
    struct stat status {};
    if (stat(path.c_str(), &status) == 0) {
        if (S_ISDIR(status.st_mode))
            data->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        const std::uint64_t seconds = static_cast<std::uint64_t>(status.st_mtime);
        data->ftLastWriteTime.dwLowDateTime = static_cast<DWORD>(seconds);
        data->ftLastWriteTime.dwHighDateTime = static_cast<DWORD>(seconds >> 32u);
        const std::uint64_t size = static_cast<std::uint64_t>(status.st_size);
        data->nFileSizeLow = static_cast<DWORD>(size);
        data->nFileSizeHigh = static_cast<DWORD>(size >> 32u);
    }
}

bool ascii_case_equal(const std::string& left, const std::string& right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto left_char = static_cast<unsigned char>(left[index]);
        const auto right_char = static_cast<unsigned char>(right[index]);
        if (std::tolower(left_char) != std::tolower(right_char))
            return false;
    }
    return true;
}

bool resolve_case_insensitive_path(const char* path, std::string* resolved) {
    if (!path || !*path || !resolved)
        return false;

    std::string normalized(path);
    for (char& character : normalized) {
        if (character == '\\')
            character = '/';
    }

    const bool absolute = normalized.front() == '/';
    std::string current = absolute ? "/" : ".";
    std::size_t start = absolute ? 1 : 0;
    while (start <= normalized.size()) {
        const std::size_t separator = normalized.find('/', start);
        const std::string component = normalized.substr(
            start,
            separator == std::string::npos ? std::string::npos : separator - start);
        start = separator == std::string::npos ? normalized.size() + 1 : separator + 1;

        if (component.empty() || component == ".")
            continue;

        const std::string candidate =
            current == "/" ? current + component : current + "/" + component;
        struct stat status {};
        if (::stat(candidate.c_str(), &status) == 0) {
            current = candidate;
            continue;
        }

        DIR* directory = ::opendir(current.c_str());
        if (!directory)
            return false;

        std::string actual_name;
        while (dirent* entry = ::readdir(directory)) {
            if (ascii_case_equal(entry->d_name, component)) {
                actual_name = entry->d_name;
                break;
            }
        }
        ::closedir(directory);
        if (actual_name.empty())
            return false;

        current = current == "/" ? current + actual_name : current + "/" + actual_name;
    }

    *resolved = current;
    return true;
}

} // namespace

int openwyd_open_compat(const char* path, int flags, int mode) {
    const int descriptor = ::open(path, flags, mode);
    if (descriptor >= 0 || errno != ENOENT)
        return descriptor;

    const int original_error = errno;
    std::string resolved;
    if (!resolve_case_insensitive_path(path, &resolved)) {
        errno = original_error;
        return -1;
    }

    return ::open(resolved.c_str(), flags, mode);
}

void openwyd_set_window_proc(WNDPROC proc) {
    g_window_proc = proc;
}

ATOM RegisterClass(const WNDCLASS* window_class) {
    if (window_class)
        openwyd_set_window_proc(window_class->lpfnWndProc);
    return 1;
}

HWND CreateWindow(LPCSTR, LPCSTR title, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) {
    initialize_event_loop();
    std::fprintf(stdout, "[openwyd] headless window: %s\n", title ? title : "server");
    std::fflush(stdout);
    return g_main_window;
}

BOOL ShowWindow(HWND, int) { return TRUE; }
BOOL UpdateWindow(HWND) { return TRUE; }
BOOL DestroyWindow(HWND window) {
    queue_message(window, WM_DESTROY, 0, 0);
    return TRUE;
}
LRESULT DefWindowProc(HWND window, UINT message, WPARAM, LPARAM) {
    if (message == WM_CLOSE)
        DestroyWindow(window);
    return 0;
}
BOOL SetWindowText(HWND, LPCSTR) { return TRUE; }
BOOL SetWindowTextA(HWND window, LPCSTR text) { return SetWindowText(window, text); }
HICON LoadIcon(HINSTANCE, LPCSTR) { return nullptr; }
HCURSOR LoadCursor(HINSTANCE, LPCSTR) { return nullptr; }
HGDIOBJ GetStockObject(int) { return nullptr; }
HMENU CreateMenu() { return reinterpret_cast<HMENU>(1); }
HMENU CreatePopupMenu() { return reinterpret_cast<HMENU>(1); }
BOOL AppendMenu(HMENU, UINT, std::uintptr_t, LPCSTR) { return TRUE; }
BOOL SetMenu(HWND, HMENU) { return TRUE; }
HDC GetDC(HWND) { return nullptr; }
int ReleaseDC(HWND, HDC) { return 1; }
DWORD SetTextColor(HDC, DWORD color) { return color; }
BOOL TextOut(HDC, int, int, LPCSTR, int) { return TRUE; }
BOOL TextOutA(HDC dc, int x, int y, LPCSTR text, int length) { return TextOut(dc, x, y, text, length); }
HDC BeginPaint(HWND, PAINTSTRUCT* paint) {
    if (paint)
        paint->hdc = nullptr;
    return nullptr;
}
BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }
HFONT CreateFont(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR) { return nullptr; }
HGDIOBJ SelectObject(HDC, HGDIOBJ object) { return object; }
BOOL DeleteObject(HGDIOBJ) { return TRUE; }

int MessageBox(HWND, LPCSTR text, LPCSTR caption, UINT) {
    std::fprintf(stderr, "[openwyd] %s: %s\n", caption ? caption : "message", text ? text : "");
    std::fflush(stderr);
    return 1;
}
int MessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type) {
    return MessageBox(window, text, caption, type);
}
void ExitProcess(UINT exit_code) { std::exit(static_cast<int>(exit_code)); }
DWORD GetLastError() { return static_cast<DWORD>(errno); }
DWORD GetTickCount() { return timeGetTime(); }
DWORD timeGetTime() {
    using namespace std::chrono;
    return static_cast<DWORD>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
void Sleep(DWORD milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
void GetLocalTime(SYSTEMTIME* value) {
    if (!value)
        return;
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    std::tm local{};
    localtime_r(&now.tv_sec, &local);
    value->wYear = static_cast<WORD>(local.tm_year + 1900);
    value->wMonth = static_cast<WORD>(local.tm_mon + 1);
    value->wDayOfWeek = static_cast<WORD>(local.tm_wday);
    value->wDay = static_cast<WORD>(local.tm_mday);
    value->wHour = static_cast<WORD>(local.tm_hour);
    value->wMinute = static_cast<WORD>(local.tm_min);
    value->wSecond = static_cast<WORD>(local.tm_sec);
    value->wMilliseconds = static_cast<WORD>(now.tv_nsec / 1000000);
}
DWORD GetModuleFileName(HINSTANCE, LPSTR filename, DWORD size) {
    if (!filename || size == 0)
        return 0;
    const ssize_t length = readlink("/proc/self/exe", filename, size - 1);
    if (length < 0)
        return 0;
    filename[length] = '\0';
    return static_cast<DWORD>(length);
}
BOOL SetCurrentDirectory(LPCSTR path) { return path && chdir(path) == 0; }
BOOL DeleteFileA(LPCSTR path) { return path && unlink(path) == 0; }
BOOL MoveFile(LPCSTR source, LPCSTR destination) {
    return source && destination && rename(source, destination) == 0;
}
HANDLE FindFirstFile(LPCSTR pattern, WIN32_FIND_DATA* data) {
    if (!pattern)
        return INVALID_HANDLE_VALUE;
    glob_t matches {};
    if (glob(pattern, 0, nullptr, &matches) != 0) {
        globfree(&matches);
        return INVALID_HANDLE_VALUE;
    }
    auto* search = new FileSearch;
    for (std::size_t index = 0; index < matches.gl_pathc; ++index)
        search->paths.emplace_back(matches.gl_pathv[index]);
    globfree(&matches);
    if (search->paths.empty()) {
        delete search;
        return INVALID_HANDLE_VALUE;
    }
    fill_find_data(search->paths.front(), data);
    search->next_index = 1;
    return search;
}
BOOL FindNextFile(HANDLE handle, WIN32_FIND_DATA* data) {
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return FALSE;
    auto* search = static_cast<FileSearch*>(handle);
    if (search->next_index >= search->paths.size())
        return FALSE;
    fill_find_data(search->paths[search->next_index++], data);
    return TRUE;
}
BOOL FindClose(HANDLE handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return FALSE;
    delete static_cast<FileSearch*>(handle);
    return TRUE;
}
std::intptr_t _findfirst(const char* pattern, _finddata_t* data) {
    WIN32_FIND_DATA win_data {};
    HANDLE search = FindFirstFile(pattern, &win_data);
    if (search == INVALID_HANDLE_VALUE)
        return -1;
    if (data) {
        std::memset(data, 0, sizeof(*data));
        data->attrib = win_data.dwFileAttributes;
        data->time_write = static_cast<std::time_t>(
            static_cast<std::uint64_t>(win_data.ftLastWriteTime.dwLowDateTime) |
            (static_cast<std::uint64_t>(win_data.ftLastWriteTime.dwHighDateTime) << 32u));
        data->size = static_cast<std::int64_t>(
            static_cast<std::uint64_t>(win_data.nFileSizeLow) |
            (static_cast<std::uint64_t>(win_data.nFileSizeHigh) << 32u));
        std::snprintf(data->name, sizeof(data->name), "%s", win_data.cFileName);
    }
    return reinterpret_cast<std::intptr_t>(search);
}
int _findnext(std::intptr_t handle_value, _finddata_t* data) {
    WIN32_FIND_DATA win_data {};
    if (!FindNextFile(reinterpret_cast<HANDLE>(handle_value), &win_data))
        return -1;
    if (data) {
        std::memset(data, 0, sizeof(*data));
        data->attrib = win_data.dwFileAttributes;
        data->time_write = static_cast<std::time_t>(
            static_cast<std::uint64_t>(win_data.ftLastWriteTime.dwLowDateTime) |
            (static_cast<std::uint64_t>(win_data.ftLastWriteTime.dwHighDateTime) << 32u));
        data->size = static_cast<std::int64_t>(
            static_cast<std::uint64_t>(win_data.nFileSizeLow) |
            (static_cast<std::uint64_t>(win_data.nFileSizeHigh) << 32u));
        std::snprintf(data->name, sizeof(data->name), "%s", win_data.cFileName);
    }
    return 0;
}
int _findclose(std::intptr_t handle_value) {
    return FindClose(reinterpret_cast<HANDLE>(handle_value)) ? 0 : -1;
}
BOOL FileTimeToSystemTime(const FILETIME* file_time, SYSTEMTIME* system_time) {
    if (!file_time || !system_time)
        return FALSE;
    const std::uint64_t seconds = static_cast<std::uint64_t>(file_time->dwLowDateTime) |
                                  (static_cast<std::uint64_t>(file_time->dwHighDateTime) << 32u);
    const std::time_t unix_time = static_cast<std::time_t>(seconds);
    std::tm value {};
    gmtime_r(&unix_time, &value);
    system_time->wYear = static_cast<WORD>(value.tm_year + 1900);
    system_time->wMonth = static_cast<WORD>(value.tm_mon + 1);
    system_time->wDayOfWeek = static_cast<WORD>(value.tm_wday);
    system_time->wDay = static_cast<WORD>(value.tm_mday);
    system_time->wHour = static_cast<WORD>(value.tm_hour);
    system_time->wMinute = static_cast<WORD>(value.tm_min);
    system_time->wSecond = static_cast<WORD>(value.tm_sec);
    system_time->wMilliseconds = 0;
    return TRUE;
}

HANDLE CreateThread(LPVOID, std::size_t, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD, DWORD* thread_id) {
    try {
        auto* thread = new std::thread([start, parameter]() {
            if (start)
                start(parameter);
        });
        if (thread_id)
            *thread_id = 0;
        thread->detach();
        return thread;
    } catch (...) {
        return nullptr;
    }
}
BOOL CloseHandle(HANDLE handle) {
    delete static_cast<std::thread*>(handle);
    return TRUE;
}

std::uintptr_t SetTimer(HWND window, std::uintptr_t id, UINT milliseconds, LPVOID) {
    initialize_event_loop();
    const int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
        return 0;
    itimerspec value{};
    value.it_interval.tv_sec = milliseconds / 1000;
    value.it_interval.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
    value.it_value = value.it_interval;
    if (value.it_value.tv_sec == 0 && value.it_value.tv_nsec == 0)
        value.it_value.tv_nsec = 1;
    if (timerfd_settime(fd, 0, &value, nullptr) != 0) {
        close(fd);
        return 0;
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = fd;
    epoll_ctl(g_epoll, EPOLL_CTL_ADD, fd, &event);
    g_sources[fd] = EventSource{EventKind::Timer, fd, window, WM_TIMER, id, 0};
    g_timers[id] = fd;
    return id;
}
BOOL KillTimer(HWND, std::uintptr_t id) {
    const auto found = g_timers.find(id);
    if (found == g_timers.end())
        return FALSE;
    const int fd = found->second;
    epoll_ctl(g_epoll, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    g_sources.erase(fd);
    g_timers.erase(found);
    return TRUE;
}

BOOL GetMessage(MSG* message, HWND, UINT, UINT) {
    initialize_event_loop();
    while (!g_quitting.load()) {
        if (pop_message(message))
            return message->message != WM_QUIT;

        epoll_event events[32]{};
        const int count = epoll_wait(g_epoll, events, 32, -1);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        for (int index = 0; index < count; ++index) {
            const auto found = g_sources.find(events[index].data.fd);
            if (found == g_sources.end())
                continue;
            const EventSource source = found->second;
            if (source.kind == EventKind::Timer) {
                std::uint64_t expirations = 0;
                read(source.fd, &expirations, sizeof(expirations));
                queue_message(source.window, WM_TIMER, source.timer_id, 0);
            } else if (source.kind == EventKind::Socket) {
                const WORD event = selected_event(source, events[index].events);
                int socket_error = 0;
                if (events[index].events & EPOLLERR) {
                    socklen_t length = sizeof(socket_error);
                    getsockopt(source.fd, SOL_SOCKET, SO_ERROR, &socket_error, &length);
                }
                queue_message(source.window, source.message, static_cast<WPARAM>(source.fd),
                              MAKELPARAM(event, socket_error));
            } else {
                signalfd_siginfo signal{};
                read(source.fd, &signal, sizeof(signal));
                queue_message(g_main_window, WM_CLOSE, 0, 0);
            }
        }
    }
    if (message)
        *message = MSG{nullptr, WM_QUIT, static_cast<WPARAM>(g_exit_code), 0, timeGetTime(), {0, 0}};
    return FALSE;
}
BOOL TranslateMessage(const MSG*) { return TRUE; }
LRESULT DispatchMessage(const MSG* message) {
    if (!message || !g_window_proc)
        return 0;
    return g_window_proc(message->hwnd, message->message, message->wParam, message->lParam);
}
BOOL PostMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    queue_message(window, message, wparam, lparam);
    return TRUE;
}
void PostQuitMessage(int exit_code) {
    g_exit_code = exit_code;
    g_quitting.store(true);
    queue_message(nullptr, WM_QUIT, static_cast<WPARAM>(exit_code), 0);
}

int WSAStartup(WORD requested_version, WSADATA* data) {
    if (data) {
        data->wVersion = requested_version;
        data->wHighVersion = requested_version;
    }
    return 0;
}
int WSACleanup() { return 0; }
int WSAGetLastError() { return errno; }
int WSAAsyncSelect(SOCKET socket, HWND window, UINT message, long events) {
    initialize_event_loop();
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags >= 0)
        fcntl(socket, F_SETFL, flags | O_NONBLOCK);
    epoll_event event{};
    event.events = epoll_events(events);
    event.data.fd = socket;
    const int operation = g_sources.count(socket) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(g_epoll, operation, socket, &event) != 0)
        return SOCKET_ERROR;
    g_sources[socket] = EventSource{EventKind::Socket, socket, window, message, 0, events};
    return 0;
}
int closesocket(SOCKET socket) {
    if (g_epoll >= 0)
        epoll_ctl(g_epoll, EPOLL_CTL_DEL, socket, nullptr);
    g_sources.erase(socket);
    return close(socket);
}

RPC_STATUS UuidCreate(UUID* value) {
    if (!value)
        return EINVAL;
    std::random_device random;
    auto* bytes = reinterpret_cast<std::uint8_t*>(value);
    for (std::size_t index = 0; index < sizeof(UUID); ++index)
        bytes[index] = static_cast<std::uint8_t>(random());
    value->Data3 = static_cast<std::uint16_t>((value->Data3 & 0x0fff) | 0x4000);
    value->Data4[0] = static_cast<std::uint8_t>((value->Data4[0] & 0x3f) | 0x80);
    return RPC_S_OK;
}
RPC_STATUS UuidToStringA(const UUID* value, RPC_CSTR* text) {
    if (!value || !text)
        return EINVAL;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(8) << value->Data1 << '-'
           << std::setw(4) << value->Data2 << '-'
           << std::setw(4) << value->Data3 << '-'
           << std::setw(2) << static_cast<unsigned>(value->Data4[0])
           << std::setw(2) << static_cast<unsigned>(value->Data4[1]) << '-';
    for (int index = 2; index < 8; ++index)
        stream << std::setw(2) << static_cast<unsigned>(value->Data4[index]);
    const std::string value_string = stream.str();
    auto* result = static_cast<unsigned char*>(std::malloc(value_string.size() + 1));
    std::memcpy(result, value_string.c_str(), value_string.size() + 1);
    *text = result;
    return RPC_S_OK;
}
RPC_STATUS RpcStringFreeA(RPC_CSTR* text) {
    if (text && *text) {
        std::free(*text);
        *text = nullptr;
    }
    return RPC_S_OK;
}
