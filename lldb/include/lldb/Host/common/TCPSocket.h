//===-- TCPSocket.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_COMMON_TCPSOCKET_H
#define LLDB_HOST_COMMON_TCPSOCKET_H

#include "lldb/Host/MainLoopBase.h"
#include "lldb/Host/Socket.h"
#include "lldb/Host/SocketAddress.h"
#include <map>
#include <string>
#include <vector>

namespace lldb_private {

class ListeningTCPSocket;

class TCPSocket : public Socket {
public:
  using listener_type = ListeningTCPSocket;

  explicit TCPSocket(bool should_close);
  TCPSocket(NativeSocket socket, bool should_close);

  using Pair =
      std::pair<std::unique_ptr<TCPSocket>, std::unique_ptr<TCPSocket>>;
  static llvm::Expected<Pair> CreatePair();

  // returns port number or 0 if error
  uint16_t GetLocalPortNumber() const;

  // returns ip address string or empty string if error
  std::string GetLocalIPAddress() const;

  // must be connected
  // returns port number or 0 if error
  uint16_t GetRemotePortNumber() const;

  // must be connected
  // returns ip address string or empty string if error
  std::string GetRemoteIPAddress() const;

  llvm::Error SetOptionNoDelay();
  llvm::Error SetOptionReuseAddress();

  Status Connect(llvm::StringRef name) override;

  bool IsValid() const override;

  std::string GetRemoteConnectionURI() const override;
};

class ListeningTCPSocket: public ListeningSocket {
public:
  static llvm::Expected<std::unique_ptr<ListeningTCPSocket>>
  Create(llvm::StringRef name, int backlog = DefaultBacklog);

  ~ListeningTCPSocket() override;

  using socket_type = TCPSocket;

  std::vector<std::string> GetConnectionURIs() const override;

  using ListeningSocket::Accept;
  llvm::Expected<std::vector<MainLoopBase::ReadHandleUP>>
  Accept(MainLoopBase &loop,
         std::function<void(std::unique_ptr<Socket> socket)> sock_cb) override;

  uint16_t GetLocalPortNumber() const;

private:
  ListeningTCPSocket(llvm::DenseMap<int, SocketAddress> sockets)
      : m_sockets(std::move(sockets)) {}

  llvm::DenseMap<int, SocketAddress> m_sockets;
};

} // namespace lldb_private

#endif // LLDB_HOST_COMMON_TCPSOCKET_H
