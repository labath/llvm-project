//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Unit tests for realpath.
///
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/limits_macros.h"
#include "hdr/types/size_t.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/CPP/utility.h"
#include "src/__support/OSUtil/path.h"
#include "src/__support/c_string.h"
#include "src/__support/fixedvector.h"
#include "src/__support/integer_to_string.h"
#include "src/__support/libc_assert.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/fcntl/openat.h"
#include "src/stdlib/realpath.h"
#include "src/string/strdup.h"
#include "src/sys/random/getrandom.h"
#include "src/sys/stat/mkdirat.h"
#include "src/unistd/chdir.h"
#include "src/unistd/close.h"
#include "src/unistd/getcwd.h"
#include "src/unistd/getpid.h"
#include "src/unistd/symlinkat.h"
#include "src/unistd/unlinkat.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"

namespace cpp = LIBC_NAMESPACE::cpp;
namespace path = LIBC_NAMESPACE::path;
using LIBC_NAMESPACE::CString;
using LIBC_NAMESPACE::FixedVector;
using LIBC_NAMESPACE::IntegerToString;

using PathVector = FixedVector<char, PATH_MAX + 1>;

template <size_t CAPACITY>
const char *c_str(FixedVector<char, CAPACITY> &vec) {
  if (!vec.push_back('\0'))
    return nullptr;
  (void)vec.pop_back();
  return vec.data();
}

template <size_t CAPACITY>
const char *c_str(FixedVector<char, CAPACITY> &&vec) {
  return c_str(vec);
}

template <size_t CAPACITY>
cpp::string_view view(const FixedVector<char, CAPACITY> &vec) {
  return cpp::string_view(vec.data(), vec.size());
}
using LIBC_NAMESPACE::testing::ErrnoCheckingTest;
using LIBC_NAMESPACE::testing::tlog;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;

// This test assumes the following values, so fail early if they mismatch.
static_assert(path::SEPARATOR == '/');
static_assert(path::CURRENT_DIR_COMPONENT == ".");
static_assert(path::PARENT_DIR_COMPONENT == "..");

// Size of a path separator.
constexpr size_t PATH_SEP_SIZE = 1;

// Creates the given directory if it does not exist. Returns zero on success.
[[nodiscard]] int ensure_directory_exists(int dirfd, const char *path,
                                          mode_t mode = 0755) {
  if (LIBC_NAMESPACE::mkdirat(dirfd, path, mode) == 0)
    return 0;
  if (libc_errno == EEXIST)
    libc_errno = 0;

  if (libc_errno != 0) {
    tlog << "Failed to create temp directory: " << path << "\n";
    return -1;
  }
  return 0;
}

// A test directory that removes itself on destruction.
class TestDir {
  // The test directory's absolute path.
  PathVector path;

  // File descriptor of the test directory.
  int fd = -1;

  // Files created in this directory tree.
  // Use a C string instead of cpp::string because FixedVector
  // only supports trivially destructable types.
  FixedVector<char *, 64> files;

  // Subdirectories created in this directory tree.
  FixedVector<char *, 64> dirs;

public:
  TestDir() {
    if (path.push_back('\0'))
      (void)path.pop_back();
  }

  // Initializes this TestDir container with the given path.
  void initialize(PathVector directory_path, int dirfd) {
    LIBC_ASSERT(this->fd == -1);
    this->path = directory_path;
    this->fd = dirfd;
    if (this->path.push_back('\0'))
      (void)this->path.pop_back();
  }

  ~TestDir() {
    if (path.empty())
      return;

    for (size_t i = 0; i < files.size(); i++) {
      LIBC_NAMESPACE::unlinkat(fd, files[i], 0);
      ::free(files[i]);
    }

    // Remove directories in reverse order so they'll be empty.
    for (size_t i = dirs.size(); i > 0; i--) {
      LIBC_NAMESPACE::unlinkat(fd, dirs[i - 1], AT_REMOVEDIR);
      ::free(dirs[i - 1]);
    }

    LIBC_NAMESPACE::close(fd);
    LIBC_NAMESPACE::unlinkat(AT_FDCWD, c_str(), AT_REMOVEDIR);
  }

