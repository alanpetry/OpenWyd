#include "Windows.h"

#include <cstdio>

extern int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show);

int main(int argc, char** argv) {
    (void)argc;
    std::fprintf(stdout, "[openwyd] starting headless server\n");
    std::fflush(stdout);
    return WinMain(nullptr, nullptr, argv && argv[0] ? argv[0] : nullptr, 0);
}
