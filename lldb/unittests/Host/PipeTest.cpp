//===-- PipeTest.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Pipe.h"
#include "TestingSupport/SubsystemRAII.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "gtest/gtest.h"

using namespace lldb_private;

class PipeTest : public testing::Test {
public:
  SubsystemRAII<FileSystem, HostInfo> subsystems;
};

TEST_F(PipeTest, CreateForReadingWithUniqueName) {
  llvm::Expected<Pipe::UnconnectedReadPipe> unconnected_pipe = Pipe::CreateForReadingWithUniqueName("PipeTest-CreateForReadingWithUniqueName");
  ASSERT_THAT_EXPECTED(unconnected_pipe, llvm::Succeeded());

  Pipe pipe2;
  ASSERT_THAT_ERROR(
      pipe2.OpenAsWriter(unconnected_pipe->GetName(), /*child_process_inherit=*/false).ToError(),
      llvm::Succeeded());

  llvm::Expected<Pipe> pipe = unconnected_pipe->Connect(std::chrono::milliseconds(0));
  ASSERT_THAT_EXPECTED(pipe, llvm::Succeeded());

  size_t bytes_written;
  ASSERT_THAT_ERROR(pipe2.Write("foo", 3, bytes_written).ToError(),
                    llvm::Succeeded());

  size_t bytes_read;
  char buf[3];
  ASSERT_THAT_ERROR(pipe->Read(buf, sizeof(buf), bytes_read).ToError(),
      llvm::Succeeded());
  EXPECT_EQ(bytes_read, 3u);
  EXPECT_EQ(llvm::StringRef(buf, 3), "foo");
}
