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
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "aemu/base/async/AsyncSocketAdapter.h"
#include "aemu/base/async/AsyncWriter.h"
#include "aemu/base/async/Looper.h"
#include "aemu/base/containers/BufferQueue.h"
#include "aemu/base/sockets/ScopedSocket.h"
#include "aemu/base/sockets/SocketUtils.h"
#include "aemu/base/synchronization/Lock.h"

namespace android {
namespace base {
using MessageQueue = BufferQueue<std::string>;

/**
 * @brief An asynchronous socket implementation using Looper.
 *
 * This class provides a way to perform socket operations asynchronously
 * using the Looper mechanism. It supports both outgoing and incoming
 * connections.
 */
class AsyncSocket : public AsyncSocketAdapter {
   public:
    /**
     * @brief Constructs an AsyncSocket for an outgoing connection.
     *
     * @param looper The Looper to use for asynchronous operations.
     * @param port The port to connect to.
     */
    AsyncSocket(Looper* looper, int port);

    /**
     * @brief Constructs an AsyncSocket for an incoming connection.
     *
     * @param looper The Looper to use for asynchronous operations.
     * @param socket The ScopedSocket representing the accepted connection.
     */
    AsyncSocket(Looper* looper, ScopedSocket socket);
    ~AsyncSocket();

    /**
     * @brief Closes the socket.
     */
    void close() override;

    /**
     * @brief Receives data from the socket.
     *
     * @param buffer The buffer to receive the data into.
     * @param bufferSize The size of the buffer.
     * @return The number of bytes received, or -1 if an error occurred.
     */
    ssize_t recv(char* buffer, uint64_t bufferSize) override;

    /**
     * @brief Sends data over the socket, note these are send
     *        asynchronously!
     *
     *
     * @param buffer The buffer containing the data to send.
     * @param bufferSize The size of the data to send.
     * @return The number of bytes sent, or -1 if an error occurred.
     */
    ssize_t send(const char* buffer, uint64_t bufferSize) override;

    // Number of bytes in the buffer (scheduled to be send)
    size_t sendBuffer() { return mSendBuffer; };

    // Wait at most duration for the send buffer to be cleared
    bool waitForSend(const std::chrono::milliseconds& rel_time);

    /**
     * @brief Attempts to asynchronously connect the socket.
     *
     * @return True if the connection attempt was successful, false
     * otherwise.
     */
    bool connect() override;

    /**
     * @brief Checks if the socket is connected.
     *
     * @return True if the socket is connected, false otherwise.
     */
    bool connected() override;

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
    bool connectSync(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) override;

    /**
     * @brief Disposes the socket.
     *
     * After this method returns, the following should hold:
     * - No events will be delivered.
     * - No send/recv/connect/close calls will be made.
     * - The socket can be closed, and any ongoing connects should stop.
     */
    void dispose() override;

    // Callback function for write completion.
    void onWrite();

    // Callback function for read availability.
    void onRead();

    // Indicates that the socket is interested in reading data.
    void wantRead();

   private:
    // Indicates that the socket is interested in writing data.
    void wantWrite();

    // Attempts to connect to the specified port.
    void connectToPort();

    void scheduleCallback(std::function<void()> callback);

    // Size of the write buffer.
    static const int WRITE_BUFFER_SIZE = 1024;

    ScopedSocket mSocket;

    // Port to connect to, or -1 if this is an incoming socket.
    int mPort;
    Looper* mLooper;
    bool mConnecting = false;
    std::unique_ptr<Looper::FdWatch> mFdWatch;

    android::base::AsyncWriter mAsyncWriter;

    // Thread for handling connection attempts.
    std::unique_ptr<std::thread> mConnectThread;

    // Queue of messages to be written.
    MessageQueue mWriteQueue;
    Lock mWriteQueueLock;

    // Mutex for synchronizing access to the FdWatch.
    std::mutex mWatchLock;

    // Condition variable for signaling changes in FdWatch state.
    std::condition_variable mWatchLockCv;

    // Condition variable for signaling changes in sendBuffer
    std::mutex mSendBufferMutex;
    std::condition_variable mSendBufferCv;
    std::atomic<size_t> mSendBuffer{0};

    // Write buffer used by the async writer.
    std::string mWriteBuffer;

    // Mutex to track callback activity, this mutex will be taken
    // when a callback is active.
    std::recursive_mutex mListenerLock;

    std::mutex mInflightMutex;
    std::condition_variable mInflightCv;
    int mInflight{0};
    bool mClosing{false};
};

}  // namespace base
}  // namespace android
