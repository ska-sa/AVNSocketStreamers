#ifndef SOCKET_RECEIVER_BASE_H
#define SOCKET_RECEIVER_BASE_H

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
#include "../../AVNUtilLibs/DataStructures/ThreadSafeCircularBuffer/ThreadSafeCircularBuffer.h"

class cSocketReceiverBase
{
public:
    class cSocketReceiverBaseCallbackInterface
    {
    public:
        virtual bool offloadData_callback(char* pData, uint32_t u32Size_B) = 0;
    };


    explicit cSocketReceiverBase(const std::string &strPeerAddress, uint16_t usPeerPort = 60001);
    virtual ~cSocketReceiverBase();

    void                                                                    startReceiving();
    virtual void                                                            stopReceiving();

    void                                                                    startCallbackOffloading();
    void                                                                    stopCallbackOffloading();

    bool                                                                    isReceivingEnabled();
    bool                                                                    isCallbackOffloadingEnabled();

    void                                                                    shutdown();
    bool                                                                    isShutdownRequested();

    void                                                                    clearBuffer();

    int32_t getNextPacketSize_B(uint32_t u32Timeout_ms = 0);
    bool                                                                    getNextPacket(char *cpData, uint32_t u32Timeout_ms = 0, bool bPopData = true);

    void                                                                    registerCallbackHandler(boost::shared_ptr<cSocketReceiverBaseCallbackInterface> pNewHandler);
    void                                                                    deregisterCallbackHandler(boost::shared_ptr<cSocketReceiverBaseCallbackInterface> pHandler);

protected:
    std::string                                                             m_strPeerAddress;
    uint16_t                                                                m_u16PeerPort;

    bool                                                                    m_bReceivingEnabled;
    bool                                                                    m_bCallbackOffloadingEnabled;
    bool                                                                    m_bShutdownFlag;
    boost::shared_mutex                                                     m_oFlagMutex;
    boost::shared_mutex                                                     m_oCallbackHandlersMutex;

    //Callback handlers
    std::vector<boost::shared_ptr<cSocketReceiverBaseCallbackInterface> >   m_vpCallbackHandlers;

    //Threads
    boost::scoped_ptr<boost::thread>                                        m_pSocketReceivingThread;
    boost::scoped_ptr<boost::thread>                                        m_pDataOffloadingThread;

    //Derived class will need some sort of socket here.

    //Thread functions
    virtual void                                                            socketReceivingThreadFunction() = 0; //Implement socket receiving here
    void                                                                    dataOffloadingThreadFunction();

    int32_t                                                                 m_i32GetRawDataInputBufferIndex;
    uint64_t                                                                u64TotalBytesProcessed;

    //Circular buffers
    cThreadSafeCircularBuffer<char>                                         m_oBuffer;

};

#endif // SOCKET_RECEIVER_BASE_H
