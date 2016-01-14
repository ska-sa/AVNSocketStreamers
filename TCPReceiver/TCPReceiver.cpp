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
    cSocketReceiverBase(strPeerAddress, u16PeerPort),
    m_oSocket(string("TCP socket"))
{
}

cTCPReceiver::~cTCPReceiver()
{
    stopReceiving();
}

void cTCPReceiver::socketReceivingThreadFunction()
{
    cout << "Entered cTCPReceiver::socketReceivingThreadFunction()" << endl;

    //First attempt to connect socket
    if(m_oSocket.openAndConnect(m_strPeerAddress, m_u16PeerPort))
    {
        //Notification of socket connection
        boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

        for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers.size(); ui++)
        {
            m_vpNotificationCallbackHandlers[ui]->socketConnected_callback();
        }

        for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers_shared.size(); ui++)
        {
            m_vpNotificationCallbackHandlers_shared[ui]->socketConnected_callback();
        }
    }
    else
    {
        //Notification of socket connection failure
        boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

        for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers.size(); ui++)
        {
            m_vpNotificationCallbackHandlers[ui]->socketDisconnected_callback();
        }

        for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers_shared.size(); ui++)
        {
            m_vpNotificationCallbackHandlers_shared[ui]->socketDisconnected_callback();
        }
    }

    //Enter thread loop, repeated reading into the FIFO
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
                cout << "cTCPReceiver::socketReceivingThread(): Warning socket error: " << m_oSocket.getLastReadError().message() << endl;
                cout << "cTCPReceiver::socketReceivingThread(): Warning socket error value: " << m_oSocket.getLastReadError().value() << endl;

                //Check for errors from socket disconnection
                //TODO: This should probably be done with error codes as apposed string matching

                if(m_oSocket.getLastReadError().message().find("End of file") != string::npos
                        || m_oSocket.getLastReadError().message().find("Bad file descriptor") != string::npos )
                {
                    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

                    for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers.size(); ui++)
                    {
                        m_vpNotificationCallbackHandlers[ui]->socketDisconnected_callback();
                    }

                    for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers_shared.size(); ui++)
                    {
                        m_vpNotificationCallbackHandlers_shared[ui]->socketDisconnected_callback();
                    }

                    cout << "cTCPReceiver::socketReceivingThread(): socket disconnected." << endl;
                    stopReceiving();
                    m_oSocket.close();
                    break;
                }
            }

            i32BytesLastRead = m_oSocket.getNBytesLastRead();

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

void  cTCPReceiver::stopReceiving()
{
    cSocketReceiverBase::stopReceiving();

    //Also interrupt the socket which exists only in this derived implmentation
    m_oSocket.cancelCurrrentOperations();
}

void cTCPReceiver::registerNoticationCallbackHandler(cNotificationCallbackInterface* pNewHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

    m_vpNotificationCallbackHandlers.push_back(pNewHandler);

    cout << "cTCPReceiver::registerNoticationCallbackHandler(): Successfully registered callback handler: " << pNewHandler << endl;
}

void cTCPReceiver::registerNoticationCallbackHandler(boost::shared_ptr<cNotificationCallbackInterface> pNewHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);

    m_vpNotificationCallbackHandlers_shared.push_back(pNewHandler);

    cout << "cTCPReceiver::registerNoticationCallbackHandler(): Successfully registered callback handler: " << pNewHandler.get() << endl;
}

void cTCPReceiver::deregisterNotificationCallbackHandler(cNotificationCallbackInterface* pHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);
    bool bSuccess = false;

    //Search for matching pointer values and erase
    for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers.size();)
    {
        if(m_vpNotificationCallbackHandlers[ui] == pHandler)
        {
            m_vpNotificationCallbackHandlers.erase(m_vpNotificationCallbackHandlers.begin() + ui);

            cout << "cTCPReceiver::deregisterNotificationCallbackHandler(): Deregistered callback handler: " << pHandler << endl;
            bSuccess = true;
        }
        else
        {
            ui++;
        }
    }

    if(!bSuccess)
    {
        cout << "cTCPReceiver::deregisterNotificationCallbackHandler(): Warning: Deregistering callback handler: " << pHandler << " failed. Object instance not found." << endl;
    }
}

void cTCPReceiver::deregisterNotificationCallbackHandler(boost::shared_ptr<cNotificationCallbackInterface> pHandler)
{
    boost::unique_lock<boost::shared_mutex> oLock(m_oCallbackHandlersMutex);
    bool bSuccess = false;

    //Search for matching pointer values and erase
    for(uint32_t ui = 0; ui < m_vpNotificationCallbackHandlers_shared.size();)
    {
        if(m_vpNotificationCallbackHandlers_shared[ui].get() == pHandler.get())
        {
            m_vpNotificationCallbackHandlers_shared.erase(m_vpNotificationCallbackHandlers_shared.begin() + ui);

            cout << "cTCPReceiver::deregisterNotificationCallbackHandler(): Deregistered callback handler: " << pHandler.get() << endl;
            bSuccess = true;
        }
        else
        {
            ui++;
        }
    }

    if(!bSuccess)
    {
        cout << "cTCPReceiver::deregisterNotificationCallbackHandler(): Warning: Deregistering callback handler: " << pHandler.get() << " failed. Object instance not found." << endl;
    }
}
