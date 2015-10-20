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
