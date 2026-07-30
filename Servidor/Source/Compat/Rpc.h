#pragma once

#include <cstdint>

using RPC_STATUS = int;
using RPC_CSTR = unsigned char*;

struct UUID {
    std::uint32_t Data1;
    std::uint16_t Data2;
    std::uint16_t Data3;
    std::uint8_t Data4[8];
};

constexpr RPC_STATUS RPC_S_OK = 0;

RPC_STATUS UuidCreate(UUID* value);
RPC_STATUS UuidToStringA(const UUID* value, RPC_CSTR* text);
RPC_STATUS RpcStringFreeA(RPC_CSTR* text);