  TestDir(TestDir &other) = delete;
  TestDir &operator=(TestDir &other) = delete;
  TestDir(TestDir &&other) = delete;
  TestDir &operator=(TestDir &&other) = delete;

  // Returns the absolute path of `relative_path` in this test directory.
  PathVector absolute_path(cpp::string_view relative_path) const {
    PathVector res = path;
    const char sep = '/';
    (void)res.insert(res.end(), &sep, &sep + 1);
    (void)res.insert(res.end(), relative_path.begin(), relative_path.end());
    if (res.push_back('\0'))
      (void)res.pop_back();
    return res;
  }

  // Returns this test directory path as a C string.
  const char *c_str() const { return path.data(); }

  // Returns this test directory path as a string view.
  const cpp::string_view view() const { return ::view(path); }

  // Creates a directory relative to TestDir. Returns zero on success.
  [[nodiscard]] int mkdir(const char *relative_path, mode_t mode = 0755) {
    char *path = LIBC_NAMESPACE::strdup(relative_path);
    if (path == nullptr)
      return -1;

    if (!dirs.push_back(path)) {
      tlog << "Not enough space in TestDir::dirs_\n";
      return -1;
    }

    return ensure_directory_exists(fd, relative_path, mode);
  }

  // Creates an empty file relative to TestDir. Returns zero on success.
  [[nodiscard]] int touch(const char *relative_path, mode_t mode = 0644) {
    char *path = LIBC_NAMESPACE::strdup(relative_path);
    if (path == nullptr)
      return -1;

    if (!files.push_back(path)) {
      tlog << "Not enough space in TestDir::files_\n";
      return -1;
    }
    int newfd =
        LIBC_NAMESPACE::openat(fd, relative_path, O_RDONLY | O_CREAT, mode);
    if (newfd < 0)
      return -1;
    return LIBC_NAMESPACE::close(newfd);
  }

  // Creates a symlink relative to TestDir. Returns zero on success.
  [[nodiscard]] int symlink(const char *target_path,
                            const char *relative_path) {
    char *path = LIBC_NAMESPACE::strdup(relative_path);
    if (path == nullptr)
      return -1;

    if (!files.push_back(path)) {
      tlog << "Not enough space in TestDir::files_\n";
      return -1;
    }

    return LIBC_NAMESPACE::symlinkat(target_path, fd, path);
  }
};

FixedVector<char, 64> unique_id() {
  FixedVector<char, 64> id;
  IntegerToString<pid_t> pid_str(LIBC_NAMESPACE::getpid());
  cpp::string_view pid_view = pid_str.view();
  (void)id.insert(id.end(), pid_view.begin(), pid_view.end());
  const char dot = '.';
  (void)id.insert(id.end(), &dot, &dot + 1);

  constexpr cpp::string_view alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
  uint8_t rand_bytes[16] = {};
  (void)LIBC_NAMESPACE::getrandom(rand_bytes, sizeof(rand_bytes), 0);
  for (size_t i = 0; i < sizeof(rand_bytes); i++)
    (void)id.push_back(alphabet[rand_bytes[i] % alphabet.size()]);

  return id;
}

class LlvmLibcRealpathTest : public ErrnoCheckingTest {
public:
  void SetUp() override {
    ErrnoCheckingTest::SetUp();

    (void)LIBC_NAMESPACE::getcwd(start_dir, sizeof(start_dir));
    ASSERT_ERRNO_SUCCESS();
  }

  void TearDown() override {
    ASSERT_THAT(LIBC_NAMESPACE::chdir(start_dir), Succeeds());

    ErrnoCheckingTest::TearDown();
  }

  char *realpath_buffered(const char *path) {
    return LIBC_NAMESPACE::realpath(path, realpath_buf);
  }

