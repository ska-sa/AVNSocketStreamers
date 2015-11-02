//System includes
#include <iostream>
#include <sstream>

//Library includes
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/asio/buffer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#endif

//Local includes
#include "UDPReceiver.h"

using namespace std;

cUDPReceiver::cUDPReceiver(const string &strLocalInterface, uint16_t u16LocalPort, const string &strPeerAddress, uint16_t u16PeerPort) :
    cSocketReceiverBase(strPeerAddress, u16PeerPort),
    m_oSocket(string("UDP socket")),
    m_strLocalInterface(strLocalInterface),
    m_u16LocalPort(u16LocalPort)
{
    m_oBuffer.resize(128, 1040 * 16); //16 packets of 1040 bytes for each complex uint32_t FFT window of 2 channels or or I,Q,U,V uint32_t stokes parameters
}

cUDPReceiver::~cUDPReceiver()
{
    m_oSocket.cancelCurrrentOperations();
}

void cUDPReceiver::socketReceivingThreadFunction()
{
    cout << "Entered cUDPReceiver::socketReceivingThreadFunction()" << endl;

    //First attempt to bind socket

    //m_oUDPSocket.openBindAndConnect(m_strLocalInterface, m_u16LocalPort, m_strPeerAddress, m_u16PeerPort);
    while(!m_oSocket.openAndBind(m_strLocalInterface, m_u16LocalPort))
    {
        if(isShutdownRequested() || !isReceivingEnabled())
        {
            cout << "cUDPReceiver::socketReceivingThreadFunction(): Got shutdown flag, returning." << endl;
            return;
        }

        //Wait some time then try to bind again...
        boost::this_thread::sleep(boost::posix_time::milliseconds(2000));
        cout << "cUDPReceiver::socketReceivingThreadFunction(): Retrying socket binding to " << m_strLocalInterface << ":" << m_u16LocalPort << endl;
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
                cout << "cUDPReceiver::socketReceivingThread(): Exiting receiving thread." << endl;
                cout << "---- Received " << u32PacketsReceived << " packets. ----" << endl;
                return;
            }
        }

        //Check that our buffer is large enough
        uint32_t u32UDPBytesAvailable = m_oSocket.getBytesAvailable();
        if(u32UDPBytesAvailable > m_oBuffer.getElementPointer(i32Index)->allocationSize())
        {
            cout << "cUDPReceiver::socketReceivingThread(): Warning: Input buffer element size is too small for UDP packet." << endl;
            cout << "Resizing to " << u32UDPBytesAvailable << " bytes" << endl;

            m_oBuffer.resize(m_oBuffer.getNElements(), u32UDPBytesAvailable);
        }

        //Read as many packets as can be fitted in to the buffer (it should be empty at this point)
        i32BytesLeftToRead = m_oBuffer.getElementPointer(i32Index)->allocationSize();

        while(i32BytesLeftToRead)
        {
            string strSender;
            uint16_t u16Port;
            //if(!m_oUDPSocket.receive(m_oBuffer.getElementDataPointer(i32Index) + m_oBuffer.getElementPointer(i32Index)->dataSize(), i32BytesLeftToRead) )
            if(!m_oSocket.receiveFrom(m_oBuffer.getElementDataPointer(i32Index) + m_oBuffer.getElementPointer(i32Index)->dataSize(), i32BytesLeftToRead, strSender, u16Port) )
            {
                cout << "cUDPReceiver::socketReceivingThread(): Warning socket error: " << m_oSocket.getLastError().message() << endl;
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
                cout << "cUDPReceiver::socketReceivingThread(): Exiting receiving thread." << endl;
                cout << "---- Received " << u32PacketsReceived << " packets. ----" << endl;
                return;
            }
        }
        //Signal we have completely filled an element of the input buffer.
        m_oBuffer.elementWritten();

    }

    cout << "cUDPReceiver::socketReceivingThread(): Exiting receiving thread." << endl;
    cout << "---- Received " << u32PacketsReceived << " packets ----" << endl;
    fflush(stdout);
}

void cUDPReceiver::stopReceiving()
{
    cSocketReceiverBase::stopReceiving();

    //Also interrupt the socket which exists only in this derived implmentation
    m_oSocket.cancelCurrrentOperations();
}
