#pragma once

#include <crossbow/infinio/Endpoint.hpp>
#include <crossbow/infinio/ErrorCode.hpp>
#include <crossbow/infinio/InfinibandBuffer.hpp>
#include <crossbow/string.hpp>

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <system_error>

#include <rdma/rdma_cma.h>

namespace crossbow {
namespace infinio {

class CompletionContext;
class InfinibandService;

class InfinibandSocketImpl;
using InfinibandSocket = boost::intrusive_ptr<InfinibandSocketImpl>;

/**
 * @brief Base class containing common functionality shared between InfinibandAcceptor and InfinibandSocket
 */
template <typename SocketType>
class InfinibandBaseSocket : public boost::intrusive_ref_counter<SocketType> {
public:
    void open(std::error_code& ec);

    bool isOpen() const {
        return (mId != nullptr);
    }

    void close(std::error_code& ec);

    void bind(Endpoint& addr, std::error_code& ec);

protected:
    InfinibandBaseSocket(struct rdma_event_channel* channel)
            : mChannel(channel),
              mId(nullptr) {
    }

    InfinibandBaseSocket(struct rdma_cm_id* id)
            : mChannel(id->channel),
              mId(id) {
        // TODO Assert that mId->context was null

        // The ID is already open, increment the reference count by one and then detach the pointer
        boost::intrusive_ptr<SocketType> ptr(static_cast<SocketType*>(this));
        mId->context = ptr.detach();
    }

    /// The RDMA channel the connection is associated with
    struct rdma_event_channel* mChannel;

    /// The RDMA ID of this connection
    struct rdma_cm_id* mId;
};

class ConnectionRequest {
public:
    ConnectionRequest(ConnectionRequest&&) = default;
    ConnectionRequest& operator=(ConnectionRequest&&) = default;

    ~ConnectionRequest();

    /**
     * @brief The private data send with the connection request
     *
     * This length of the string may be larger than the actual data sent (as dictated by the underlying transport). Any
     * additional bytes are zeroed out.
     */
    const crossbow::string& data() const {
        return mData;
    }

    InfinibandSocket accept(std::error_code& ec, uint64_t thread = 0) {
        return accept(crossbow::string(), ec, thread);
    }

    InfinibandSocket accept(const crossbow::string& data, std::error_code& ec, uint64_t thread = 0);

    void reject() {
        reject(crossbow::string());
    }

    void reject(const crossbow::string& data);

private:
    friend class InfinibandService;

    ConnectionRequest(InfinibandService& service, InfinibandSocket socket, crossbow::string data)
            : mService(service),
              mSocket(std::move(socket)),
              mData(std::move(data)) {
    }

    InfinibandService& mService;

    /// The socket with the pending connection request
    InfinibandSocket mSocket;

    /// The private data send with the connection request
    crossbow::string mData;
};

/**
 * @brief Interface class containing callbacks for various socket events
 */
class InfinibandAcceptorHandler {
public:
    virtual ~InfinibandAcceptorHandler();

    /**
     * @brief Handle a new incoming connection
     *
     * The connection is not yet in a fully connected state so any write operations on the socket will fail until the
     * onConnected callback is invoked from the socket.
     *
     * In case the connection is accepted the class is responsible for taking ownership of the InfinibandSocket object.
     *
     * @param request Connection request from the remote host
     */
    virtual void onConnection(ConnectionRequest request);
};

/**
 * @brief Socket acceptor to listen for new incoming connections
 */
class InfinibandAcceptorImpl: public InfinibandBaseSocket<InfinibandAcceptorImpl> {
public:
    void listen(int backlog, std::error_code& ec);

    void setHandler(InfinibandAcceptorHandler* handler) {
        mHandler = handler;
    }

private:
    friend class InfinibandService;

    InfinibandAcceptorImpl(struct rdma_event_channel* channel)
            : InfinibandBaseSocket(channel),
              mHandler(nullptr) {
    }

    /**
     * @brief The listen socket received a new connection request
     *
     * Invoke the onConnection handler to pass the connection request to the owner.
     */
    void onConnectionRequest(ConnectionRequest request) {
        mHandler->onConnection(std::move(request));
    }