  template <size_t CAPACITY>
  char *realpath_buffered(FixedVector<char, CAPACITY> &path) {
    return realpath_buffered(c_str(path));
  }

  template <size_t CAPACITY>
  char *realpath_buffered(FixedVector<char, CAPACITY> &&path) {
    return realpath_buffered(c_str(path));
  }

  // Creates a test directory in dst. Returns true if successful.
  //
  // While we would prefer to return cpp::optional<TestDir> here,
  // LLVM-libc's optional expects types to be trivially destructible.
  [[nodiscard]] bool create_test_dir(const char *name, TestDir &dst) {
    CString test_dir = libc_make_test_file_path(".");
    char buf[PATH_MAX];
    // Some test environments will have symlinks in their test directory.
    // In order for tests to compute an expected canonical path,
    // the path returned by create_test_dir needs to be canonical itself.
    char *test_dir_abspath = LIBC_NAMESPACE::realpath(test_dir, buf);
    if (libc_errno != 0)
      return false;

    PathVector test_dir_path;
    cpp::string_view abspath_view(test_dir_abspath);
    if (!test_dir_path.insert(test_dir_path.end(), abspath_view.begin(),
                              abspath_view.end()))
      return false;

    constexpr cpp::string_view prefix = "/LlvmLibcRealpathTest.";
    if (!test_dir_path.insert(test_dir_path.end(), prefix.begin(),
                              prefix.end()))
      return false;

    cpp::string_view name_view(name);
    if (!test_dir_path.insert(test_dir_path.end(), name_view.begin(),
                              name_view.end()))
      return false;

    const char dot = '.';
    if (!test_dir_path.insert(test_dir_path.end(), &dot, &dot + 1))
      return false;

    // Include a unique ID in case multiple builds of this test run at once.
    auto uid = unique_id();
    if (!test_dir_path.insert(test_dir_path.end(), uid.begin(), uid.end()))
      return false;

    const char *test_dir_path_cstr = c_str(test_dir_path);
    if (test_dir_path_cstr == nullptr)
      return false;

    if (ensure_directory_exists(AT_FDCWD, test_dir_path_cstr))
      return false;
    int fd = LIBC_NAMESPACE::openat(AT_FDCWD, test_dir_path_cstr,
                                    O_RDONLY | O_DIRECTORY);
    if (fd < 0)
      return false;

    dst.initialize(cpp::move(test_dir_path), fd);
    return true;
  }

private:
  // Buffer for storing realpath output.
  char realpath_buf[PATH_MAX];

  // Current working dir at test SetUp.
  char start_dir[PATH_MAX];
};

TEST_F(LlvmLibcRealpathTest, ErrorsWithInvalidArgIfNullPath) {
  ASSERT_EQ(realpath_buffered(nullptr), nullptr);
  ASSERT_ERRNO_EQ(EINVAL);
}

TEST_F(LlvmLibcRealpathTest, ErrorsWithNoEntryIfEmptyPath) {
  ASSERT_EQ(realpath_buffered(""), nullptr);
  ASSERT_ERRNO_EQ(ENOENT);
}

TEST_F(LlvmLibcRealpathTest, OkIfPathArgIsExactlyMaxSize) {
  // PATH_MAX counts null terminator, so construct a path of size PATH_MAX-1.
  PathVector s(PATH_MAX - 1, '/');
  for (size_t i = 1; i < s.size(); i += 2)
    s[i] = '.';

  ASSERT_STREQ(realpath_buffered(s), "/");
}

