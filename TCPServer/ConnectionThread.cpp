
//System includes
#include <iostream>

//Library include:

//Local includes
#include "ConnectionThread.h"

using namespace std;

cConnectionThread::cConnectionThread(boost::shared_ptr<cInterruptibleBlockingTCPSocket> pClientSocket) :
    m_bIsValid(true),
    m_oBuffer(64, 1040 * 16) //16 packets of 1040 bytes for each complex uint32_t FFT window of 2 channels or or I,Q,U,V uint32_t stokes parameters.
{
    m_pClientSocket.swap(pClientSocket);

    cout << "Got new connection from host: " << m_pClientSocket->getRemoteAddress() << endl;

    m_pSocketWritingThread.reset(new boost::thread(&cConnectionThread::socketWritingThreadFunction, this));
}

cConnectionThread::~cConnectionThread()
{
    setInvalid();
    shutdown();

    m_pClientSocket->close();

    if(m_pSocketWritingThread.get())
    {
        m_pSocketWritingThread->join();
    }
}

void cConnectionThread::shutdown()
{
    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bShutdownFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);

    m_bShutdownFlag = true;
}

bool cConnectionThread::isValid()
{
    boost::shared_lock<boost::shared_mutex>  oLock(m_bValidMutex);

    return m_bIsValid;
}

void cConnectionThread::setInvalid()
{
    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bValidMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);

    m_bIsValid = false;
}

bool cConnectionThread::isShutdownRequested()
{
    boost::shared_lock<boost::shared_mutex> oLock(m_bShutdownFlagMutex);

    return m_bShutdownFlag;
}

bool cConnectionThread::tryAddDataToSend(char* cpData, uint32_t u32Size_B)
{
    //Try to get the next free pointer. (For max 10 ms)
    int32_t i32Index = m_oBuffer.getNextWriteIndex(10);

    //If the is not space in the buffer return false.
    if(i32Index == -1)
        return false;

    //Otherwise check that our buffer is large enough
    if(u32Size_B > m_oBuffer.getElementPointer(i32Index)->allocationSize())
    {
        cout << "Warning: Input buffer element size is too small for UDP packet." << endl;
        cout << "Resizing to " << u32Size_B << " bytes" << endl;

        m_oBuffer.resize(m_oBuffer.getNElements(), u32Size_B);
    }

    //Read as much data as can be fitted in to the buffer (it should be empty at this point)
    int32_t i32BytesLeftToRead = m_oBuffer.getElementPointer(i32Index)->allocationSize();

    memcpy(m_oBuffer.getElementDataPointer(i32Index), cpData, u32Size_B);
    m_oBuffer.getElementPointer(i32Index)->setDataAdded(u32Size_B);

    //Signal we have completely filled an element of the input buffer.
    m_oBuffer.elementWritten();
}

void cConnectionThread::blockingAddDataToSend(char* cpData, uint32_t u32Size_B)
{
    //Get (or wait for) the next available element to write data to
    //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
    //This prevents the program locking up in this thread.
    int32_t i32Index = -1;
    while(i32Index == -1)
    {
        i32Index = m_oBuffer.getNextWriteIndex(500);

        //Also check for shutdown flag
        if(isShutdownRequested())
        {
            cout << "Exiting blockingAddDataToSend() on detection of shutdown flag." << endl;
            return;
        }
    }

    //Check that our buffer is large enough
    if(u32Size_B > m_oBuffer.getElementPointer(i32Index)->allocationSize())
    {
        cout << "Warning: Input buffer element size is too small for UDP packet." << endl;
        cout << "Resizing to " << u32Size_B << " bytes" << endl;

        m_oBuffer.resize(m_oBuffer.getNElements(), u32Size_B);
    }

    //Read as much data as can be fitted in to the buffer (it should be empty at this point)
    int32_t i32BytesLeftToRead = m_oBuffer.getElementPointer(i32Index)->allocationSize();

    memcpy(m_oBuffer.getElementDataPointer(i32Index), cpData, u32Size_B);
    m_oBuffer.getElementPointer(i32Index)->setDataAdded(u32Size_B);

    //Signal we have completely filled an element of the input buffer.
    m_oBuffer.elementWritten();
}

void cConnectionThread::socketWritingThreadFunction()
{
    uint32_t u32BytesToTransfer = 0;
    uint32_t u32BytesTransferred = 0;
    int32_t i32Index = 0;
    bool bSuccess = false;

    while(!isShutdownRequested())
    {
        //Get a new buffer element's worth of data and send it.

        //Get (or wait for) the next available element to read data from
        //If waiting timeout every 500 ms and check for shutdown or stop streaming flags
        //This prevents the program locking up in this thread.
        i32Index = -1;
        while(i32Index == -1)
        {
            i32Index = m_oBuffer.getNextReadIndex(500);

            //Also check for shutdown flag
            if(isShutdownRequested())
            {
                cout << "Aborting reading next packet from UDPReceiver." << endl;
                return;
            }
        }
        u32BytesToTransfer = m_oBuffer.getElementPointer(i32Index)->allocationSize();
        u32BytesTransferred = 0;
        while(u32BytesToTransfer)
        {
            bSuccess = m_pClientSocket->send(m_oBuffer.getElementDataPointer(i32Index) + u32BytesTransferred, u32BytesToTransfer);

            if(!bSuccess)
                break;

            u32BytesToTransfer -= m_pClientSocket->getNBytesLastTransferred();
            u32BytesTransferred += m_pClientSocket->getNBytesLastTransferred();
        }

        if(!bSuccess)
            continue; //If the sending failed trying again from the beginning.


        m_oBuffer.elementRead(); //Otherwise write is complete. Signal to pop element off FIFO
    }
}