    /// Callback handlers for events occuring on this socket
    InfinibandAcceptorHandler* mHandler;
};

extern template class InfinibandBaseSocket<InfinibandAcceptorImpl>;

using InfinibandAcceptor = boost::intrusive_ptr<InfinibandAcceptorImpl>;

/**
 * @brief Interface class containing callbacks for various socket events
 */
class InfinibandSocketHandler {
public:
    virtual ~InfinibandSocketHandler();

    /**
     * @brief Invoked when the connection to the remote host was established
     *
     * The socket is now able to send and receive messages to and from the remote host.
     *
     * Beware of race conditions: The remote end might start sending data before the onConnected function is executed,
     * in this case onReceive might be called before onConnected.
     *
     * @param data Private data sent by the remote host
     * @param ec Error in case the connection attempt failed
     */
    virtual void onConnected(const crossbow::string& data, const std::error_code& ec);

    /**
     * @brief Invoked whenever data was received from the remote host
     *
     * The function must not release the buffer containing the data.
     *
     * @param buffer Pointer to the transmitted data
     * @param length Number of bytes transmitted
     * @param ec Error in case the receive failed
     */
    virtual void onReceive(const void* buffer, size_t length, const std::error_code& ec);

    /**
     * @brief Invoked whenever data was sent to the remote host
     *
     * The function must not release the buffer containing the data.
     *
     * @param userId The user supplied ID of the send call
     * @param ec Error in case the send failed
     */
    virtual void onSend(uint32_t userId, const std::error_code& ec);

    /**
     * @brief Invoked whenever data was read from the remote host
     *
     * @param userId The user supplied ID of the read call
     * @param ec Error in case the read failed
     */
    virtual void onRead(uint32_t userId, const std::error_code& ec);

    /**
     * @brief Invoked whenever data was written to the remote host
     *
     * @param userId The user supplied ID of the write call
     * @param ec Error in case the write failed
     */
    virtual void onWrite(uint32_t userId, const std::error_code& ec);

    /**
     * @brief Invoked whenever the remote host disconnected
     *
     * In order to shutdown the connection the handler should also disconnect from the remote host.
     *
     * Receives may be triggered even after this callback was invoked from remaining packets that were in flight.
     */
    virtual void onDisconnect();

    /**
     * @brief Invoked whenever the connection is disconnected
     *
     * Any remaining in flight packets were processed, it is now safe to clean up the connection.
     */
    virtual void onDisconnected();
};

/**
 * @brief Socket class to communicate with a remote host
 */
class InfinibandSocketImpl: public InfinibandBaseSocket<InfinibandSocketImpl> {
public:
    void setHandler(InfinibandSocketHandler* handler) {
        mHandler = handler;
    }

    void execute(std::function<void()> fun, std::error_code& ec);

    void connect(Endpoint& addr, std::error_code& ec);

    void connect(Endpoint& addr, const crossbow::string& data, std::error_code& ec);

    void disconnect(std::error_code& ec);

    void send(InfinibandBuffer& buffer, uint32_t userId, std::error_code& ec);

    /**
     * @brief Start a RDMA read from the remote memory region with offset into the local target buffer
     *
     * @param src Remote memory region to read from
     * @param offset Offset into the remote memory region
     * @param dst Local buffer to write the data into
     * @param userId User supplied ID passed to the completion handler
     * @param ec Error in case the read failed
     */
    void read(const RemoteMemoryRegion& src, size_t offset, InfinibandBuffer& dst, uint32_t userId,
            std::error_code& ec);

    /**
     * @brief Start a RDMA write from the local source buffer into the remote memory region with offset
     *
     * @param src Local buffer to read the data from
     * @param dst Remote memory region to write the data into
     * @param offset Offset into the remote memory region
     * @param userId User supplied ID passed to the completion handler
     * @param ec Error in case the write failed
     */
    void write(InfinibandBuffer& src, const RemoteMemoryRegion& dst, size_t offset, uint32_t userId,
            std::error_code& ec);