// Creates a test directory that has an absolute path with exactly
// `desired_size` characters.
[[nodiscard]] bool create_absolute_path_with_size(TestDir &test_dir,
                                                  size_t desired_size,
                                                  PathVector &out) {
  if (desired_size < test_dir.view().size() + PATH_SEP_SIZE) {
    tlog << "Test directory is already too long in "
            "create_absolute_path_with_size: "
         << test_dir.view().size() << "\n";
    return false;
  }
  size_t remaining_size = desired_size - test_dir.view().size() - PATH_SEP_SIZE;

  PathVector relative_path;

  while (remaining_size != 0) {
    if (!relative_path.empty()) {
      if (!relative_path.push_back('/'))
        return false;
      remaining_size -= PATH_SEP_SIZE;
    }

    size_t component_size = NAME_MAX;
    if (component_size > remaining_size)
      component_size = remaining_size;

    // If adding a component of this size would leave us in a state where
    // we only have enough space for a separator, shorten the component.
    if (remaining_size - component_size == PATH_SEP_SIZE)
      component_size -= 1;

    for (size_t i = 0; i < component_size; ++i) {
      if (!relative_path.push_back('a'))
        return false;
    }
    remaining_size -= component_size;

    const char *rel_cstr = c_str(relative_path);
    if (rel_cstr == nullptr || test_dir.mkdir(rel_cstr))
      return false;
  }

  out = test_dir.absolute_path(view(relative_path));

  if (out.size() != desired_size) {
    tlog << "Failed to create path of size=" << desired_size << "\n";
    return false;
  }
  return true;
}

TEST_F(LlvmLibcRealpathTest, OkIfResolvedPathIsExactlyMaxSize) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("OkIfResolvedPathIsExactlyMaxSize", test_dir));

  PathVector path;
  ASSERT_TRUE(create_absolute_path_with_size(test_dir, PATH_MAX - 1, path));

  ASSERT_STREQ(realpath_buffered(path), c_str(path));
}

TEST_F(LlvmLibcRealpathTest, ErrorsWithNameTooLongIfPathArgExceedsMaxSize) {
  // PATH_MAX counts null terminator, so construct a path of size PATH_MAX.
  PathVector s(PATH_MAX, '/');
  for (size_t i = 1; i < s.size(); i += 2)
    s[i] = '.';

  ASSERT_EQ(realpath_buffered(s), nullptr);
  ASSERT_ERRNO_EQ(ENAMETOOLONG);
}

TEST_F(LlvmLibcRealpathTest, RootResolvesToRoot) {
  ASSERT_STREQ(realpath_buffered("/"), "/");
}

TEST_F(LlvmLibcRealpathTest, RootDotDotTraversalStaysAtRoot) {
  ASSERT_STREQ(realpath_buffered("/.."), "/");
}

TEST_F(LlvmLibcRealpathTest, SimpleAbsolutePath) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("SimpleAbsolutePath", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_THAT(test_dir.mkdir("a/b"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("a/b")),
               c_str(test_dir.absolute_path("a/b")));
}

TEST_F(LlvmLibcRealpathTest, DotDotTraversesParent) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("DotDotTraversesParent", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_THAT(test_dir.mkdir("a/b"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("a/b/..")),
               c_str(test_dir.absolute_path("a")));
}

TEST_F(LlvmLibcRealpathTest, DotTraversalIsNop) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("DotTraversalIsNop", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_THAT(test_dir.mkdir("a/b"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("a/b/./")),
               c_str(test_dir.absolute_path("a/b")));
}

TEST_F(LlvmLibcRealpathTest, ConsecutiveSeparatorsIgnored) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("ConsecutiveSeparatorsIgnored", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("a//..///a//")),
               c_str(test_dir.absolute_path("a")));
}

TEST_F(LlvmLibcRealpathTest, AllocatesResultWhenBufferIsNull) {
  char *result = LIBC_NAMESPACE::realpath("/", nullptr);
  ASSERT_STREQ(result, "/");
  ::free(result);
}

TEST_F(LlvmLibcRealpathTest, ErrorsWithNotDirWhenFileIsInPath) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("ErrorsWithNotDirWhenFileIsInPath", test_dir));

  ASSERT_THAT(test_dir.touch("file"), Succeeds());

  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("file/.")), nullptr);
  ASSERT_ERRNO_EQ(ENOTDIR);

  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("file/")), nullptr);
  ASSERT_ERRNO_EQ(ENOTDIR);
}

