/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LinuxSyscalls.h"

#include <LibCore/System.h>

#ifndef AK_OS_WINDOWS
#    include <fcntl.h>
#    include <poll.h>
#    include <sys/eventfd.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace MyceliumVR::ProcessTransport::LinuxSyscalls {

ErrorOr<int> create_event_fd()
{
#ifdef AK_OS_WINDOWS
    return Error::from_string_literal("Linux syscalls are not available on Windows");
#else
    int fd = eventfd(0, EFD_CLOEXEC);
    if (fd < 0)
        return Error::from_errno(errno);
    TRY(set_fd_nonblocking(fd));
    return fd;
#endif
}

ErrorOr<void> set_fd_nonblocking(int fd)
{
#ifdef AK_OS_WINDOWS
    (void)fd;
    return Error::from_string_literal("Linux syscalls are not available on Windows");
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return Error::from_errno(errno);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return Error::from_errno(errno);
    return {};
#endif
}

ErrorOr<void> write_event_fd(int fd)
{
#ifdef AK_OS_WINDOWS
    (void)fd;
    return Error::from_string_literal("Linux syscalls are not available on Windows");
#else
    uint64_t value = 1;
    if (write(fd, &value, sizeof(value)) < 0)
        return Error::from_errno(errno);
    return {};
#endif
}

ErrorOr<bool> poll_readable(int fd, u32 timeout_ms)
{
#ifdef AK_OS_WINDOWS
    (void)fd;
    (void)timeout_ms;
    return Error::from_string_literal("Linux syscalls are not available on Windows");
#else
    pollfd pfd {
        .fd = fd,
        .events = POLLIN,
        .revents = 0,
    };
    int rc = poll(&pfd, 1, static_cast<int>(timeout_ms));
    if (rc < 0)
        return Error::from_errno(errno);
    return rc > 0;
#endif
}

ErrorOr<void> drain_event_fd(int fd)
{
#ifdef AK_OS_WINDOWS
    (void)fd;
    return Error::from_string_literal("Linux syscalls are not available on Windows");
#else
    while (true) {
        uint64_t value = 0;
        ssize_t rc = read(fd, &value, sizeof(value));
        if (rc == sizeof(value))
            continue;
        if (rc < 0 && errno == EAGAIN)
            return {};
        if (rc < 0)
            return Error::from_errno(errno);
        return {};
    }
#endif
}

ErrorOr<void> create_local_socket_pair(int fds[2])
{
#ifdef AK_OS_WINDOWS
    (void)fds;
    return Error::from_string_literal("Linux syscalls are not available on Windows");
#else
    return Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
#endif
}

ErrorOr<int> duplicate_fd(int fd)
{
    return Core::System::dup(fd);
}

ErrorOr<void> close_fd(int fd)
{
    if (fd < 0)
        return {};
    return Core::System::close(fd);
}

}
