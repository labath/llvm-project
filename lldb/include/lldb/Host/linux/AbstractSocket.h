//===-- AbstractSocket.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AbstractSocket_h_
#define liblldb_AbstractSocket_h_

#include "lldb/Host/posix/DomainSocket.h"

namespace lldb_private {
class AbstractSocket : public DomainSocket {
public:
  AbstractSocket();
  AbstractSocket(NativeSocket socket, bool should_close);

protected:
  size_t GetNameOffset() const override;
};

class ListeningAbstractSocket : public ListeningDomainSocket {
public:
  static llvm::Expected<std::unique_ptr<ListeningAbstractSocket>>
  Create(llvm::StringRef name, int backlog = DefaultBacklog);

  using socket_type = AbstractSocket;

  std::vector<std::string> GetConnectionURIs() const override;

  using ListeningSocket::Accept;
  llvm::Expected<std::vector<MainLoopBase::ReadHandleUP>>
  Accept(MainLoopBase &loop,
         std::function<void(std::unique_ptr<Socket> socket)> sock_cb) override;

private:
  using ListeningDomainSocket::ListeningDomainSocket;
};

} // namespace lldb_private

#endif // ifndef liblldb_AbstractSocket_h_