TEST_F(LlvmLibcRealpathTest, FileAtEndOfPathIsOk) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("FileAtEndOfPathIsOk", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_THAT(test_dir.touch("a/file"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("a/file")),
               c_str(test_dir.absolute_path("a/file")));
}

TEST_F(LlvmLibcRealpathTest, ErrorsWithNoEntWhenComponentDoesNotExist) {
  TestDir test_dir;
  ASSERT_TRUE(
      create_test_dir("ErrorsWithNoEntWhenComponentDoesNotExist", test_dir));

  // A missing directory should give ENOENT.
  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("a/b")), nullptr);
  ASSERT_ERRNO_EQ(ENOENT);

  // Should fail if the final component doesn't exist.
  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("a")), nullptr);
  ASSERT_ERRNO_EQ(ENOENT);
}

TEST_F(LlvmLibcRealpathTest, ErrorsWithNoAccessWhenDirectoryNotSearchable) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("ErrorsWithNoAccessWhenDirectoryNotSearchable",
                              test_dir));

  ASSERT_THAT(test_dir.mkdir("a", /* mode= */ 0644), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("a/b")), nullptr);
  ASSERT_ERRNO_EQ(EACCES);
}

TEST_F(LlvmLibcRealpathTest, RelativePathResolvesToCurrentWorkingDir) {
  TestDir test_dir;
  ASSERT_TRUE(
      create_test_dir("RelativePathResolvesToCurrentWorkingDir", test_dir));

  ASSERT_THAT(LIBC_NAMESPACE::chdir(test_dir.c_str()), Succeeds());
  ASSERT_STREQ(realpath_buffered("."), test_dir.c_str());

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_STREQ(realpath_buffered("a"), c_str(test_dir.absolute_path("a")));
}

// Creates a directory with the desired_size and then chdir's into it.
// Returns true on success.
[[nodiscard]] bool chdir_to_absolute_path_with_size(TestDir &test_dir,
                                                    size_t desired_size,
                                                    PathVector &out) {
  if (!create_absolute_path_with_size(test_dir, desired_size, out))
    return false;

  // Convert the directory to be relative to test_dir.
  const char *test_dir_relative_path =
      c_str(out) + test_dir.view().size() + PATH_SEP_SIZE;

  // Change directories into the path iteratively.
  // This allows us to chdir into paths longer than PATH_MAX, as long as each
  // of test_dir and test_dir_relative_path are shorter than PATH_MAX.
  if (LIBC_NAMESPACE::chdir(test_dir.c_str()))
    return false;
  if (LIBC_NAMESPACE::chdir(test_dir_relative_path))
    return false;
  return true;
}

TEST_F(LlvmLibcRealpathTest, RelativeRealpathAcceptsPathExactlyMaxSize) {
  TestDir test_dir;
  ASSERT_TRUE(
      create_test_dir("RelativeRealpathAcceptsPathExactlyMaxSize", test_dir));

  PathVector path;
  ASSERT_TRUE(chdir_to_absolute_path_with_size(test_dir, PATH_MAX - 1, path));

  ASSERT_STREQ(realpath_buffered("."), c_str(path));
}

TEST_F(LlvmLibcRealpathTest, RelativeRealpathRejectsPathExceedingMaxSize) {
  TestDir test_dir;
  ASSERT_TRUE(
      create_test_dir("RelativeRealpathRejectsPathExceedingMaxSize", test_dir));

  PathVector path;
  if (!chdir_to_absolute_path_with_size(test_dir, PATH_MAX, path)) {
    // Skip the test if the system didn't allow creating the path.
    ASSERT_ERRNO_EQ(ENAMETOOLONG);
    return;
  }

  ASSERT_EQ(realpath_buffered("."), nullptr);
  ASSERT_ERRNO_EQ(ENAMETOOLONG);
}

TEST_F(LlvmLibcRealpathTest, AbsoluteSymlinkResolves) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("AbsoluteSymlinkResolves", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_THAT(test_dir.touch("a/file"), Succeeds());

  PathVector absolute_target = test_dir.absolute_path("a/file");
  ASSERT_THAT(test_dir.symlink(c_str(absolute_target), "link"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("link")),
               c_str(absolute_target));
}

