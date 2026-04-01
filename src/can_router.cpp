#include "can_router.h"
#include "can_rx_buffer.h"
#include "app_mode.h"

// forward (no coupling)
void analyzerPush(const CANRxItem& item);
void gvretPush(const CANRxItem& item);

void canRouterProcess()
{
    CANRxItem item;
    int budget = 128;

    while (budget-- && rxBufferPop(item))
    {
        switch (appState.mode)
        {
        case MODE_ANALYZER:
            analyzerPush(item);
            break;

        case MODE_SAVVYCAN:
            gvretPush(item);
            break;

        default:
            break;
        }
    }
}