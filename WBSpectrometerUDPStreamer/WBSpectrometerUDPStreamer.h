#ifndef WB_SPECTROMETER_UDP_STREAMER_H
#define WB_SPECTROMETER_UDP_STREAMER_H

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
#include <boost/scoped_ptr.hpp>
#endif

//Local includes
#include "AVNUtilLibs/DataStructures/ThreadSafeCircularBuffer/ThreadSafeCircularBuffer.h"
#include "AVNUtilLibs/Socket/InterruptableBlockingUDPSocket/InterruptableBlockingUDPSocket.h"

struct cRhinoStreamerMetaData
{
    unsigned char                               m_ucTotalPacketsPerContinousBlock; //0 = continous streaming
    unsigned char                               m_ucPacketNumber;
    unsigned char                               m_ucEthernetStatus;
    unsigned char                               m_ucRHINOEthernetReadBufferLevel;
    unsigned char                               m_ucRHINOEthernetWriteBufferLevel;
    bool                                        m_bOverFlowDetected;
    bool                                        m_bSequenceNumberErrorDetected;
    uint64_t                                    m_u64Timestamp_us;
    uint16_t                                    m_u16Channels;
};

class cWBSpectrometerUDPStreamer
{
public:
    explicit cWBSpectrometerUDPStreamer(const std::string &strRemoteAddress, uint16_t usRemotePort = 60001);
    ~cWBSpectrometerUDPStreamer();

    void startStreaming();
    void stopStreaming();

    void clearBuffers();

    static const unsigned int                   SYNC_WORD = 0xa1b2c3d4;

    bool readSamples(short *spBuffer, unsigned int uiNSamples, cRhinoStreamerMetaData &oMetaData);

private:
    std::string                                 m_strRemoteAddress;
    uint16_t                                    m_u16RemotePort;
    std::string                                 m_strLocalInterfaceAddress;
    uint16_t                                    m_u16LocalPort;

    bool                                        m_bStreamingEnabled;
    bool                                        m_bShutDown;

    cRhinoStreamerMetaData                      m_oLastMetadata;
    boost::mutex                                m_oLastMetadataMutex;

    //Socket
    cInterruptibleBlockUDPSocket                m_oUDPSocket;

    //Threads
    boost::scoped_ptr<boost::thread>            m_pSocketReceivingThread;
    boost::scoped_ptr<boost::thread>            m_pDataProcessingThread;

    //Thread functions
    void                                        socketReceivingThreadFunction();
    void                                        dataProcessingThreadFunction();

    void                                        getRawData(void* voidPtr, uint32_t u32Size_B);

    int32_t                                     m_i32GetRawDataInputBufferIndex;
    uint64_t                                    u64TotalBytesProcessed;

    //Circular buffers
    cThreadSafeCircularBuffer<char>             m_oInputBuffer;
    cThreadSafeCircularBuffer<int32_t>          m_oOutputBuffer;
};

#endif // WB_SPECTROMETER_UDP_STREAMER_H
