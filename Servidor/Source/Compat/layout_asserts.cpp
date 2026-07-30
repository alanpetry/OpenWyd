#include "CPSock.h"
#include "Basedef.h"

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "OpenWyd server persistence and wire formats require a little-endian target"
#endif

static_assert(sizeof(HEADER) == 12, "wire packet header layout changed");
static_assert(sizeof(MSG_STANDARD) == 12, "standard packet header layout changed");
static_assert(sizeof(STRUCT_ITEM) == 8, "persisted item layout changed");
static_assert(sizeof(STRUCT_ACCOUNTINFO) == 216, "persisted account info layout changed");
static_assert(sizeof(STRUCT_MOB) == 1040, "persisted mob layout changed");
static_assert(sizeof(STRUCT_ACCOUNTFILE) == 8792, "persisted account file must remain 8,792 bytes");
static_assert(sizeof(MSG_AccountLogin) == 116, "login packet layout changed");
static_assert(sizeof(MSG_CharacterLogin) == 20, "character login packet layout changed");
static_assert(sizeof(MSG_CreateCharacter) == 48, "character creation packet layout changed");
