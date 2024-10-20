//===-- TCPSocket.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(_MSC_VER)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "lldb/Host/common/TCPSocket.h"

#include "lldb/Host/Config.h"
#include "lldb/Host/MainLoop.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/WindowsError.h"
#include "llvm/Support/raw_ostream.h"

#if LLDB_ENABLE_POSIX
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#endif

using namespace lldb;
using namespace lldb_private;

static const int kType = SOCK_STREAM;

TCPSocket::TCPSocket(bool should_close) : Socket(ProtocolTcp, should_close) {}

TCPSocket::TCPSocket(NativeSocket socket, bool should_close)
    : Socket(ProtocolTcp, should_close) {
  m_socket = socket;
}

llvm::Expected<TCPSocket::Pair> TCPSocket::CreatePair() {
  auto expected_listen_socket = ListeningTCPSocket::Create("localhost:0");
  if (!expected_listen_socket)
    return expected_listen_socket.takeError();
  ListeningTCPSocket &listener = **expected_listen_socket;

  std::string connect_address = llvm::StringRef(listener.GetConnectionURIs()[0])
                                    .split("://")
                                    .second.str();

  auto connect_socket_up = std::make_unique<TCPSocket>(true);
  if (Status error = connect_socket_up->Connect(connect_address); error.Fail())
    return error.takeError();

  // Connection has already been made above, so a short timeout is sufficient.
  Socket *accept_socket;
  if (Status error = listener.Accept(std::chrono::seconds(1), accept_socket);
      error.Fail())
    return error.takeError();

  return Pair(
      std::move(connect_socket_up),
      std::unique_ptr<TCPSocket>(static_cast<TCPSocket *>(accept_socket)));
}

bool TCPSocket::IsValid() const {
  return m_socket != kInvalidSocketValue;
}

// Return the port number that is being used by the socket.
uint16_t TCPSocket::GetLocalPortNumber() const {
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getsockname(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetPort();
  }
  return 0;
}

std::string TCPSocket::GetLocalIPAddress() const {
  // We bound to port zero, so we need to figure out which port we actually
  // bound to
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getsockname(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetIPAddress();
  }
  return "";
}

uint16_t TCPSocket::GetRemotePortNumber() const {
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getpeername(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetPort();
  }
  return 0;
}

std::string TCPSocket::GetRemoteIPAddress() const {
  // We bound to port zero, so we need to figure out which port we actually
  // bound to
  if (m_socket != kInvalidSocketValue) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getpeername(m_socket, sock_addr, &sock_addr_len) == 0)
      return sock_addr.GetIPAddress();
  }
  return "";
}

std::string TCPSocket::GetRemoteConnectionURI() const {
  if (m_socket != kInvalidSocketValue) {
    return std::string(llvm::formatv(
        "connect://[{0}]:{1}", GetRemoteIPAddress(), GetRemotePortNumber()));
  }
  return "";
}

