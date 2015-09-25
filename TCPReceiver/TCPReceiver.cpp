//System includes
#include <iostream>
#include <sstream>

//Library includes
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/asio/buffer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#endif

//Local includes
#include "TCPReceiver.h"

using namespace std;

cTCPReceiver::cTCPReceiver(const string &strPeerAddress, uint16_t u16PeerPort) :
    m_strPeerAddress(strPeerAddress),
    m_u16PeerPort(u16PeerPort),
    m_bReceivingEnabled(false),
    m_bCallbackOffloadingEnabled(false),
    m_bShutdownFlag(false),
    m_oSocket(string("TCP socket")),
    m_pSocketReceivingThread(NULL),
    m_pDataOffloadingThread(NULL),
    m_i32GetRawDataInputBufferIndex(-1),
    m_oBuffer(1024, 1040)
{
}

cTCPReceiver::~cTCPReceiver()
{
    stopReceiving();
    stopCallbackOffloading();

    shutdown();

    m_oSocket.close();

    if(m_pSocketReceivingThread.get())
    {
        m_pSocketReceivingThread->join();
    }

    if(m_pDataOffloadingThread.get())
    {
        m_pDataOffloadingThread->join();
    }
}

void cTCPReceiver::clearBuffer()
{
    m_oBuffer.clear();
}

void cTCPReceiver::startReceiving()
{
    cout << "cTCPReceiver::startReceiving()" << endl;

    u64TotalBytesProcessed = 0;

    {
        boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
        m_bReceivingEnabled = true;
    }

    m_i32GetRawDataInputBufferIndex = -1;

    clearBuffer();

    m_pSocketReceivingThread.reset(new boost::thread(&cTCPReceiver::socketReceivingThreadFunction, this));
}

void cTCPReceiver::stopReceiving()
{
    cout << "cTCPReceiver::stopReceiving()" << endl;

    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
    m_bReceivingEnabled = false;
}

void cTCPReceiver::startCallbackOffloading()
{
    cout << "cTCPReceiver::startCallbackOffloading()" << endl;

    {
        boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
        m_bCallbackOffloadingEnabled = true;
    }

    m_i32GetRawDataInputBufferIndex = -1;

    clearBuffer();

    m_pSocketReceivingThread.reset(new boost::thread(&cTCPReceiver::dataOffloadingThreadFunction, this));
}

void cTCPReceiver::stopCallbackOffloading()
{
    cout << "cTCPReceiver::stopCallbackOffloading()" << endl;

    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
    m_bCallbackOffloadingEnabled = false;
}

bool  cTCPReceiver::isReceivingEnabled()
{
    boost::shared_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    return m_bReceivingEnabled;
}

bool  cTCPReceiver::isCallbackOffloadingEnabled()
{
    boost::shared_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    return m_bCallbackOffloadingEnabled;
}

void cTCPReceiver::shutdown()
{
    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
    m_bShutdownFlag = true;
}

bool cTCPReceiver::isShutdownRequested()
{
    boost::shared_lock<boost::shared_mutex>  oLock(m_bFlagMutex);
    return m_bShutdownFlag;
}

void cTCPReceiver::socketReceivingThreadFunction()
{
    cout << "Entered cTCPReceiver::socketReceivingThreadFunction()" << endl;

    //First attempt to connect socket
    while(!m_oSocket.openAndConnect(m_strPeerAddress, m_u16PeerPort))
    {
        if(isShutdownRequested() || !isReceivingEnabled())
        {
            cout << "cTCPReceiver::socketReceivingThreadFunction(): Got shutdown flag, returning." << endl;
            return;
        }

        //Wait some time then try to bind again...
        boost::this_thread::sleep(boost::posix_time::milliseconds(2000));
        cout << "cTCPReceiver::socketReceivingThreadFunction(): Retrying socket connection to " << m_strPeerAddress << ":" << m_u16PeerPort << endl;
    }

    //Enter thread loop, repeated reading into the FIFO

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
                cout << "cTCPReceiver::socketReceivingThread(): Exiting receiving thread." << endl;
                cout << "---- Received " << u32PacketsReceived << " packets. ----" << endl;
                return;
            }
        }

        //Read as many packets as can be fitted in to the buffer (it should be empty at this point)
        i32BytesLeftToRead = m_oBuffer.getElementPointer(i32Index)->allocationSize();

        while(i32BytesLeftToRead)
        {
            if(!m_oSocket.receive(m_oBuffer.getElementDataPointer(i32Index) + m_oBuffer.getElementPointer(i32Index)->dataSize(), i32BytesLeftToRead) )
            {
                cout << "cTCPReceiver::socketReceivingThread(): Warning socket error: " << m_oSocket.getLastError().message() << endl;
            }
            else
            {
                i32BytesLastRead = m_oSocket.getNBytesLastTransferred();
            }

            u32PacketsReceived++;

            i32BytesLeftToRead -= i32BytesLastRead;
            m_oBuffer.getElementPointer(i32Index)->setDataAdded(i32BytesLastRead);

            //Also check for shutdown flag
            if(!isReceivingEnabled() || isShutdownRequested())
            {
                cout << "cTCPReceiver::socketReceivingThread(): Exiting receiving thread." << endl;
                cout << "---- Received " << u32PacketsReceived << " packets. ----" << endl;
                return;
            }
        }
        //Signal we have completely filled an element of the input buffer.
        m_oBuffer.elementWritten();
    }

    cout << "cTCPReceiver::socketReceivingThread(): Exiting receiving thread." << endl;
    cout << "---- Received " << u32PacketsReceived << " packets ----" << endl;
    fflush(stdout);
}

