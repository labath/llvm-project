//===-- PipePosix.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_POSIX_PIPEPOSIX_H
#define LLDB_HOST_POSIX_PIPEPOSIX_H
#if defined(__cplusplus)

#include "lldb/Host/PipeBase.h"
#include "lldb/Utility/Timeout.h"
#include "llvm/Support/Process.h"

namespace lldb_private {

/// \class PipePosix PipePosix.h "lldb/Host/posix/PipePosix.h"
/// A posix-based implementation of Pipe, a class that abtracts
///        unix style pipes.
///
/// A class that abstracts the LLDB core from host pipe functionality.
class PipePosix : public PipeBase {
public:
  static int kInvalidDescriptor;

  PipePosix();
  PipePosix(lldb::pipe_t read, lldb::pipe_t write);
  PipePosix(const PipePosix &) = delete;
  PipePosix(PipePosix &&pipe_posix);
  PipePosix &operator=(const PipePosix &) = delete;
  PipePosix &operator=(PipePosix &&pipe_posix);

  ~PipePosix() override;

  Status CreateNew(bool child_process_inherit) override;
  Status CreateNew(llvm::StringRef name, bool child_process_inherit) override;

  class UnconnectedReadPipe {
  public:
    UnconnectedReadPipe(llvm::StringRef name, lldb::pipe_t fd)
      : m_name(name), m_fd(fd) {}

    llvm::StringRef GetName() { return m_name; }

    UnconnectedReadPipe(UnconnectedReadPipe &&rhs)
        : m_name(std::move(rhs.m_name)),
          m_fd(std::exchange(rhs.m_fd, kInvalidDescriptor)) {}
    void operator=(UnconnectedReadPipe &&) = delete;

    ~UnconnectedReadPipe() {
      if (m_fd != kInvalidDescriptor)
        llvm::sys::Process::SafelyCloseFileDescriptor(m_fd);
    }

    llvm::Expected<PipePosix> Connect(Timeout<std::milli> timeout) {
      return PipePosix(std::exchange(m_fd, kInvalidDescriptor),
                       kInvalidDescriptor);
    }

  private:
    llvm::SmallString<0> m_name;
    lldb::pipe_t m_fd;
  };
  static llvm::Expected<UnconnectedReadPipe>
  CreateForReadingWithUniqueName(llvm::StringRef prefix);

  Status
  OpenAsWriterWithTimeout(llvm::StringRef name, bool child_process_inherit,
                          const std::chrono::microseconds &timeout) override;

  bool CanRead() const override;
  bool CanWrite() const override;

  lldb::pipe_t GetReadPipe() const override {
    return lldb::pipe_t(GetReadFileDescriptor());
  }
  lldb::pipe_t GetWritePipe() const override {
    return lldb::pipe_t(GetWriteFileDescriptor());
  }

  int GetReadFileDescriptor() const override;
  int GetWriteFileDescriptor() const override;
  int ReleaseReadFileDescriptor() override;
  int ReleaseWriteFileDescriptor() override;
  void CloseReadFileDescriptor() override;
  void CloseWriteFileDescriptor() override;

  // Close both descriptors
  void Close() override;

  Status Delete(llvm::StringRef name) override;

  Status Write(const void *buf, size_t size, size_t &bytes_written) override;
  Status ReadWithTimeout(void *buf, size_t size,
                         const std::chrono::microseconds &timeout,
                         size_t &bytes_read) override;

private:
  int m_fds[2];
};

} // namespace lldb_private

#endif // #if defined(__cplusplus)
#endif // LLDB_HOST_POSIX_PIPEPOSIX_H
