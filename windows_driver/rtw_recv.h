#pragma once

//
// Receive Descriptor
//
typedef struct _RX_DESC {
    UINT32 Dword0;
    UINT32 Dword1;
    UINT32 Dword2;
    UINT32 Dword3;
    UINT32 Dword4;
    UINT32 Dword5;
} RX_DESC, *PRX_DESC;

#define RX_DESC_PKT_LEN_MASK 0x3FFF
#define RX_DESC_PKT_LEN_SHIFT 0
