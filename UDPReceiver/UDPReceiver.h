#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

//System includes

//Library includes

//Local includes
#include "../SocketReceiverBase.h"
#include "../../../AVNUtilLibs/Sockets/InterruptibleBlockingSockets/InterruptibleBlockingUDPSocket.h"

class cUDPReceiver  : public cSocketReceiverBase
{
public:
    explicit cUDPReceiver(const std::string &strLocalInterface, uint16_t u16LocalPort = 60000, const std::string &strPeerAddress = std::string(""), uint16_t usPeerPort = 60001);

protected:
    //Socket
    cInterruptibleBlockingUDPSocket m_oUDPSocket;

    std::string                     m_strLocalInterface;
    uint16_t                        m_u16LocalPort;

    //Thread functions
    virtual void                    socketReceivingThreadFunction();
};

#endif // UDP_RECEIVER_H
