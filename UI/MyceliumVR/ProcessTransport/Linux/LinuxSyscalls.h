/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>

namespace MyceliumVR::ProcessTransport::LinuxSyscalls {

ErrorOr<int> create_event_fd();
ErrorOr<void> set_fd_nonblocking(int fd);
ErrorOr<void> write_event_fd(int fd);
ErrorOr<bool> poll_readable(int fd, u32 timeout_ms);
ErrorOr<void> drain_event_fd(int fd);

ErrorOr<void> create_local_socket_pair(int fds[2]);
ErrorOr<int> duplicate_fd(int fd);
ErrorOr<void> close_fd(int fd);

}
