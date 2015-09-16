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
#include <boost/shared_ptr.hpp>
#include <boost/thread/shared_mutex.hpp>
#endif

//Local includes
#include "../../../AVNUtilLibs/DataStructures/ThreadSafeCircularBuffer/ThreadSafeCircularBuffer.h"
#include "../../../AVNUtilLibs/Sockets/InterruptibleBlockingSockets/InterruptibleBlockingUDPSocket.h"
#include "../../../AVNUtilLibs/Sockets/InterruptibleBlockingSocketAcceptors/InterruptibleBlockingTCPAcceptor.h"
#include "ConnectionThread.h"

class cTCPServer
{
public:
    cTCPServer(const std::string &strInterface = std::string("0.0.0.0"), uint16_t usPort = 60001, uint32_t u32MaxConnections = 0);
    virtual ~cTCPServer();

    bool                                                writeData(char* cpData, uint32_t u32Size_B);

    void                                                shutdown();
    bool                                                isShutdownRequested();

protected:
    bool                                                m_bShutdownFlag;
    boost::shared_mutex                                 m_bShutdownFlagMutex;

    uint32_t                                            m_u32MaxConnections;
    std::string                                         m_strInterface;
    uint16_t                                            m_u16Port;

    cInterruptibleBlockingTCPAcceptor                   m_oTCPAcceptor;

    std::vector<boost::shared_ptr<cConnectionThread> >  m_vpConnectionThreads;
    boost::shared_mutex                                 m_oConnectThreadsMutex;

    void socketListeningThreadFunction();

    boost::scoped_ptr<boost::thread>                    m_pSocketListeningThread;
};

#endif //TCP_SERVER_H