int32_t cTCPReceiver::getNextPacketSize_B(uint32_t u32Timeout_ms)
{
    //Get (or wait for) the next available element to read data from
    //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
    //This prevents the program locking up in this thread.

    //Current time:
    boost::posix_time::ptime oStartTime = boost::posix_time::microsec_clock::local_time();

    int32_t i32Index = -1;
    while(i32Index == -1)
    {
        boost::posix_time::time_duration oDuration = boost::posix_time::microsec_clock::local_time() - oStartTime;
        if(u32Timeout_ms && oDuration.total_milliseconds() >= u32Timeout_ms)
        {
            cout << "cTCPReceiver::getNextPacketSize_B(): Hit caller specified timeout. Returning." << endl;
            return -1;
        }

        i32Index = m_oBuffer.getNextReadIndex(100);

        //Also check for shutdown flag
        if(!isReceivingEnabled() || isShutdownRequested())
        {
            cout << "cTCPReceiver::getNextPacketSize_B(): Got stop flag. Aborting..." << endl;
            return -1;
        }
    }

    return m_oBuffer.getElementPointer(i32Index)->allocationSize();
}

bool cTCPReceiver::getNextPacket(char *cpData, uint32_t u32Timeout_ms, bool bPopData)
{
    //By setting pop data to false this function can be used to peak into the front of the queue. Otherwise it reads
    //data off the queue by default. Note bPopData = true should probably not be used concurrently with callback based
    //offloading as this will results in inconsistent data distribution.

    //Note cpData should be of sufficient size to store data. Check with getNextPacketSize_B()

    //Get (or wait for) the next available element to read data from
    //If waiting timeout every 100 ms and check for shutdown or stop streaming flags
    //This prevents the program locking up in this thread.

    //Current time:
    boost::posix_time::ptime oStartTime = boost::posix_time::microsec_clock::local_time();

    int32_t i32Index = -1;
    while(i32Index == -1)
    {
        boost::posix_time::time_duration oDuration = boost::posix_time::microsec_clock::local_time() - oStartTime;
        if(u32Timeout_ms && oDuration.total_milliseconds() >= u32Timeout_ms)
        {
            cout << "cTCPReceiver::getNextPacket(): Hit caller specified timeout. Returning." << endl;
            return false;
        }

        i32Index = m_oBuffer.getNextReadIndex(100);

        if(i32Index == -1)
            cout << "Got semphore timeout." << endl;

        //Also check for shutdown flag
        if(!isReceivingEnabled() || isShutdownRequested())
        {
            cout << "cTCPReceiver::getNextPacket(): Got stop flag. Aborting..." << endl;
            return false;
        }
    }

    memcpy(cpData, m_oBuffer.getElementDataPointer(i32Index), m_oBuffer.getElementPointer(i32Index)->allocationSize());

    if(bPopData)
    {
        if(m_bCallbackOffloadingEnabled)
        {
            cout << "cTCPReceiver::getNextPacket(): Warning. Popping data while callback offloading is enabled. Data may be insistency distributed amongst destinations." << endl;
        }
        m_oBuffer.elementRead(); //Signal to pop element off FIFO
    }

    return true;
}

void cTCPReceiver::dataOffloadingThreadFunction()
{
    cout << "Entered cTCPReceiver::dataOffloadingThreadFuncton()." << endl;

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
                cout << "cTCPReceiver::dataOffloadingThreadFunction(): Got stop flag. Aborting..." << endl;
                return;
            }
        }

        {
            boost::upgrade_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

            for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size(); ui++)
            {
                m_vpCallbackHandlers[ui]->offloadData_callback(m_oBuffer.getElementDataPointer(i32Index), m_oBuffer.getElementPointer(i32Index)->allocationSize());
            }
        }

        m_oBuffer.elementRead(); //Signal to pop element off FIFO
    }

    cout << "Exiting cTCPReceiver::dataOffloadingThreadFunction()." << endl;
}

void cTCPReceiver::registerCallbackHandler(boost::shared_ptr<cTCPReceiverCallbackInterface> pNewHandler)
{
    boost::upgrade_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

    m_vpCallbackHandlers.push_back(pNewHandler);

    cout << "cUDPReceiver::registerCallbackHandler(): Successfully registered callback handler: " << pNewHandler.get() << endl;
}

void cTCPReceiver::deregisterCallbackHandler(boost::shared_ptr<cTCPReceiverCallbackInterface> pHandler)
{
    boost::upgrade_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);
    bool bSuccess = false;

    //Search for matching pointer values and erase
    for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size();)
    {
        if(m_vpCallbackHandlers[ui].get() == pHandler.get())
        {
            m_vpCallbackHandlers.erase(m_vpCallbackHandlers.begin() + ui);

            cout << "cTCPReceiver::deregisterCallbackHandler(): Deregistered callback handler: " << pHandler.get() << endl;
            bSuccess = true;
        }
        else
        {
            ui++;
        }
    }

    if(!bSuccess)
    {
        cout << "cTCPReceiver::deregisterCallbackHandler(): Warning: Deregistering callback handler: " << pHandler.get() << " failed. Object instance not found." << endl;
    }
}
