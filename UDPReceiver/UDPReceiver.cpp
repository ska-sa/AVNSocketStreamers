//System includes
#include <iostream>
#include <sstream>

//Library includes
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/asio/buffer.hpp>
#endif

//Local includes
#include "UDPReceiver.h"

using namespace std;

cUDPReceiver::cUDPReceiver(const string &strRemoteAddress, uint16_t u16RemotePort) :
    m_strRemoteAddress(strRemoteAddress),
    m_u16RemotePort(u16RemotePort),
    m_bReceivingEnabled(false),
    m_bCallbackOffloadingEnabled(false),
    m_bShutdownFlag(false),
    m_oUDPSocket(string("UDP socket")),
    m_pSocketReceivingThread(NULL),
    m_pDataOffloadingThread(NULL),
    m_i32GetRawDataInputBufferIndex(-1),
    m_oBuffer(64, 1040 * 16) //16 packets of 1040 bytes for each complex uint32_t FFT window of 2 channels or or I,Q,U,V uint32_t stokes parameters.
{
}

cUDPReceiver::~cUDPReceiver()
{
    stopReceiving();
    stopCallbackOffloading();

    shutdown();

    m_oUDPSocket.close();

    if(m_pSocketReceivingThread.get())
    {
        m_pSocketReceivingThread->join();
    }

    if(m_pDataOffloadingThread.get())
    {
        m_pDataOffloadingThread->join();
    }
}

void cUDPReceiver::clearBuffer()
{
    m_oBuffer.clear();
}

void cUDPReceiver::startReceiving()
{
    cout << "cUDPReceiver::startReceiving()" << endl;

    m_oUDPSocket.openAndConnectSocket(m_strRemoteAddress, m_u16RemotePort);
    u64TotalBytesProcessed = 0;

    {
        boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
        m_bReceivingEnabled = true;
    }

    m_i32GetRawDataInputBufferIndex = -1;

    clearBuffer();

    m_pSocketReceivingThread.reset(new boost::thread(&cUDPReceiver::socketReceivingThreadFunction, this));
}

void cUDPReceiver::stopReceiving()
{
    cout << "cUDPReceiver::stopReceiving()" << endl;

    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
    m_bReceivingEnabled = false;
}

void cUDPReceiver::startCallbackOffloading()
{
    cout << "cUDPReceiver::startCallbackOffloading()" << endl;

    {
        boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
        m_bCallbackOffloadingEnabled = true;
    }

    m_i32GetRawDataInputBufferIndex = -1;

    clearBuffer();

    m_pSocketReceivingThread.reset(new boost::thread(&cUDPReceiver::socketReceivingThreadFunction, this));
}

void cUDPReceiver::stopCallbackOffloading()
{
    cout << "cUDPReceiver::stopCallbackOffloading()" << endl;

    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
    m_bCallbackOffloadingEnabled = false;
}

bool  cUDPReceiver::isReceivingEnabled()
{
    boost::shared_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    return m_bReceivingEnabled;
}

bool  cUDPReceiver::isCallbackOffloadingEnabled()
{
    boost::shared_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    return m_bCallbackOffloadingEnabled;
}

void cUDPReceiver::shutdown()
{
    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
    m_bShutdownFlag = true;
}

bool cUDPReceiver::isShutdownRequested()
{
    boost::shared_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    return m_bShutdownFlag;
}

void cUDPReceiver::socketReceivingThreadFunction()
{
    boost::system::error_code oEC;

    uint32_t u32PacketsReceived = 0;
    int32_t i32BytesLastRead;
    int32_t i32BytesLeftToRead;

    while(isReceivingEnabled() && !isShutdownRequested())
    {
        //Get (or wait for) the next available element to write data to
        //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
        //This prevents the program locking up in this thread.
        int32_t i32Index = -1;
        while(i32Index == -1)
        {
            i32Index = m_oBuffer.getNextWriteIndex(500);

            //Also check for shutdown flag
            if(!isReceivingEnabled() || isShutdownRequested())
            {
                cout << "Exiting receiving thread." << endl;
                cout << "Received " << u32PacketsReceived << " packets." << endl;
                return;
            }
        }

        //Check that our buffer is large enough
        uint32_t u32UDPBytesAvailable = m_oUDPSocket.getBytesAvailable();
        if(u32UDPBytesAvailable > m_oBuffer.getElementPointer(i32Index)->allocationSize())
        {
            cout << "Warning: Input buffer element size is too small for UDP packet." << endl;
            cout << "Resizing to " << u32UDPBytesAvailable << " bytes" << endl;

            m_oBuffer.resize(m_oBuffer.getNElements(), u32UDPBytesAvailable);
        }

        //Read as many packets as can be fitted in to the buffer (it should be empty at this point)
        i32BytesLeftToRead = m_oBuffer.getElementPointer(i32Index)->allocationSize();

        while(i32BytesLeftToRead)
        {
            if(!m_oUDPSocket.receive(m_oBuffer.getElementDataPointer(i32Index) + m_oBuffer.getElementPointer(i32Index)->dataSize(), i32BytesLeftToRead) )
            {
                cout << "Warning socket error: " << m_oUDPSocket.getLastError().message() << endl;
            }
            else
            {
                i32BytesLastRead = m_oUDPSocket.getNBytesLastTransferred();
            }

            u32PacketsReceived++;

            i32BytesLeftToRead -= i32BytesLastRead;
            m_oBuffer.getElementPointer(i32Index)->setDataAdded(i32BytesLastRead);
        }
        //Signal we have completely filled an element of the input buffer.
        m_oBuffer.elementWritten();

        //cout << "Received " << iBytesLastRead << " bytes from UDP socket" << endl;
        //cout << "m_oInputBuffer (" << &m_oInputBuffer << ") element written. Level is now " << m_oInputBuffer.getLevel() << endl;
    }

    cout << "Exiting receiving thread." << endl;
    cout << "---- Received " << u32PacketsReceived << " packets ----" << endl;
    fflush(stdout);
}

