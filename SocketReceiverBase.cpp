//System includes
#include <iostream>
#include <sstream>

//Library includes
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/asio/buffer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#endif

//Local includes
#include "SocketReceiverBase.h"

using namespace std;

cSocketReceiverBase::cSocketReceiverBase(const string &strPeerAddress, uint16_t u16PeerPort) :
    m_strPeerAddress(strPeerAddress),
    m_u16PeerPort(u16PeerPort),
    m_bReceivingEnabled(false),
    m_bCallbackOffloadingEnabled(false),
    m_bShutdownFlag(false),
    m_pSocketReceivingThread(NULL),
    m_pDataOffloadingThread(NULL),
    m_i32GetRawDataInputBufferIndex(-1),
    m_oBuffer(1024, 1040)
{
}

cSocketReceiverBase::~cSocketReceiverBase()
{
    shutdown();
}

void cSocketReceiverBase::clearBuffer()
{
    m_oBuffer.clear();
}

void cSocketReceiverBase::startReceiving()
{
    cout << "cSocketReceiverBase::startReceiving()" << endl;

    u64TotalBytesProcessed = 0;

    {
        boost::upgrade_lock<boost::shared_mutex>  oLock(m_oFlagMutex);
        boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);
        m_bReceivingEnabled = true;
    }

    m_i32GetRawDataInputBufferIndex = -1;

    clearBuffer();

    m_pSocketReceivingThread.reset(new boost::thread(&cSocketReceiverBase::socketReceivingThreadFunction, this));
}

void cSocketReceiverBase::stopReceiving()
{
    cout << "cSocketReceiverBase::stopReceiving()" << endl;

    boost::unique_lock<boost::shared_mutex>  oLock(m_oFlagMutex);
    m_bReceivingEnabled = false;
}

void cSocketReceiverBase::startCallbackOffloading()
{
    cout << "cSocketReceiverBase::startCallbackOffloading()" << endl;

    {
        boost::unique_lock<boost::shared_mutex>  oLock(m_oFlagMutex);
        m_bCallbackOffloadingEnabled = true;
    }

    m_i32GetRawDataInputBufferIndex = -1;

    clearBuffer();

    m_pSocketReceivingThread.reset(new boost::thread(&cSocketReceiverBase::dataOffloadingThreadFunction, this));
}

void cSocketReceiverBase::stopCallbackOffloading()
{
    //Thread safe flag mutator

    cout << "cSocketReceiverBase::stopCallbackOffloading()" << endl;

    boost::unique_lock<boost::shared_mutex>  oLock(m_oFlagMutex);
    m_bCallbackOffloadingEnabled = false;
}

bool  cSocketReceiverBase::isReceivingEnabled()
{
    //Thread safe accessor

    boost::shared_lock<boost::shared_mutex>  oLock(m_oFlagMutex);
    return m_bReceivingEnabled;
}

bool  cSocketReceiverBase::isCallbackOffloadingEnabled()
{
    //Thread safe accessor

    boost::shared_lock<boost::shared_mutex>  oLock(m_oFlagMutex);
    return m_bCallbackOffloadingEnabled;
}

void cSocketReceiverBase::shutdown()
{
    //Thread safe flag mutator

    {
        boost::unique_lock<boost::shared_mutex>  oLock(m_oFlagMutex);
        m_bShutdownFlag = true;
    }

    if(m_pSocketReceivingThread.get())
    {
        m_pSocketReceivingThread->join();
    }

    if(m_pDataOffloadingThread.get())
    {
        m_pDataOffloadingThread->join();
    }
}

bool cSocketReceiverBase::isShutdownRequested()
{
    //Thread safe accessor

    boost::shared_lock<boost::shared_mutex> oLock(m_oFlagMutex);
    return m_bShutdownFlag;
}