    uint32_t bufferLength() const;

    InfinibandBuffer acquireSendBuffer();

    InfinibandBuffer acquireSendBuffer(uint32_t length);

    void releaseSendBuffer(InfinibandBuffer& buffer);

private:
    friend class CompletionContext;
    friend class ConnectionRequest;
    friend class InfinibandService;

    InfinibandSocketImpl(struct rdma_event_channel* channel, CompletionContext* context)
            : InfinibandBaseSocket(channel),
              mContext(context),
              mHandler(nullptr) {
    }

    InfinibandSocketImpl(struct rdma_cm_id* id)
            : InfinibandBaseSocket(id),
              mContext(nullptr),
              mHandler(nullptr) {
    }

    /**
     * @brief Accepts the pending connection request
     */
    void accept(CompletionContext* context, const crossbow::string& data, std::error_code& ec);

    /**
     * @brief Rejects the pending connection request
     */
    void reject(const crossbow::string& data, std::error_code& ec);

    void doSend(struct ibv_send_wr* wr, std::error_code& ec);

    /**
     * @brief The address to the remote host was successfully resolved
     *
     * Continue by resolving the route to the remote host.
     */
    void onAddressResolved();

    /**
     * @brief The route to the remote host was successfully resolved
     *
     * Setup the socket and establish the connection to the remote host.
     */
    void onRouteResolved();

    /**
     * @brief An error has occured while resolving the address or route of a connection
     *
     * Invoke the onConnected handler to inform the owner that the connection attempt failed.
     */
    void onResolutionError(error::network_errors err);

    /**
     * @brief An error has occured while establishing a connection
     *
     * Invoke the onConnected handler to inform the owner that the connection attempt failed.
     */
    void onConnectionError(error::network_errors err);

    /**
     * @brief The connection has been rejected
     *
     * Invoke the onConnected handler to inform the owner that the connection attempt failed.
     */
    void onConnectionRejected(const crossbow::string& data);

    /**
     * @brief The connection has been established
     *
     * Invoke the onConnected handler to inform the owner that the socket is now connected.
     */
    void onConnectionEstablished(const crossbow::string& data);

    /**
     * @brief The remote connection has been disconnected
     *
     * Executed when the remote host triggered a disconnect. Invoke the onDisconnect handler so that the socket can
     * close on the local host.
     */
    void onDisconnected() {
        mHandler->onDisconnect();
        // TODO Call disconnect here directly? The socket has to call it anyway to succesfully shutdown the connection
    }

    /**
     * @brief The connection has exited the Timewait state
     *
     * After disconnecting the connection any packets that were still in flight have now been processed by the NIC. Tell
     * the connection to drain the completion queue so it can be disconnected.
     */
    void onTimewaitExit();

    /**
     * @brief The connection completed a receive operation
     */
    void onReceive(const void* buffer, uint32_t length, const std::error_code& ec) {
        mHandler->onReceive(buffer, length, ec);
    }

    /**
     * @brief The connection completed a send operation
     */
    void onSend(uint32_t userId, const std::error_code& ec) {
        mHandler->onSend(userId, ec);
    }

    /**
     * @brief The connection completed a read operation
     */
    void onRead(uint32_t userId, const std::error_code& ec) {
        mHandler->onRead(userId, ec);
    }

    /**
     * @brief The connection completed a write operation
     */
    void onWrite(uint32_t userId, const std::error_code& ec) {
        mHandler->onWrite(userId, ec);
    }

    /**
     * @brief The completion queue processed all pending work completions and is now disconnected
     */
    void onDrained();

    /// The completion context of the underlying NIC
    CompletionContext* mContext;

    /// Callback handlers for events occuring on this socket
    InfinibandSocketHandler* mHandler;

    /// The private data to send while connecting
    /// This value will only be set during the connection process and freed when the connection is established.
    crossbow::string mData;
};

extern template class InfinibandBaseSocket<InfinibandSocketImpl>;

} // namespace infinio
} // namespace crossbow
