//===-- HostInfoLinux.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/linux/HostInfoLinux.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/auxv.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <optional>

using namespace lldb_private;

namespace {
struct HostInfoLinuxFields {
  llvm::SmallString<0> likely_startup_cwd;

  llvm::once_flag m_distribution_once_flag;
  std::string m_distribution_id;

  llvm::once_flag program_filespec_flag;
  FileSpec program_filespec;
};
} // namespace

static HostInfoLinuxFields *g_fields = nullptr;

void HostInfoLinux::Initialize(SharedLibraryDirectoryHelper *helper) {
  HostInfoPosix::Initialize(helper);

  g_fields = new HostInfoLinuxFields();
  llvm::sys::fs::current_path(g_fields->likely_startup_cwd);
}

void HostInfoLinux::Terminate() {
  assert(g_fields && "Missing call to Initialize?");
  delete g_fields;
  g_fields = nullptr;
  HostInfoBase::Terminate();
}

llvm::StringRef HostInfoLinux::GetDistributionId() {
  assert(g_fields && "Missing call to Initialize?");
  // Try to run 'lbs_release -i', and use that response for the distribution
  // id.
  llvm::call_once(g_fields->m_distribution_once_flag, []() {
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOGF(log, "attempting to determine Linux distribution...");

    // check if the lsb_release command exists at one of the following paths
    const char *const exe_paths[] = {"/bin/lsb_release",
                                     "/usr/bin/lsb_release"};

    for (size_t exe_index = 0;
         exe_index < sizeof(exe_paths) / sizeof(exe_paths[0]); ++exe_index) {
      const char *const get_distribution_info_exe = exe_paths[exe_index];
      if (access(get_distribution_info_exe, F_OK)) {
        // this exe doesn't exist, move on to next exe
        LLDB_LOGF(log, "executable doesn't exist: %s",
                  get_distribution_info_exe);
        continue;
      }

      // execute the distribution-retrieval command, read output
      std::string get_distribution_id_command(get_distribution_info_exe);
      get_distribution_id_command += " -i";

      FILE *file = popen(get_distribution_id_command.c_str(), "r");
      if (!file) {
        LLDB_LOGF(log,
                  "failed to run command: \"%s\", cannot retrieve "
                  "platform information",
                  get_distribution_id_command.c_str());
        break;
      }

      // retrieve the distribution id string.
      char distribution_id[256] = {'\0'};
      if (fgets(distribution_id, sizeof(distribution_id) - 1, file) !=
          nullptr) {
        LLDB_LOGF(log, "distribution id command returned \"%s\"",
                  distribution_id);

        const char *const distributor_id_key = "Distributor ID:\t";
        if (strstr(distribution_id, distributor_id_key)) {
          // strip newlines
          std::string id_string(distribution_id + strlen(distributor_id_key));
          llvm::erase(id_string, '\n');

          // lower case it and convert whitespace to underscores
          std::transform(
              id_string.begin(), id_string.end(), id_string.begin(),
              [](char ch) { return tolower(isspace(ch) ? '_' : ch); });

          g_fields->m_distribution_id = id_string;
          LLDB_LOGF(log, "distribution id set to \"%s\"",
                    g_fields->m_distribution_id.c_str());
        } else {
          LLDB_LOGF(log, "failed to find \"%s\" field in \"%s\"",
                    distributor_id_key, distribution_id);
        }
      } else {
        LLDB_LOGF(log,
                  "failed to retrieve distribution id, \"%s\" returned no"
                  " lines",
                  get_distribution_id_command.c_str());
      }

      // clean up the file
      pclose(file);
    }
  });

  return g_fields->m_distribution_id;
}

FileSpec HostInfoLinux::GetProgramFileSpec() {
  llvm::call_once(g_fields->program_filespec_flag, [] {
    FileSpec auxv_path;
    if (const char *execfn =
            reinterpret_cast<const char *>(getauxval(AT_EXECFN))) {
      auxv_path.SetFile(execfn, FileSpec::Style::native);
      auxv_path.MakeAbsolute(FileSpec(g_fields->likely_startup_cwd));
    }

    FileSpec proc_path;
    char storage[PATH_MAX];
    if (ssize_t len = readlink("/proc/self/exe", storage, sizeof(storage));
        len > 0) {
      proc_path.SetFile(llvm::StringRef(storage, len), FileSpec::Style::native);
    }

    if (auxv_path && proc_path) {
      g_fields->program_filespec =
          llvm::sys::fs::equivalent(auxv_path.GetPath(), proc_path.GetPath())
              ? auxv_path
              : proc_path;
    } else {
      g_fields->program_filespec = auxv_path ? auxv_path : proc_path;
    }
  });
  return g_fields->program_filespec;
}

void HostInfoLinux::ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                                   ArchSpec &arch_64) {
  HostInfoPosix::ComputeHostArchitectureSupport(arch_32, arch_64);

  // On Linux, "unknown" in the vendor slot isn't what we want for the default
  // triple.  It's probably an artifact of config.guess.
  if (arch_32.IsValid()) {
    if (arch_32.GetTriple().getVendor() == llvm::Triple::UnknownVendor)
      arch_32.GetTriple().setVendorName(llvm::StringRef());
  }
  if (arch_64.IsValid()) {
    if (arch_64.GetTriple().getVendor() == llvm::Triple::UnknownVendor)
      arch_64.GetTriple().setVendorName(llvm::StringRef());
  }
}
