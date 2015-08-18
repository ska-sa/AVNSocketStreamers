//System includes
#include <iostream>
#include <sstream>

//Library includes
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/asio/buffer.hpp>
#endif

//Local includes
#include "WBSpectrometerUDPStreamer.h"

using namespace std;

cWBSpectrometerUDPStreamer::cWBSpectrometerUDPStreamer(const string &strRemoteAddress, uint16_t u16RemotePort) :
    m_strRemoteAddress(strRemoteAddress),
    m_u16RemotePort(u16RemotePort),
    m_bStreamingEnabled(false),
    m_bShutDown(false),
    m_oUDPSocket(string("UDP socket")),
    m_pSocketReceivingThread(NULL),
    m_pDataProcessingThread(NULL),
    m_i32GetRawDataInputBufferIndex(-1),
    m_oInputBuffer(64, 1040 * 16), //16 packets of 1040 bytes for each complex uint32_t FFT window of 2 channels or or I,Q,U,V uint32_t stokes parameters.
    m_oOutputBuffer(16, 1024 * 4)
{
    m_oUDPSocket.openAndConnectSocket(strRemoteAddress, u16RemotePort);
    u64TotalBytesProcessed = 0;
}

cWBSpectrometerUDPStreamer::~cWBSpectrometerUDPStreamer()
{
    if(m_bStreamingEnabled)
        stopStreaming();

    m_bShutDown = true;
    m_oUDPSocket.close();

    if(m_pSocketReceivingThread.get())
    {
        m_pSocketReceivingThread->join();
    }

    if(m_pDataProcessingThread.get())
    {
        m_pDataProcessingThread->join();
    }
}

void cWBSpectrometerUDPStreamer::clearBuffers()
{
    m_oInputBuffer.clear();
    m_oOutputBuffer.clear();
}

void cWBSpectrometerUDPStreamer::startStreaming()
{
    cout << "cWBSpectrometerUDPStreamer::startStreaming()" << endl;

    m_bStreamingEnabled = true;
    m_i32GetRawDataInputBufferIndex = -1;

    clearBuffers();

    m_pDataProcessingThread.reset(new boost::thread(&cWBSpectrometerUDPStreamer::dataProcessingThreadFunction, this));
    m_pSocketReceivingThread.reset(new boost::thread(&cWBSpectrometerUDPStreamer::socketReceivingThreadFunction, this));
}

void cWBSpectrometerUDPStreamer::stopStreaming()
{
    cout << "cWBSpectrometerUDPStreamer::stopStreaming()" << endl;
    m_bStreamingEnabled = false;
}

void cWBSpectrometerUDPStreamer::socketReceivingThreadFunction()
{
    boost::system::error_code oEC;

    uint32_t u32PacketsReceived = 0;
    int32_t i32BytesLastRead;
    int32_t i32BytesLeftToRead;

    while(m_bStreamingEnabled && !m_bShutDown)
    {
        //Get (or wait for) the next available element to write data to
        //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
        //This prevents the program locking up in this thread.
        int32_t i32Index = -1;
        while(i32Index == -1)
        {
            i32Index = m_oInputBuffer.getNextWriteIndex(500);

            //Also check for shutdown flag
            if(!m_bStreamingEnabled || m_bShutDown)
            {
                cout << "Exiting receiving thread." << endl;
                cout << "Received " << u32PacketsReceived << " packets." << endl;
                return;
            }
        }

        //Check that our buffer is large enough
        uint32_t u32UDPBytesAvailable = m_oUDPSocket.getBytesAvailable();
        if(u32UDPBytesAvailable > m_oInputBuffer.getElementPointer(i32Index)->allocationSize())
        {
            cout << "Warning: Input buffer element size is too small for UDP packet." << endl;
            cout << "Resizing to " << u32UDPBytesAvailable << " bytes" << endl;

            m_oInputBuffer.resize(m_oInputBuffer.getNElements(), u32UDPBytesAvailable);
        }

        //Read as many packets as can be fitted in to the buffer (it should be empty at this point)
        i32BytesLeftToRead = m_oInputBuffer.getElementPointer(i32Index)->allocationSize();

        while(i32BytesLeftToRead)
        {
            if(!m_oUDPSocket.receive(m_oInputBuffer.getElementDataPointer(i32Index) + m_oInputBuffer.getElementPointer(i32Index)->dataSize(), i32BytesLeftToRead) )
            {
                cout << "Warning socket error: " << m_oUDPSocket.getLastError().message() << endl;
            }
            else
            {
                i32BytesLastRead = m_oUDPSocket.getNBytesLastTransferred();
            }

            u32PacketsReceived++;

            i32BytesLeftToRead -= i32BytesLastRead;
            m_oInputBuffer.getElementPointer(i32Index)->setDataAdded(i32BytesLastRead);
        }
        //Signal we have completely filled an element of the input buffer.
        m_oInputBuffer.elementWritten();

        //cout << "Received " << iBytesLastRead << " bytes from UDP socket" << endl;
        //cout << "m_oInputBuffer (" << &m_oInputBuffer << ") element written. Level is now " << m_oInputBuffer.getLevel() << endl;
    }
    cout << "Exiting receiving thread." << endl;
    cout << "---- Received " << u32PacketsReceived << " packets ----" << endl;
    fflush(stdout);
}