TEST_F(LlvmLibcRealpathTest, RelativeSymlinkResolves) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("RelativeSymlinkResolves", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_THAT(test_dir.touch("a/file"), Succeeds());

  ASSERT_THAT(test_dir.symlink("a/file", "link"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("link")),
               c_str(test_dir.absolute_path("a/file")));
}

TEST_F(LlvmLibcRealpathTest, SymlinkWithinDirectoryTraversalResolves) {
  TestDir test_dir;
  ASSERT_TRUE(
      create_test_dir("SymlinkWithinDirectoryTraversalResolves", test_dir));

  ASSERT_THAT(test_dir.mkdir("a"), Succeeds());
  ASSERT_THAT(test_dir.mkdir("a/b"), Succeeds());
  ASSERT_THAT(test_dir.touch("a/b/c"), Succeeds());
  ASSERT_THAT(test_dir.symlink("a/b", "link"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("link/c")),
               c_str(test_dir.absolute_path("a/b/c")));
}

TEST_F(LlvmLibcRealpathTest, MultipleSymlinkResolutions) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("MultipleSymlinkResolutions", test_dir));

  ASSERT_THAT(test_dir.symlink("b", "a"), Succeeds());
  ASSERT_THAT(test_dir.symlink("c", "b"), Succeeds());
  ASSERT_THAT(test_dir.symlink("d", "c"), Succeeds());
  ASSERT_THAT(test_dir.touch("d"), Succeeds());

  ASSERT_STREQ(realpath_buffered(test_dir.absolute_path("a")),
               c_str(test_dir.absolute_path("d")));
}

TEST_F(LlvmLibcRealpathTest, SymlinkLoop) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("SymlinkLoop", test_dir));

  ASSERT_THAT(test_dir.symlink("a", "b"), Succeeds());
  ASSERT_THAT(test_dir.symlink("b", "a"), Succeeds());

  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("a")), nullptr);
  ASSERT_ERRNO_EQ(ELOOP);
}

TEST_F(LlvmLibcRealpathTest, LongSymlinkErrorsWithNameTooLong) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("LongSymlinkErrorsWithNameTooLong", test_dir));

  PathVector target(PATH_MAX - 1, 'a');
  for (size_t i = 0; i < target.size(); i += NAME_MAX)
    target[i] = '/';

  ASSERT_THAT(test_dir.symlink(c_str(target), "link"), Succeeds());

  // The link resolves to a maximum length path,
  // so adding anything to the end means the intermediary path is too long.
  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("link/long")), nullptr);
  ASSERT_ERRNO_EQ(ENAMETOOLONG);
}

TEST_F(LlvmLibcRealpathTest, ErrorsWithNotDirWhenLinkTargetHasTrailingSep) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("ErrorsWithNotDirWhenLinkTargetHasTrailingSep",
                              test_dir));

  ASSERT_THAT(test_dir.touch("file"), Succeeds());
  // The trailing slash in the symlink target should be respected.
  ASSERT_THAT(test_dir.symlink("file/", "link"), Succeeds());

  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("link")), nullptr);
  ASSERT_ERRNO_EQ(ENOTDIR);
}

TEST_F(LlvmLibcRealpathTest, ErrorsWithNotDirWhenPathWithLinkHasTrailingSep) {
  TestDir test_dir;
  ASSERT_TRUE(create_test_dir("ErrorsWithNotDirWhenPathWithLinkHasTrailingSep",
                              test_dir));

  ASSERT_THAT(test_dir.touch("file"), Succeeds());
  ASSERT_THAT(test_dir.symlink("file", "link"), Succeeds());

  // The trailing slash in "link/" should be respected.
  ASSERT_EQ(realpath_buffered(test_dir.absolute_path("link/")), nullptr);
  ASSERT_ERRNO_EQ(ENOTDIR);
}
