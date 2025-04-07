// Copyright (C) 2022 The Android Open Source Project
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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <future>
#include <istream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "aemu/base/streams/RingStreambuf.h"
#include "aemu/base/ThreadAnnotations.h"

namespace android {
namespace base {

class Command;

using android::base::streams::RingStreambuf;
using CommandArguments = std::vector<std::string>;
using Pid = int;
using ProcessExitCode = int;

/**
 * Represents a process running within the operating system.
 */
class Process {
public:
    virtual ~Process() = default;

    /**
     * @return The process ID (PID) of the process, or -1 if invalid.
     */
    Pid pid() const { return mPid; };

    /**
     * @return The name of the process executable. Note that this information
     *         might not be immediately available, especially shortly after
     *         the process has been started.
     */
    virtual std::string exe() const = 0;

    /**
     * Retrieves the exit code of the process. This method will block until
     * the process has finished or is detached.
     *
     * @return The process exit code. This can return INT_MIN in case of
     *         failures retrieving the exit code.
     */
    ProcessExitCode exitCode() const;

    /**
     * Forcibly terminates the process (similar to sending SIGKILL).
     *
     * @return True if the process was successfully terminated, false otherwise.
     */
    virtual bool terminate() = 0;

    /**
     * Checks if the process is currently alive according to the operating
     * system.
     *
     * @return True if the process is alive, false otherwise.
     */
    virtual bool isAlive() const = 0;

    /**
     * Waits for the process to complete, or until the specified timeout
     * duration has elapsed.
     *
     * @param timeout_duration The maximum duration to wait for process
     *        completion.
     * @return A std::future_status value indicating whether the wait
     *         completed due to process termination or timeout.
     */
    virtual std::future_status wait_for(
            const std::chrono::milliseconds timeout_duration) const {
        return wait_for_kernel(timeout_duration);
    }

    /**
     * Waits for the process to complete, or until the specified time point
     * has been reached.
     *
     * @tparam Clock The clock type used for the timeout.
     * @tparam Duration The duration type used for the timeout.
     * @param timeout_time The time point at which the wait should timeout.
     * @return A std::future_status value indicating whether the wait
     *         completed due to process termination or timeout.
     */
    template <class Clock, class Duration>
    std::future_status wait_until(
            const std::chrono::time_point<Clock, Duration>& timeout_time)
            const {
        return wait_for(timeout_time - std::chrono::steady_clock::now());
    };

    bool operator==(const Process& rhs) const { return (mPid == rhs.mPid); }
    bool operator!=(const Process& rhs) const { return !operator==(rhs); }

    /**
     * Retrieves a Process object representing the process with the given PID.
     *
     * @param pid The process ID (PID) to search for.
     * @return A unique pointer to a Process object representing the process,
     *         or nullptr if no such process exists.
     */
    static std::unique_ptr<Process> fromPid(Pid pid);

    /**
     * Retrieves a list of Process objects representing processes whose
     * executable name contains the specified name substring.
     *
     * Note: There might be a delay between the creation of a process and its
     * appearance in the process list. This delay can vary depending on the
     * operating system and system load.
     *
     * @param name The name substring to search for in process names.
     * @return A vector of unique pointers to Process objects representing the
     *         matching processes. If no matching processes are found, the
     *         vector will be empty.
     */
    static std::vector<std::unique_ptr<Process>> fromName(std::string name);

    /**
     * @return A unique pointer to a Process object representing the current
     *         process.
     */
    static std::unique_ptr<Process> me();

protected:
    /**
     * Retrieves the exit code of the process without blocking.
     *
     * @return An optional containing the process exit code if available,
     *         or std::nullopt if the process is still running or the exit
     *         code cannot be retrieved.
     */
    virtual std::optional<ProcessExitCode> getExitCode() const = 0;

    /**
     * Waits for the process to complete using an operating system-level call,
     * without using any additional polling mechanisms.
     *
     * @param timeout_duration The maximum duration to wait for process
     *        completion.
     * @return A std::future_status value indicating whether the wait
     *         completed due to process termination or timeout.
     */
    virtual std::future_status wait_for_kernel(
            const std::chrono::milliseconds timeout_duration) const = 0;

    Pid mPid;
};

/**
 * Represents the output (stdout and stderr) of a process.
 */
class ProcessOutput {
public:
    virtual ~ProcessOutput() = default;

    /**
     * Consumes the entire output stream and returns it as a string.
     *
     * @return The entire process output as a string.
     */
    virtual std::string asString() = 0;