void cWBSpectrometerUDPStreamer::dataProcessingThreadFunction()
{

//#ifdef _WIN32
//    else if(uiSyncWordTest == _byteswap_ulong(SYNC_WORD))
//#else
//    else if(uiSyncWordTest == __builtin_bswap32(SYNC_WORD))
//#endif
//    {
//        cout << "Error you platform is big endian. This is not current supported by this software." << endl;
//        exit(1);
//    }
}

bool cWBSpectrometerUDPStreamer::readSamples(short* spBuffer, uint32_t u32NSamples, cRhinoStreamerMetaData &oMetaData)
{
    int32_t i32Index = -1;
    while(i32Index == -1)
    {
        i32Index = m_oOutputBuffer.getNextReadIndex(500);

        //Also check for shutdown flag
        if(!m_bStreamingEnabled || m_bShutDown)
            return false;
    }
    //cout << "cWBSpectrometerUDPStreamer::readSamples() Got output index: " << i32Index << endl;

    uint32_t u32SamplesLeftToCopy = u32NSamples;

    while(u32SamplesLeftToCopy)
    {

        if(m_oOutputBuffer.getElementPointer(i32Index)->dataSize() > u32SamplesLeftToCopy)
        {
            //If there is more data in the output buffer element than what the user requested

            //Filled the users buffer
            memcpy(spBuffer, m_oOutputBuffer.getElementPointer(i32Index)->getDataStartPointer(), u32SamplesLeftToCopy * sizeof(int16_t));

            //Mark the data copied from the output buffer as used
            m_oOutputBuffer.getElementPointer(i32Index)->setDataUsed(u32NSamples);

            u32SamplesLeftToCopy = 0;
        }
        else
        {
            //If there is space in the users buffer for more 1 or more of our output buffer elements

            //Filled the users buffer
            memcpy(spBuffer, m_oOutputBuffer.getElementPointer(i32Index)->getDataStartPointer(), m_oOutputBuffer.getElementPointer(i32Index)->dataSize() * sizeof(int16_t));
            spBuffer += m_oOutputBuffer.getElementPointer(i32Index)->dataSize();

            //Reduce the number of samples left to the copy to users buffer accordingly
            u32SamplesLeftToCopy -= m_oOutputBuffer.getElementPointer(i32Index)->dataSize();

            //Mark the element of the output buffer as completely consumed
            m_oOutputBuffer.getElementPointer(i32Index)->setEmpty();
            m_oOutputBuffer.elementRead();

            //Get the index of the next element of the output buffer to use
            i32Index = -1;
            while(i32Index == -1)
            {
                i32Index = m_oOutputBuffer.getNextReadIndex(500);

                //Also check for shutdown flag
                if(!m_bStreamingEnabled || m_bShutDown)
                    return false;
            }
        }
    }

    //Copy metadata
    boost::unique_lock<boost::mutex> oLock(m_oLastMetadataMutex);
    oMetaData = m_oLastMetadata;

    return true;
}
