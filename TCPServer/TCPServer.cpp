
//System includes

//Library include:
#ifndef Q_MOC_RUN //Qt's MOC and Boost have some issues don't let MOC process boost headers
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#endif

//Local includes
#include "TCPServer.h"

cTCPServer::cTCPServer(const std::string &strInterface, uint16_t u16Port, uint32_t u32MaxConnections) :
    m_bShutdownFlag(false),
    m_oTCPAcceptor(m_oIOService),
    m_strInterface(strInterface),
    m_u16Port(u16Port),
    m_u32MaxConnections(u32MaxConnections)
{
    m_pSocketListeningThread.reset(new boost::thread(&cTCPServer::socketListeningThreadFunction, this));
}

cTCPServer::~cTCPServer()
{
    shutdown();

    if(m_oTCPAcceptor.is_open())
        m_oTCPAcceptor.close();

    if(m_pSocketListeningThread.get())
    {
        m_pSocketListeningThread->join();
    }

    m_vpConnectionThreads.clear();
}

void cTCPServer::shutdown()
{
    boost::upgrade_lock<boost::shared_mutex>  oLock(m_bShutdownFlagMutex);
    boost::upgrade_to_unique_lock<boost::shared_mutex>  oUniqueLock(oLock);

    m_bShutdownFlag = true;
}

bool cTCPServer::isShutdownRequested()
{
    boost::shared_lock<boost::shared_mutex> oLock(m_bShutdownFlagMutex);

    return m_bShutdownFlag;
}

void cTCPServer::socketListeningThreadFunction()
{
    while(!isShutdownRequested())
    {
        //If the listening socket exists already close it
        if(m_oTCPAcceptor.is_open())
            m_oTCPAcceptor.close();

        //Listen for incoming connects from clients
        try
        {
            m_oTCPAcceptor.open(boost::asio::ip::tcp::v4()); //Use only IPv4
            m_oTCPAcceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true)); //Set Listening socket to reuse address.
            m_oTCPAcceptor.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(m_strInterface), m_u16Port));
            m_oTCPAcceptor.listen();

            break;
        }

        catch(boost::system::system_error const &oSystemError)
        {
            cout << "Failed to bind to port and listen." << endl;
            cout << "The error was: " << oSystemError.what() << endl;
            cout << "Retrying in 5 s ..." << endl;
            boost::this_thread::sleep(boost::posix_time::milliseconds(5000));
        }
    }

    while(!isShutdownRequested())
    {
        cout << "Listening for client connections..." << endl;

        boost::shared_ptr<cInterruptibleBlockingTCPSocket> pClientSocket = boost::make_shared<cInterruptibleBlockingTCPSocket>(); //A socket object to store the incoming connection
        boost::asio::ip::tcp::acceptor::endpoint_type oEndPoint; //A endpoint object to store details about the client
        try
        {
            m_oTCPAcceptor.accept(*pClientSocket->getBoostSocketPointer(), oEndPoint); //Accept connection from a client.

            m_vpConnectionThreads.push_back(boost::make_shared<cConnectionThread>(pClientSocket));
            cout << "There are now " << m_vpConnectionThreads.size() << " clients connected." << endl;
        }
        catch(boost::system::system_error const &oSystemError)
        {
            cout << "Accept incoming connection." << endl;
            cout << "The error was: " << oSystemError.what() << endl;
            continue; //Try again
        }

    }
    cout << "Returning from thread function." << endl;
    m_oTCPAcceptor.close();
}

bool cTCPServer::writeData(char* cpData, uint32_t u32Size_B)
{
    boost::shared_lock<boost::shared_mutex> oLock(m_oConnectThreadsMutex);

    for(uint32_t ui = 0; ui < m_vpConnectionThreads.size(); ui++)
    {
        m_vpConnectionThreads[ui]->tryAddDataToSend(cpData, u32Size_B);
    }
}


