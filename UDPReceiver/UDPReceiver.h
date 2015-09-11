#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

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
#include "../../../AVNUtilLibs/Socket/InterruptableBlockingSockets/InterruptableBlockingUDPSocket.h"

class cUDPReceiver
{
public:
    class cUDPReceiverCallbackInterface
    {
    public:
        virtual bool offloadData_callback(char* pData, uint32_t u32Size_B) = 0;
    };


    explicit cUDPReceiver(const std::string &strRemoteAddress, uint16_t usRemotePort = 60001);
    ~cUDPReceiver();

    void                                                            startReceiving();
    void                                                            stopReceiving();

    void                                                            startCallbackOffloading();
    void                                                            stopCallbackOffloading();

    bool                                                            isReceivingEnabled();
    bool                                                            isCallbackOffloadingEnabled();

    void                                                            shutdown();
    bool                                                            isShutdownRequested();

    void                                                            clearBuffer();

    uint32_t                                                        getNextPacketSize_B();
    bool                                                            getNextPacket(char *cpData, bool bPopData = true);

    void                                                            registerCallbackHandler(boost::shared_ptr<cUDPReceiverCallbackInterface> pNewHandler);
    void                                                            deregisterCallbackHandler(boost::shared_ptr<cUDPReceiverCallbackInterface> pHandler);

    static const unsigned int                                       SYNC_WORD = 0xa1b2c3d4;

private:
    std::string                                                     m_strRemoteAddress;
    uint16_t                                                        m_u16RemotePort;
    std::string                                                     m_strLocalInterfaceAddress;
    uint16_t                                                        m_u16LocalPort;

    bool                                                            m_bReceivingEnabled;
    bool                                                            m_bCallbackOffloadingEnabled;
    bool                                                            m_bShutdownFlag;
    boost::shared_mutex                                             m_bFlagMutex;

    //Callback handlers
    std::vector<boost::shared_ptr<cUDPReceiverCallbackInterface> >  m_vpCallbackHandlers;

    //Socket
    cInterruptibleBlockingUDPSocket                                 m_oUDPSocket;

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

#endif // UDP_RECEIVER_H