Status TCPSocket::Connect(llvm::StringRef name) {

  Log *log = GetLog(LLDBLog::Communication);
  LLDB_LOG(log, "Connect to host/port {0}", name);

  llvm::Expected<Socket::HostAndPort> host_port = Socket::DecodeHostAndPort(name);
  if (!host_port)
    return Status::FromError(host_port.takeError());

  std::vector<SocketAddress> addresses =
      SocketAddress::GetAddressInfo(host_port->hostname.c_str(), nullptr,
                                    AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
  llvm::Error all_errors = llvm::Error::success();
  for (SocketAddress &address : addresses) {
    llvm::Expected<NativeSocket> sockfd =
        socket_detail::CreateSocket(address.GetFamily(), kType, IPPROTO_TCP);
    if (!sockfd) {
      all_errors = joinErrors(std::move(all_errors), sockfd.takeError());
      continue;
    }

    address.SetPort(host_port->port);
    llvm::Error err = socket_detail::ConnectSocket(*sockfd,
                                    &address.sockaddr(),
                                    address.GetLength());
    if (!err)
      err = socket_detail::SetSocketOption(*sockfd, IPPROTO_TCP, TCP_NODELAY, 1);

    if (err) {
      consumeError(socket_detail::CloseSocket(*sockfd));
      all_errors = joinErrors(std::move(all_errors), std::move(err));
      continue;
    }

    m_socket = *sockfd;
    consumeError(std::move(all_errors));
    return Status();
  }

  return Status::FromError(std::move(all_errors));
}

llvm::Expected<std::unique_ptr<ListeningTCPSocket>>
ListeningTCPSocket::Create(llvm::StringRef name, int backlog) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOG(log, "Listen to {0}", name);

  llvm::Expected<Socket::HostAndPort> host_port = Socket::DecodeHostAndPort(name);
  if (!host_port)
    return host_port.takeError();

  if (host_port->hostname == "*")
    host_port->hostname = "0.0.0.0";
  std::vector<SocketAddress> addresses = SocketAddress::GetAddressInfo(
      host_port->hostname.c_str(), nullptr, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
  llvm::Error all_errors = llvm::Error::success();
  llvm::DenseMap<int, SocketAddress> sockets;
  for (SocketAddress &address : addresses) {
    llvm::Expected<NativeSocket> socket =
        socket_detail::CreateSocket(address.GetFamily(), kType, IPPROTO_TCP);
    if (!socket) {
      all_errors = joinErrors(std::move(all_errors), socket.takeError());
      continue;
    }

    // enable local address reuse
    if (llvm::Error err = socket_detail::SetSocketOption(*socket, SOL_SOCKET, SO_REUSEADDR, 1))  {
      consumeError(socket_detail::CloseSocket(*socket));
      all_errors = joinErrors(std::move(all_errors), std::move(err));
      continue;
    }

    SocketAddress listen_address = address;
    if(!listen_address.IsLocalhost())
      listen_address.SetToAnyAddress(address.GetFamily(), host_port->port);
    else
      listen_address.SetPort(host_port->port);

    llvm::Error err = socket_detail::BindSocket(*socket, &listen_address.sockaddr(),
          listen_address.GetLength());
    if (!err)
      err = socket_detail::ListenSocket(*socket, backlog);

    if (err) {
      consumeError(socket_detail::CloseSocket(*socket));
      all_errors = joinErrors(std::move(all_errors), std::move(err));
      continue;
    }

    if (host_port->port == 0) {
      socklen_t sa_len = address.GetLength();
      if (getsockname(*socket, &address.sockaddr(), &sa_len) == 0)
        host_port->port = address.GetPort();
    }
    sockets[*socket] = address;
  }

  if (sockets.empty())
    return all_errors;
  consumeError(std::move(all_errors));
  return std::unique_ptr<ListeningTCPSocket>(new ListeningTCPSocket(std::move(sockets)));
}

ListeningTCPSocket::~ListeningTCPSocket() {
  for (auto [sockfd, addr] : m_sockets)
    consumeError(socket_detail::CloseSocket(sockfd));
  m_sockets.clear();
}

std::vector<std::string> ListeningTCPSocket::GetConnectionURIs() const {
  std::vector<std::string> URIs;
  for (const auto &[fd, addr] : m_sockets)
    URIs.emplace_back(llvm::formatv("connection://[{0}]:{1}",
                                    addr.GetIPAddress(), addr.GetPort()));
  return URIs;
}

llvm::Expected<std::vector<MainLoopBase::ReadHandleUP>>
ListeningTCPSocket::Accept(MainLoopBase &loop,
                  std::function<void(std::unique_ptr<Socket> socket)> sock_cb) {
  std::vector<MainLoopBase::ReadHandleUP> handles;
  for (auto socket : m_sockets) {
    auto fd = socket.first;
    const lldb_private::SocketAddress &AddrIn = socket.second;
    auto io_sp = std::make_shared<TCPSocket>(fd, false);
    auto cb = [fd, AddrIn, sock_cb](MainLoopBase &loop) {
      lldb_private::SocketAddress AcceptAddr;
      socklen_t sa_len = AcceptAddr.GetMaxLength();
      llvm::Expected<NativeSocket> sockfd =
          socket_detail::AcceptSocket(fd, &AcceptAddr.sockaddr(), &sa_len);
      Log *log = GetLog(LLDBLog::Host);
      if (!sockfd) {
        LLDB_LOG_ERROR(log, sockfd.takeError(), "AcceptSocket({1}): {0}", fd);
        return;
      }

      if (!AddrIn.IsAnyAddr() && AcceptAddr != AddrIn) {
        consumeError(socket_detail::CloseSocket(*sockfd));
        LLDB_LOG(log, "rejecting incoming connection from {0} (expecting {1})",
                 AcceptAddr.GetIPAddress(), AddrIn.GetIPAddress());
        return;
      }
      auto sock_up = std::make_unique<TCPSocket>(*sockfd, /*should_close=*/true);

      // Keep our TCP packets coming without any delays.
      consumeError(sock_up->SetOptionNoDelay());

      sock_cb(std::move(sock_up));
    };
    Status error;
    handles.emplace_back(loop.RegisterReadObject(io_sp, cb, error));
    if (error.Fail())
      return error.ToError();
  }

  return handles;
}

uint16_t ListeningTCPSocket::GetLocalPortNumber() const {
  if (!m_sockets.empty()) {
    SocketAddress sock_addr;
    socklen_t sock_addr_len = sock_addr.GetMaxLength();
    if (::getsockname(m_sockets.begin()->first, sock_addr,
                      &sock_addr_len) == 0)
      return sock_addr.GetPort();
   }
  return 0;
}

llvm::Error TCPSocket::SetOptionNoDelay() {
  return SetOption(IPPROTO_TCP, TCP_NODELAY, 1);
}

llvm::Error TCPSocket::SetOptionReuseAddress() {
  return SetOption(SOL_SOCKET, SO_REUSEADDR, 1);
}
