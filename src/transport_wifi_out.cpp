#include "transport_iface.h"

class WiFiTransport : public ITransport
{
public:
    void send(const uint8_t* data, int len) override
    {
        // later: socket send (non-blocking)
    }
};

WiFiTransport wifiTransport;