int32_t cSocketReceiverBase::getNextPacketSize_B(uint32_t u32Timeout_ms)
{
    //Get (or wait for) the next available element to read data from
    //If waiting, timeout every 500 ms and check for shutdown or stop streaming flags
    //This prevents the program locking up in this thread.

    //Current time:
    boost::posix_time::ptime oStartTime = boost::posix_time::microsec_clock::local_time();

    int32_t i32Index = -1;
    while(i32Index == -1)
    {
        boost::posix_time::time_duration oDuration = boost::posix_time::microsec_clock::local_time() - oStartTime;
        if(u32Timeout_ms && oDuration.total_milliseconds() >= u32Timeout_ms)
        {
            cout << "cSocketReceiverBase::getNextPacketSize_B(): Hit caller specified timeout. Returning." << endl;
            return -1;
        }

        i32Index = m_oBuffer.getNextReadIndex(100);

        //Also check for shutdown flag
        if(!isReceivingEnabled() || isShutdownRequested())
        {
            cout << "cSocketReceiverBase::getNextPacketSize_B(): Got stop flag. Aborting..." << endl;
            return -1;
        }
    }

    return m_oBuffer.getElementPointer(i32Index)->allocationSize();
}

bool cSocketReceiverBase::getNextPacket(char *cpData, uint32_t u32Timeout_ms, bool bPopData)
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
            cout << "cSocketReceiverBase::getNextPacket(): Hit caller specified timeout. Returning." << endl;
            return false;
        }

        i32Index = m_oBuffer.getNextReadIndex(100);

        if(i32Index == -1)
            cout << "Got semphore timeout." << endl;

        //Also check for shutdown flag
        if(!isReceivingEnabled() || isShutdownRequested())
        {
            cout << "cSocketReceiverBase::getNextPacket(): Got stop flag. Aborting..." << endl;
            return false;
        }
    }

    memcpy(cpData, m_oBuffer.getElementDataPointer(i32Index), m_oBuffer.getElementPointer(i32Index)->allocationSize());

    if(bPopData)
    {
        if(m_bCallbackOffloadingEnabled)
        {
            cout << "cSocketReceiverBase::getNextPacket(): Warning. Popping data while callback offloading is enabled. Data may be insistency distributed amongst destinations." << endl;
        }
        m_oBuffer.elementRead(); //Signal to pop element off FIFO
    }

    return true;
}

void cSocketReceiverBase::dataOffloadingThreadFunction()
{
    cout << "Entered cSocketReceiverBase::dataOffloadingThreadFuncton()." << endl;

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
                cout << "cSocketReceiverBase::dataOffloadingThreadFunction(): Got stop flag. Aborting..." << endl;
                return;
            }
        }

        {
            boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

            for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size(); ui++)
            {
                m_vpCallbackHandlers[ui]->offloadData_callback(m_oBuffer.getElementDataPointer(i32Index), m_oBuffer.getElementPointer(i32Index)->allocationSize());
            }
        }

        m_oBuffer.elementRead(); //Signal to pop element off FIFO
    }

    cout << "Exiting cSocketReceiverBase::dataOffloadingThreadFunction()." << endl;
}

void cSocketReceiverBase::registerCallbackHandler(boost::shared_ptr<cCallbackInterface> pNewHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

    m_vpCallbackHandlers.push_back(pNewHandler);

    cout << "cUDPReceiver::registerCallbackHandler(): Successfully registered callback handler: " << pNewHandler.get() << endl;
}

void cSocketReceiverBase::deregisterCallbackHandler(boost::shared_ptr<cCallbackInterface> pHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);
    bool bSuccess = false;

    //Search for matching pointer values and erase
    for(uint32_t ui = 0; ui < m_vpCallbackHandlers.size();)
    {
        if(m_vpCallbackHandlers[ui].get() == pHandler.get())
        {
            m_vpCallbackHandlers.erase(m_vpCallbackHandlers.begin() + ui);

            cout << "cSocketReceiverBase::deregisterCallbackHandler(): Deregistered callback handler: " << pHandler.get() << endl;
            bSuccess = true;
        }
        else
        {
            ui++;
        }
    }

    if(!bSuccess)
    {
        cout << "cSocketReceiverBase::deregisterCallbackHandler(): Warning: Deregistering callback handler: " << pHandler.get() << " failed. Object instance not found." << endl;
    }
}
