#ifndef CONNECTION_THREAD_H
#define CONNECTION_THREAD_H

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

//Library include:
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#endif

//Local includes
#include "../../../AVNUtilLibs/DataStructures/ThreadSafeCircularBuffer/ThreadSafeCircularBuffer.h"
#include "../../../AVNUtilLibs/Sockets/InterruptibleBlockingSockets/InterruptibleBlockingTCPSocket.h"
#include "../UDPReceiver/UDPReceiver.h"

class cConnectionThread
{
public:
    explicit cConnectionThread(boost::shared_ptr<cInterruptibleBlockingTCPSocket> pClientSocket);
    ~cConnectionThread();

    bool                                                tryAddDataToSend(char* cpData, uint32_t u32Size_B);
    void                                                blockingAddDataToSend(char* cpData, uint32_t u32Size_B);

    bool                                                isValid();
    void                                                setInvalid();

    void                                                shutdown();
    bool                                                isShutdownRequested();

    std::string                                         getPeerAddress();
    std::string                                         getSocketName();

private:
    std::string                                         m_strPeerAddress;

    bool                                                m_bShutdownFlag;
    boost::shared_mutex                                 m_bShutdownFlagMutex;

    //Thread functions
    void                                                socketWritingThreadFunction();

    //Threads
    boost::scoped_ptr<boost::thread>                   m_pSocketWritingThread;

    boost::shared_ptr<cInterruptibleBlockingTCPSocket> m_pSocket;

    bool                                               m_bIsValid;
    boost::shared_mutex                                m_bValidMutex;

    //Circular buffers
    cThreadSafeCircularBuffer<char>                    m_oBuffer;

};

#endif //CONNECTION_THREAD_H
