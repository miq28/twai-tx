#include "transport_iface.h"
#include "transport_tx_buffer.h"

class SerialTransport : public ITransport
{
public:
    void send(const uint8_t* data, int len) override
    {
        txPush(data, len);
    }
};

SerialTransport serialTransport;