#ifndef TCP_SERVER_H
#define TCP_SERVER_H

//System includes
#ifdef _WIN32
#include <stdint.h>

#ifndef int64_t
typedef __int64 int64_t;
#endif

#ifndef uint64_t
typedef unsigned __int64 uint64_t;
#endif

#else
#include <inttypes.h>
#endif

#include <vector>

//Library include:
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_service.hpp>
#endif

//Local includes
#include "../../../AVNUtilLibs/DataStructures/ThreadSafeCircularBuffer/ThreadSafeCircularBuffer.h"
#include "../../../AVNUtilLibs/Socket/InterruptableBlockingSockets/InterruptableBlockingUDPSocket.h"
#include "../UDPReceiver/UDPReceiver.h"
#include "ConnectionThread.h"

class cTCPServer : public cUDPReceiver::cUDPReceiverCallbackInterface
{
public:
    explicit cTCPServer(const std::string &strInterface = std::string("0.0.0.0"), uint16_t usPort = 60001, uint32_t u32MaxConnections = 0);
    ~cTCPServer();

    bool                                                offloadData_callback(char* cpData, uint32_t u32Size_B);

    void                                                shutdown();
    bool                                                isShutdownRequested();

private:
    bool                                                m_bShutdownFlag;
    boost::shared_mutex                                 m_bShutdownFlagMutex;

    uint32_t                                            m_u32MaxConnections;
    std::string                                         m_strInterface;
    uint16_t                                            m_u16Port;

    boost::asio::ip::tcp::acceptor                      m_oTCPAcceptor;
    boost::asio::io_service                             m_oIOService;

    std::vector<boost::scoped_ptr<cConnectionThread> >  m_vpConnectionThreads;
    boost::shared_mutex                                 m_oConnectThreadsMutex;

    void socketListeningThreadFunction();

    boost::scoped_ptr<boost::thread>                    m_pSocketListeningThread;
};

#endif //TCP_SERVER_H