uint32_t cUDPReceiver::getNextPacketSize_B()
{
    //Get (or wait for) the next available element to read data from
    //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
    //This prevents the program locking up in this thread.
    int32_t i32Index = -1;
    while(i32Index == -1)
    {
        i32Index = m_oBuffer.getNextReadIndex(500);

        //Also check for shutdown flag
        if(!isReceivingEnabled() || isShutdownRequested())
        {
            cout << "Aborting reading next packet from UDPReceiver." << endl;
            return false;
        }
    }

    return m_oBuffer.getElementPointer(i32Index)->allocationSize();
}

bool cUDPReceiver::getNextPacket(char *cpData, bool bPopData)
{
    //By setting pop data to false this function can be used to peak into the front of the queue. Otherwise it reads
    //data off the queue by default. Not bPopData = true should in most cases not be used concurrently with callback
    //based offloading as this will results in inconsistent data distribution.

    //Note cpData should be of sufficient size to store data. Check with getNextPacketSize_B()

    //Get (or wait for) the next available element to read data from
    //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
    //This prevents the program locking up in this thread.
    int32_t i32Index = -1;
    while(i32Index == -1)
    {
        i32Index = m_oBuffer.getNextReadIndex(500);

        //Also check for shutdown flag
        if(!isReceivingEnabled() || isShutdownRequested())
        {
            cout << "Aborting reading next packet from UDPReceiver." << endl;
            return false;
        }
    }

    memcpy(cpData, m_oBuffer.getElementDataPointer(i32Index), m_oBuffer.getElementPointer(i32Index)->allocationSize());

    if(bPopData)
    {
        if(m_bCallbackOffloadingEnabled)
        {
            cout << "Warning. Popping data while callback offloading is enabled. Data may be insistency distributed amongst destinations." << endl;
        }
        m_oBuffer.elementRead(); //Signal to pop element off FIFO
    }

    return true;
}

void cUDPReceiver::dataOffloadingThreadFunction()
{
    while(isCallbackOffloadingEnabled() && !isShutdownRequested())
    {
        //Get (or wait for) the next available element to read data from
        //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
        //This prevents the program locking up in this thread.
        int32_t i32Index = -1;
        while(i32Index == -1)
        {
            i32Index = m_oBuffer.getNextReadIndex(500);

            //Also check for shutdown flag
            if(!m_bCallbackOffloadingEnabled || isShutdownRequested())
            {
                cout << "Aborting reading next packet from UDPReceiver." << endl;
                return;
            }
        }

        for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size(); ui++)
        {
            if(m_vpCallbackHandlers[ui])
            {
                m_vpCallbackHandlers[ui]->offloadData_callback(m_oBuffer.getElementDataPointer(i32Index), m_oBuffer.getElementPointer(i32Index)->allocationSize());
            }
            else
            {
                //Remove the element if the pointer is null
                m_vpCallbackHandlers.erase(m_vpCallbackHandlers.begin() + ui);
                ui--;
            }
        }

        m_oBuffer.elementRead(); //Signal to pop element off FIFO
    }
}

void cUDPReceiver::registerCallbackHandler(boost::shared_ptr<cUDPReceiverCallbackInterface> pNewHandler)
{
    m_vpCallbackHandlers.push_back(pNewHandler);
}

void cUDPReceiver::deregisterCallbackHandler(boost::shared_ptr<cUDPReceiverCallbackInterface> pHandler)
{
    //Search for matching pointer values and erase
    for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size(); ui++)
    {
        if(m_vpCallbackHandlers[ui].get() == pHandler.get())
            m_vpCallbackHandlers.erase(m_vpCallbackHandlers.begin() + ui);
    }
}