    /**
     * Provides access to the output stream, which can be used to read the
     * process output incrementally. This method may block until data is
     * available from the child process.
     *
     * @return A reference to the output stream.
     */
    virtual std::istream& asStream() = 0;
};

/**
 * The ProcessOverseer class is responsible for monitoring a child process
 * and capturing its output (stdout and stderr).
 */
class ProcessOverseer {
public:
    virtual ~ProcessOverseer() = default;

    /**
     * Starts monitoring the child process and capturing its output.
     *
     * The overseer should:
     * - Write captured output to the provided `out` and `err`
     *   RingStreambuf objects.
     * - Close the RingStreambuf objects when the corresponding output streams
     *   are closed by the child process.
     * - Return from this method when it can no longer read or write from the
     *   child process's stdout and stderr.
     *
     * @param out The RingStreambuf object to write captured stdout output to.
     * @param err The RingStreambuf object to write captured stderr output to.
     */
    virtual void start(RingStreambuf* out, RingStreambuf* err) = 0;

    /**
     * Stops monitoring the child process and releases any resources held by
     * the overseer.
     *
     * After this method returns:
     * - No further writes should be made to the `out` and `err`
     *   RingStreambuf objects.
     * - All resources associated with the overseer should be released.
     * - Calling the `start` method again should result in an error or return
     *   immediately.
     */
    virtual void stop() = 0;
};

/**
 * A ProcessOverseer implementation that does nothing. This can be used for
 * detached processes or in testing scenarios where process output monitoring
 * is not required.
 */
class NullOverseer : public ProcessOverseer {
public:
    virtual void start(RingStreambuf* out, RingStreambuf* err) override {}
    virtual void stop() override {}
};

/**
 * Represents a running process that can be interacted with, such as reading
 * its output or terminating it.
 *
 * You typically obtain an ObservableProcess object by executing a Command.
 *
 * Example:
 * ```cpp
 * auto p = Command::create({"ls"}).execute();
 * if (p->exitCode() == 0) {
 *     auto list = p->out()->asString();
 * }
 * ```
 */
class ObservableProcess : public Process {
public:
    // Kills the process..
    virtual ~ObservableProcess();

    /**
     * @return A pointer to the ProcessOutput object representing the child
     *         process's standard output (stdout), or nullptr if the process
     *         was started in detached mode.
     */
    ProcessOutput* out() { return mStdOut.get(); };

    /**
     * @return A pointer to the ProcessOutput object representing the child
     *         process's standard error (stderr), or nullptr if the process
     *         was started in detached mode.
     */
    ProcessOutput* err() { return mStdErr.get(); };

    /**
     * Detaches the process overseer, stopping the monitoring of the child
     * process's output and preventing the process from being automatically
     * terminated when the ObservableProcess object goes out of scope.
     *
     * After calling this method:
     * - You will no longer be able to read the child process's stdout and
     *   stderr.
     * - The child process will continue running even after the
     *   ObservableProcess object is destroyed.
     */
    void detach();

    std::future_status wait_for(
            const std::chrono::milliseconds timeout_duration) const override;

protected:
    /**
     * Subclasses should implement this method to handle the actual process
     * creation and launch.
     *
     * @param args The command line arguments to pass to the child process.
     * @param captureOutput Whether to capture the child process's output
     *        (stdout and stderr).
     * @return An optional containing the PID of the newly created process if
     *         successful, or std::nullopt if process creation failed.
     */
    virtual std::optional<Pid> createProcess(const CommandArguments& args,
                                             bool captureOutput,
                                             bool replace) = 0;

    /**
     * Creates the ProcessOverseer object responsible for monitoring the child
     * process and capturing its output.
     *
     * @return A unique pointer to the created ProcessOverseer object.
     */
    virtual std::unique_ptr<ProcessOverseer> createOverseer() = 0;

    // True if no overseer is needed
    bool mDeamon{false};

    // True if we want to inherit all the fds/handles.
    bool mInherit{false};

private:
    void runOverseer();

    std::unique_ptr<ProcessOverseer> mOverseer;
    std::unique_ptr<std::thread> mOverseerThread;
    bool mOverseerActive GUARDED_BY(mOverseerMutex) {false};
    mutable std::mutex mOverseerMutex;
    mutable std::condition_variable mOverseerCv;

    std::unique_ptr<ProcessOutput> mStdOut;
    std::unique_ptr<ProcessOutput> mStdErr;

    friend Command;
};
}  // namespace base
}  // namespace android
