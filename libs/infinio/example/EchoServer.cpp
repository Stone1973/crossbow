#include <crossbow/infinio/Endpoint.hpp>
#include <crossbow/infinio/EventDispatcher.hpp>
#include <crossbow/infinio/InfinibandService.hpp>
#include <crossbow/infinio/InfinibandSocket.hpp>
#include <crossbow/program_options.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <unordered_set>

using namespace crossbow::infinio;

class EchoConnection: private InfinibandSocketHandler {
public:
    EchoConnection(InfinibandSocket socket)
            : mSocket(std::move(socket)) {
    }

    void init();

private:
    virtual void onConnected(const boost::system::error_code& ec) override;

    virtual void onReceive(const void* buffer, size_t length, const boost::system::error_code& ec) override;

    virtual void onDisconnect() override;

    virtual void onDisconnected() override;

    void handleError(std::string message, boost::system::error_code& ec);

    InfinibandSocket mSocket;
};

void EchoConnection::init() {
    mSocket.setHandler(this);
}

void EchoConnection::onConnected(const boost::system::error_code& ec) {
    std::cout << "Connected" << std::endl;
}

void EchoConnection::onReceive(const void* buffer, size_t length, const boost::system::error_code& /* ec */) {
    boost::system::error_code ec;

    // Acquire buffer with same size
    auto sendbuffer = mSocket.acquireSendBuffer(length);
    if (sendbuffer.id() == InfinibandBuffer::INVALID_ID) {
        handleError("Error acquiring buffer", ec);
        return;
    }

    // Copy received message into send buffer
    memcpy(sendbuffer.data(), buffer, length);

    // Send incoming message back to client
    mSocket.send(sendbuffer, 0x0u, ec);
    if (ec) {
        handleError("Send failed", ec);
    }
}

void EchoConnection::onDisconnect() {
    std::cout << "Disconnect" << std::endl;
    boost::system::error_code ec;
    mSocket.disconnect(ec);
    if (ec) {
        std::cout << "Disconnect failed " << ec.value() << " - " << ec.message() << std::endl;
    }
}

void EchoConnection::onDisconnected() {
    std::cout << "Disconnected" << std::endl;
}

void EchoConnection::handleError(std::string message, boost::system::error_code& ec) {
    std::cout << message << " [" << ec << " - " << ec.message() << "]" << std::endl;
    std::cout << "Disconnecting after error" << std::endl;
    mSocket.disconnect(ec);
    if (ec) {
        std::cout << "Disconnect failed [" << ec << " - " << ec.message() << "]" << std::endl;
    }
}

class EchoAcceptor: protected InfinibandSocketHandler {
public:
    EchoAcceptor(InfinibandService& service)
            : mAcceptor(service) {
    }

    void open(uint16_t port);

protected:
    virtual bool onConnection(InfinibandSocket socket) override;

private:
    InfinibandAcceptor mAcceptor;

    std::unordered_set<std::unique_ptr<EchoConnection>> mConnections;
};

void EchoAcceptor::open(uint16_t port) {
    boost::system::error_code ec;

    // Open socket
    mAcceptor.open(ec);
    if (ec) {
        std::cout << "Open failed " << ec << " - " << ec.message() << std::endl;
        std::terminate();
    }
    mAcceptor.setHandler(this);

    // Bind socket
    mAcceptor.bind(Endpoint(Endpoint::ipv4(), port), ec);
    if (ec) {
        std::cout << "Bind failed " << ec << " - " << ec.message() << std::endl;
        std::terminate();
    }

    // Start listening
    mAcceptor.listen(10, ec);
    if (ec) {
        std::cout << "Listen failed " << ec << " - " << ec.message() << std::endl;
        std::terminate();
    }

    std::cout << "Echo server started up" << std::endl;
}

bool EchoAcceptor::onConnection(InfinibandSocket socket) {
    std::cout << "New incoming connection" << std::endl;

    std::unique_ptr<EchoConnection> con(new EchoConnection(std::move(socket)));
    con->init();

    mConnections.emplace(std::move(con));

    return true;
}

int main(int argc, const char** argv) {
    bool help = false;
    uint16_t port = 4488;
    auto opts = crossbow::program_options::create_options(argv[0],
            crossbow::program_options::toggle<'h'>("help", help),
            crossbow::program_options::value<'p'>("port", port));

    try {
        crossbow::program_options::parse(opts, argc, argv);
    } catch (crossbow::program_options::argument_not_found e) {
        std::cerr << e.what() << std::endl << std::endl;
        crossbow::program_options::print_help(std::cout, opts);
        return 1;
    }

    if (help) {
        crossbow::program_options::print_help(std::cout, opts);
        return 0;
    }

    std::cout << "Starting echo server" << std::endl;
    EventDispatcher dispatcher;
    InfinibandService service(dispatcher);
    EchoAcceptor echo(service);
    echo.open(port);

    dispatcher.run();

    return 0;
}
