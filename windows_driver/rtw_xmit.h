#pragma once

//
// Transmit Descriptor
//
typedef struct _TX_DESC {
    UINT32 Dword0;
    UINT32 Dword1;
    UINT32 Dword2;
    UINT32 Dword3;
    UINT32 Dword4;
    UINT32 Dword5;
} TX_DESC, *PTX_DESC;

VOID RtwFillTxDesc(PTX_DESC TxDesc, UINT32 BufferLen);
