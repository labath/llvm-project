//===-- DomainSocket.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIX_DOMAINSOCKET_H
#define LLDB_HOST_POSIX_DOMAINSOCKET_H

#include "lldb/Host/Socket.h"
#include <string>
#include <vector>

namespace lldb_private {

class ListeningDomainSocket;

class DomainSocket : public Socket {
public:
  using Pair =
      std::pair<std::unique_ptr<DomainSocket>, std::unique_ptr<DomainSocket>>;
  static llvm::Expected<Pair> CreatePair();

  using listener_type = ListeningDomainSocket;

  DomainSocket(NativeSocket socket, bool should_close);
  explicit DomainSocket(bool should_close);

  Status Connect(llvm::StringRef name) override;

  std::string GetRemoteConnectionURI() const override;

  static llvm::Expected<std::unique_ptr<DomainSocket>>
  FromBoundNativeSocket(NativeSocket sockfd, bool should_close);

protected:
  explicit DomainSocket(SocketProtocol protocol);

  virtual size_t GetNameOffset() const;
  std::string GetSocketName() const;
};

class ListeningDomainSocket : public ListeningSocket {
public:
  static llvm::Expected<std::unique_ptr<ListeningDomainSocket>>
  Create(llvm::StringRef name, int backlog = DefaultBacklog);

  ~ListeningDomainSocket() override;

  using socket_type = DomainSocket;

  std::vector<std::string> GetConnectionURIs() const override;

  using ListeningSocket::Accept;
  llvm::Expected<std::vector<MainLoopBase::ReadHandleUP>>
  Accept(MainLoopBase &loop,
         std::function<void(std::unique_ptr<Socket> socket)> sock_cb) override;

private:
  NativeSocket m_socket;

  ListeningDomainSocket(NativeSocket socket) : m_socket(socket) {}
};

} // namespace lldb_private

#endif // LLDB_HOST_POSIX_DOMAINSOCKET_H
