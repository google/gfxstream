
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#ifdef _MSC_VER
#include "aemu/base/msvc.h"
#else
#include <unistd.h>
#endif

namespace android {
namespace base {

class AsyncSocketAdapter;

/**
 * @brief An interface for listening to events from an
 * AsyncSocketAdapter.
 */
class AsyncSocketEventListener {
   public:
    virtual ~AsyncSocketEventListener() = default;

    /**
     * @brief Called when bytes can be read from the socket.
     *
     * @param socket The socket that has bytes available for reading.
     */
    virtual void onRead(AsyncSocketAdapter* socket) = 0;

    /**
     * @brief Called when this socket is closed.
     *
     * @param socket The socket that was closed.
     * @param err The error code associated with the closure, if any.
     */
    virtual void onClose(AsyncSocketAdapter* socket, int err) = 0;

    /**
     * @brief Called when this socket (re)establishes a connection.
     *
     * This callback is only invoked for sockets that initiate an outgoing
     * connection.
     *
     * @param socket The socket that successfully connected.
     */
    virtual void onConnected(AsyncSocketAdapter* socket) = 0;
};

/**
 * @brief A connected asynchronous socket.
 *
 */
class AsyncSocketAdapter {
   public:
    virtual ~AsyncSocketAdapter() = default;

    /**
     * @brief Sets the event listener for this socket.
     *
     * @param listener The listener to receive events.
     */
    void setSocketEventListener(AsyncSocketEventListener* listener) { mListener = listener; }

    /**
     * @brief Receives data from the socket.
     *
     * You should call this method in response to an onRead event.
     *
     * @param buffer The buffer to receive the data into.
     * @param bufferSize The size of the buffer.
     * @return The number of bytes received, or -1 if an error occurred.
     */
    virtual ssize_t recv(char* buffer, uint64_t bufferSize) = 0;

    /**
     * @brief Sends data over the socket.
     *
     * @param buffer The buffer containing the data to send.
     * @param bufferSize The size of the data to send.
     * @return The number of bytes sent, or -1 if an error occurred.
     */
    virtual ssize_t send(const char* buffer, uint64_t bufferSize) = 0;

    /**
     * @brief Closes the socket.
     */
    virtual void close() = 0;

    /**
     * @brief Checks if the socket is connected.
     *
     * @return True if the socket is connected, false otherwise.
     */
    virtual bool connected() = 0;

    /**
     * @brief Attempts to reconnect the socket.
     *
     * @return True if the reconnection attempt was successful, false
     * otherwise.
     */
    virtual bool connect() = 0;

    /**
     * @brief Connects the socket synchronously.
     *
     * The onConnected callback will have been called before this function
     * returns. This means that if you lock on mutex x before calling this you
     * will not be able to lock mutex x in the onConnected callback.
     *
     * @param timeout The maximum time to wait for the connection to be
     * established.
     * @return True if the connection was successful, false if it timed out.
     */
    virtual bool connectSync(std::chrono::milliseconds timeout) = 0;

    /**
     * @brief Disposes the socket.
     *
     * After this method returns, the following should hold:
     * - No events will be delivered.
     * - No send/recv/connect/close calls will be made.
     * - The socket can be closed, and any ongoing connects should stop.
     */
    virtual void dispose() = 0;

   protected:
    AsyncSocketEventListener* mListener = nullptr;
};

/**
 * @brief A simplified wrapper for `AsyncSocketAdapter` that provides
 *        easy-to-use callbacks for handling read and close events.
 *        This makes the underlying implementations (RtcSocket/AsyncSocket)
 *        easier to use.
 *
 * This class handles incoming socket connections and provides a convenient
 * interface for receiving and sending data and handling socket closures.
 */
class SimpleAsyncSocket : public AsyncSocketEventListener {
   public:
    /**
     * @brief Callback type for handling received data.
     *
     * @param data The received data as a `std::string_view`. Note that this
     *             `std::string_view` will become invalid upon return from this
     *             callback. If you need to store the data for later use, you
     *             must copy it.
     */
    using OnReadCallback = std::function<void(std::string_view)>;

    /**
     * @brief Callback type for handling socket closures.
     */
    using OnCloseCallback = std::function<void()>;

    /**
     * @brief Constructs a `SimpleAsyncSocket`.
     *
     * @param socket The underlying `AsyncSocketAdapter` to wrap.
     * @param onRead The callback to invoke when data is received.
     * @param onClose The callback to invoke when the socket is closed.
     */
    SimpleAsyncSocket(AsyncSocketAdapter* socket, OnReadCallback onRead, OnCloseCallback onClose)
        : mSocket(std::move(socket)), mOnRead(std::move(onRead)), mOnClose(std::move(onClose)) {
        mSocket->setSocketEventListener(this);
    }

    virtual ~SimpleAsyncSocket() = default;

    /**
     * @brief Implementation of `AsyncSocketEventListener::onRead`.
     *
     * This method is called when data is available to be read from the socket.
     * It reads data in chunks and invokes the `onRead` callback for each chunk.
     *
     * @param socket The `AsyncSocketAdapter` that has data available for reading.
     */
    void onRead(AsyncSocketAdapter* socket) override {
        // See https://www.evanjones.ca/read-write-buffer-size.html
        constexpr int buffer_size = 32 * 1024;
        char buffer[buffer_size];
        do {
            int bytes = mSocket->recv(buffer, sizeof(buffer));
            if (bytes <= 0) {
                break;
            }
            mOnRead(std::string_view(buffer, bytes));
        } while (true);
    };

    void onClose(AsyncSocketAdapter* socket, int err) override {
        if (mOnClose) mOnClose();
    };

    void onConnected(AsyncSocketAdapter* socket) override {}

    /**
     * @brief Sends data over the socket.
     *
     * @param buffer The buffer containing the data to send.
     * @param bufferSize The size of the data to send.
     * @return The number of bytes sent, or -1 if an error occurred.
     */
    ssize_t send(const char* buffer, uint64_t bufferSize) {
        return mSocket->send(buffer, bufferSize);
    }

    /**
     * @brief Closes the socket.
     */
    void close() { mSocket->close(); }

    /**
     * @brief Disposes the socket.
     */
    void dispose() { mSocket->dispose(); }

   protected:
    AsyncSocketAdapter* mSocket;  ///< The underlying socket.
    OnReadCallback mOnRead;       ///< Callback for handling received data.
    OnCloseCallback mOnClose;     ///< Callback for handling socket closures.
};
}  // namespace base
}  // namespace android