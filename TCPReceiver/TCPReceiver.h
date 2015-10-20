#ifndef TCP_RECEIVER_H
#define TCP_RECEIVER_H

//System includes

//Library includes

//Local includes
#include "../SocketReceiverBase.h"
#include "../../../AVNUtilLibs/Sockets/InterruptibleBlockingSockets/InterruptibleBlockingTCPSocket.h"

class cTCPReceiver : public cSocketReceiverBase
{
public:
    explicit cTCPReceiver(const std::string &strPeerAddress, uint16_t usPeerPort = 60001);

protected:
    //TCP Socket
    cInterruptibleBlockingTCPSocket m_oSocket;

    //Thread functions
    virtual void                    socketReceivingThreadFunction();
};

#endif // TCP_RECEIVER_H
