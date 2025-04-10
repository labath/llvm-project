//===-- HostTest.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Host.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/ProcessInfo.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <cerrno>
#include <sys/resource.h>

using namespace lldb_private;

namespace {
class HostTest : public testing::Test {
public:
  static void SetUpTestCase() {
    FileSystem::Initialize();
    HostInfo::Initialize();
  }
  static void TearDownTestCase() {
    HostInfo::Terminate();
    FileSystem::Terminate();
  }
};
} // namespace

static llvm::cl::opt<std::string> expect_path("expect-path");
static llvm::cl::opt<std::string> chdir_to("chdir-to");

TEST(XXX, YYY) {
  FileSystem::Initialize();
  HostInfo::Initialize();
  if (!expect_path.empty()) {
    // In subprocess.
    EXPECT_EQ(HostInfo::GetProgramFileSpec().GetPath(), expect_path.getValue());
    exit(HasFailure() ? 1 : 0);
  }

  FileSpec temp_dir = HostInfo::GetProcessTempDir();
  ASSERT_TRUE(temp_dir);
  FileSpec symlink = temp_dir.CopyByAppendingPathComponent("symlink");
  ASSERT_THAT_ERROR(FileSystem::Instance()
                        .Symlink(symlink, HostInfo::GetProgramFileSpec())
                        .takeError(),
                    llvm::Succeeded());

  EXPECT_EQ(llvm::sys::ExecuteAndWait(symlink.GetPath(),
                                      {"argv0", "--gtest_filter=XXX.YYY",
                                       "--expect-path=" + symlink.GetPath()}),
            0);

  HostInfo::Terminate();
  FileSystem::Terminate();
}

TEST(XXX, YYY2) {
  FileSystem::Initialize();
  HostInfo::Initialize();
  if (!expect_path.empty()) {
    // In subprocess.
    EXPECT_EQ(HostInfo::GetProgramFileSpec().GetPath(), expect_path.getValue());
    exit(HasFailure() ? 1 : 0);
  }

  llvm::SmallString<0> temp_dir;
  ASSERT_THAT_ERROR(llvm::errorCodeToError(FileSystem::Instance().GetRealPath(HostInfo::GetProcessTempDir().GetPath(), temp_dir)), llvm::Succeeded());
  FileSpec symlink = FileSpec(temp_dir).CopyByAppendingPathComponent("symlink");
  ASSERT_THAT_ERROR(FileSystem::Instance()
                        .Symlink(symlink, HostInfo::GetProgramFileSpec())
                        .takeError(),
                    llvm::Succeeded());

  llvm::SmallString<0> cwd;
  llvm::sys::fs::current_path(cwd);
  ASSERT_THAT_ERROR(llvm::errorCodeToError(
                        llvm::sys::fs::set_current_path(temp_dir)),
                    llvm::Succeeded());
  auto reset_cwd =
      llvm::make_scope_exit([&] { llvm::sys::fs::set_current_path(cwd); });

  EXPECT_EQ(llvm::sys::ExecuteAndWait("./symlink",
                                      {"argv0", "--gtest_filter=XXX.YYY2",
                                       "--expect-path=" + symlink.GetPath()}),
            0);

  HostInfo::Terminate();
  FileSystem::Terminate();
}

TEST(XXX, YYY3) {
  FileSystem::Initialize();
  HostInfo::Initialize();
  if (!expect_path.empty()) {
    // In subprocess.
    EXPECT_THAT_ERROR(
        llvm::errorCodeToError(llvm::sys::fs::set_current_path(chdir_to)),
        llvm::Succeeded());
    EXPECT_EQ(HostInfo::GetProgramFileSpec().GetPath(), expect_path.getValue());
    exit(HasFailure() ? 1 : 0);
  }

  llvm::SmallString<0> temp_dir;
  ASSERT_THAT_ERROR(llvm::errorCodeToError(FileSystem::Instance().GetRealPath(HostInfo::GetProcessTempDir().GetPath(), temp_dir)), llvm::Succeeded());
  FileSpec symlink = FileSpec(temp_dir).CopyByAppendingPathComponent("symlink");
  ASSERT_THAT_ERROR(FileSystem::Instance()
                        .Symlink(symlink, HostInfo::GetProgramFileSpec())
                        .takeError(),
                    llvm::Succeeded());

  llvm::SmallString<0> cwd;
  llvm::sys::fs::current_path(cwd);
  ASSERT_THAT_ERROR(llvm::errorCodeToError(
                        llvm::sys::fs::set_current_path(temp_dir)),
                    llvm::Succeeded());
  auto reset_cwd =
      llvm::make_scope_exit([&] { llvm::sys::fs::set_current_path(cwd); });

  EXPECT_EQ(llvm::sys::ExecuteAndWait("./symlink",
                                      {"argv0", "--gtest_filter=XXX.YYY3",
                                       "--expect-path=" + symlink.GetPath(),
                                       ("--chdir-to=" + cwd).str()}),
            0);

  HostInfo::Terminate();
  FileSystem::Terminate();
}

TEST(XXX, YYY4) {
  FileSystem::Initialize();
  if (!expect_path.empty()) {
    // In subprocess.
    EXPECT_THAT_ERROR(
        llvm::errorCodeToError(llvm::sys::fs::set_current_path(chdir_to)),
        llvm::Succeeded());
    HostInfo::Initialize();
    llvm::SmallString<0> real_path;
    ASSERT_THAT_ERROR(llvm::errorCodeToError(FileSystem::Instance().GetRealPath(
                          HostInfo::GetProgramFileSpec().GetPath(), real_path)),
                      llvm::Succeeded());
    EXPECT_EQ(real_path, expect_path.getValue());
    exit(HasFailure() ? 1 : 0);
  }

  HostInfo::Initialize();

  FileSpec temp_dir = HostInfo::GetProcessTempDir();
  ASSERT_TRUE(temp_dir);
  FileSpec symlink = temp_dir.CopyByAppendingPathComponent("symlink");
  ASSERT_THAT_ERROR(FileSystem::Instance()
                        .Symlink(symlink, HostInfo::GetProgramFileSpec())
                        .takeError(),
                    llvm::Succeeded());

  llvm::SmallString<0> real_path;
  ASSERT_THAT_ERROR(llvm::errorCodeToError(FileSystem::Instance().GetRealPath(
                        symlink.GetPath(), real_path)),
                    llvm::Succeeded());

  llvm::SmallString<0> cwd;
  llvm::sys::fs::current_path(cwd);
  ASSERT_THAT_ERROR(llvm::errorCodeToError(
                        llvm::sys::fs::set_current_path(temp_dir.GetPath())),
                    llvm::Succeeded());
  auto reset_cwd =
      llvm::make_scope_exit([&] { llvm::sys::fs::set_current_path(cwd); });

  EXPECT_EQ(llvm::sys::ExecuteAndWait("./symlink",
                                      {"argv0", "--gtest_filter=XXX.YYY4",
                                       ("--expect-path=" + real_path).str(),
                                       ("--chdir-to=" + cwd).str()}),
            0);

  HostInfo::Terminate();
  FileSystem::Terminate();
}
