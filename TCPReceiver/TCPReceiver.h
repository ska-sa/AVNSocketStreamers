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
    class cNotificationCallbackInterface
    {
    public:
        //Callbacks to notify about connectiviy
        virtual void                                                    socketConnected_callback() = 0;
        virtual void                                                    socketDisconnected_callback() = 0;
    };

    explicit cTCPReceiver(const std::string &strPeerAddress, uint16_t usPeerPort = 60001);
    virtual ~cTCPReceiver();

    virtual void                                                        stopReceiving();

    void                                                                registerNoticationCallbackHandler(boost::shared_ptr<cNotificationCallbackInterface> pNewHandler);
    void                                                                deregisterNotificationCallbackHandler(boost::shared_ptr<cNotificationCallbackInterface> pHandler);

protected:
    //TCP Socket
    cInterruptibleBlockingTCPSocket                                     m_oSocket;

    //Callback handlers
    std::vector<boost::shared_ptr<cNotificationCallbackInterface> >     m_vpNotificationCallbackHandlers;

    //Thread functions
    virtual void                                                        socketReceivingThreadFunction();

};

#endif // TCP_RECEIVER_H
