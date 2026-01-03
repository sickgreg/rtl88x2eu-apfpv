#include <ntddk.h>
#include "rtw_xmit.h"

VOID RtwFillTxDesc(PTX_DESC TxDesc, UINT32 BufferLen)
{
    // A more complete implementation would set various fields
    // in the descriptor, such as sequence number, rate, etc.
    TxDesc->Dword0 = BufferLen & 0xFFF;
    TxDesc->Dword1 = 0;
    TxDesc->Dword2 = 0;
    TxDesc->Dword3 = 0;
    TxDesc->Dword4 = 0;
    TxDesc->Dword5 = 0;
}
