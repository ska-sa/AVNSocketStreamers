#ifndef TCP_RECEIVER_H
#define TCP_RECEIVER_H

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
#include <boost/shared_array.hpp>
#include <boost/thread/shared_mutex.hpp>
#endif

//Local includes
#include "../../../AVNUtilLibs/DataStructures/ThreadSafeCircularBuffer/ThreadSafeCircularBuffer.h"
#include "../../../AVNUtilLibs/Sockets/InterruptibleBlockingSockets/InterruptibleBlockingTCPSocket.h"

class cTCPReceiver
{
public:
    class cTCPReceiverCallbackInterface
    {
    public:
        virtual bool offloadData_callback(char* pData, uint32_t u32Size_B) = 0;
    };


    explicit cTCPReceiver(const std::string &strPeerAddress, uint16_t usPeerPort = 60001);
    ~cTCPReceiver();

    void                                                            startReceiving();
    void                                                            stopReceiving();

    void                                                            startCallbackOffloading();
    void                                                            stopCallbackOffloading();

    bool                                                            isReceivingEnabled();
    bool                                                            isCallbackOffloadingEnabled();

    void                                                            shutdown();
    bool                                                            isShutdownRequested();

    void                                                            clearBuffer();

    int32_t getNextPacketSize_B(uint32_t u32Timeout_ms = 0);
    bool                                                            getNextPacket(char *cpData, uint32_t u32Timeout_ms = 0, bool bPopData = true);

    void                                                            registerCallbackHandler(boost::shared_ptr<cTCPReceiverCallbackInterface> pNewHandler);
    void                                                            deregisterCallbackHandler(boost::shared_ptr<cTCPReceiverCallbackInterface> pHandler);

private:
    std::string                                                     m_strPeerAddress;
    uint16_t                                                        m_u16PeerPort;

    bool                                                            m_bReceivingEnabled;
    bool                                                            m_bCallbackOffloadingEnabled;
    bool                                                            m_bShutdownFlag;
    boost::shared_mutex                                             m_bFlagMutex;
    boost::shared_mutex                                             m_oCallbackHandlersMutex;

    //Callback handlers
    std::vector<boost::shared_ptr<cTCPReceiverCallbackInterface> >  m_vpCallbackHandlers;

    //Socket
    cInterruptibleBlockingTCPSocket                                 m_oSocket;

    //Threads
    boost::scoped_ptr<boost::thread>                                m_pSocketReceivingThread;
    boost::scoped_ptr<boost::thread>                                m_pDataOffloadingThread;

    //Thread functions
    void                                                            socketReceivingThreadFunction();
    void                                                            dataOffloadingThreadFunction();

    int32_t                                                         m_i32GetRawDataInputBufferIndex;
    uint64_t                                                        u64TotalBytesProcessed;

    //Circular buffers
    cThreadSafeCircularBuffer<char>                                 m_oBuffer;
};

#endif // TCP_RECEIVER_H
