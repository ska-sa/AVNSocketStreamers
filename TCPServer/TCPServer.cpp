
//System includes
#include <sstream>

//Library include:
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#endif

//Local includes
#include "TCPServer.h"

cTCPServer::cTCPServer(const std::string &strInterface, uint16_t u16Port, uint32_t u32MaxConnections) :
    m_bShutdownFlag(false),
    m_u32MaxConnections(u32MaxConnections),
    m_strInterface(strInterface),
    m_u16Port(u16Port)
{
    m_pSocketListeningThread.reset(new boost::thread(&cTCPServer::socketListeningThreadFunction, this));
}

cTCPServer::~cTCPServer()
{
    shutdown();

    {
        boost::unique_lock<boost::shared_mutex>  oLock(m_oConnectThreadsMutex);
        m_vpConnectionThreads.clear();
    }
}

void cTCPServer::shutdown()
{
    {
        boost::unique_lock<boost::shared_mutex>  oLock(m_bShutdownFlagMutex);
        m_bShutdownFlag = true;
    }

    if(m_oTCPAcceptor.isOpen())
        m_oTCPAcceptor.close();

    if(m_pSocketListeningThread.get())
    {
        m_pSocketListeningThread->join();
    }
}

bool cTCPServer::isShutdownRequested()
{
    boost::shared_lock<boost::shared_mutex> oLock(m_bShutdownFlagMutex);

    return m_bShutdownFlag;
}

void cTCPServer::socketListeningThreadFunction()
{
    uint32_t u32ConnectionNumber = 0;

    while(!isShutdownRequested())
    {
        //If the listening socket exists already close it
        if(m_oTCPAcceptor.isOpen())
            m_oTCPAcceptor.close();

        //Listen for incoming connects from clients
        try
        {
            m_oTCPAcceptor.openAndListen(m_strInterface, m_u16Port);

            break;
        }

        catch(boost::system::system_error const &oSystemError)
        {
            cout << "cTCPServer::socketListeningThreadFunction(): Failed to bind to port and listen." << endl;
            cout << "cTCPServer::socketListeningThreadFunction(): The error was: " << oSystemError.what() << endl;
            cout << "cTCPServer::socketListeningThreadFunction(): Retrying in 5 s ..." << endl;
            boost::this_thread::sleep(boost::posix_time::milliseconds(5000));
        }
    }

    cout << "cTCPServer::socketListeningThreadFunction(): Listening for TCP connections on " << m_strInterface << ":" << m_u16Port << endl;

    while(!isShutdownRequested())
    {
        cout << "cTCPServer::socketListeningThreadFunction(): Listening for client connections..." << endl;

        std::stringstream oSS;
        oSS << "Connection ";
        oSS << u32ConnectionNumber++;

        boost::shared_ptr<cInterruptibleBlockingTCPSocket> pClientSocket = boost::make_shared<cInterruptibleBlockingTCPSocket>(oSS.str()); //A socket object to store the incoming connection
        try
        {
            string strPeerAddress;
            m_oTCPAcceptor.accept(pClientSocket, strPeerAddress); //Accept connection from a client.

            boost::unique_lock<boost::shared_mutex> oLock(m_oConnectThreadsMutex);
            m_vpConnectionThreads.push_back(boost::make_shared<cConnectionThread>(pClientSocket));

            cout << "cTCPServer::socketListeningThreadFunction(): There are now " << m_vpConnectionThreads.size() << " client(s) connected." << endl;
        }
        catch(boost::system::system_error const &oSystemError)
        {
            cout << "cTCPServer::socketListeningThreadFunction(): Caught Exception on accepting incoming connection." << endl;
            cout << "cTCPServer::socketListeningThreadFunction():The error was: " << oSystemError.what() << endl;
            continue; //Try again
        }

    }

    m_oTCPAcceptor.close();

    cout << "cTCPServer::socketListeningThreadFunction(): Returning from thread function." << endl;
}

void cTCPServer::writeData(char* cpData, uint32_t u32Size_B)
{
    boost::upgrade_lock<boost::shared_mutex> oLock(m_oConnectThreadsMutex);

    for(uint32_t ui = 0; ui < m_vpConnectionThreads.size(); ui++)
    {
        //Send only to valid connections
        if(m_vpConnectionThreads[ui]->isValid())
            m_vpConnectionThreads[ui]->tryAddDataToSend(cpData, u32Size_B);
    }

    //Clean up any invalid connections
    for(uint32_t ui = 0; ui < m_vpConnectionThreads.size();)
    {
        if(!m_vpConnectionThreads[ui]->isValid())
        {
            cout << "cTCPServer::writeData(): Closing connection to client " << m_vpConnectionThreads[ui]->getPeerAddress();
            if(m_vpConnectionThreads[ui]->getSocketName().length())
                cout << " (" << m_vpConnectionThreads[ui]->getSocketName() << ")";
            
            cout << endl;

            {
                boost::upgrade_to_unique_lock< boost::shared_mutex > uniqueLock(oLock);
                m_vpConnectionThreads.erase(m_vpConnectionThreads.begin() + ui);
            }

            cout << "cTCPServer::writeData(): There are now " << m_vpConnectionThreads.size() << " client(s) connected." << endl;
        }
        else
        {
            ui++;
        }
    }
}


