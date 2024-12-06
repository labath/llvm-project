//===-- NameToDIE.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_NAMETODIE_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_NAMETODIE_H

#include "DIERef.h"
#include "llvm/ADT/DenseMap.h"
#include <cstddef>
#include <functional>
#include <vector>

namespace lldb_private::plugin {
namespace dwarf {
class DWARFUnit;

class NameToDIEX {
public:
  void Insert(const char *name, DIERef die_ref) {
    m_map.emplace_back(name, die_ref);
  }

  void Finalize() { llvm::sort(m_map, llvm::less_first()); }

  std::vector<std::pair<const char *, DIERef>> m_map;
};

class NameToDIE {
public:
  NameToDIE() : m_map() {}

  ~NameToDIE() = default;

  void Dump(Stream *s);

  void Insert(llvm::StringRef name, const DIERef &die_ref);

  void Append(const NameToDIEX &other);
  void Reserve(size_t count) { m_map.reserve(count); }

  bool Find(llvm::StringRef name,
            llvm::function_ref<bool(DIERef ref)> callback) const;

  bool Find(const RegularExpression &regex,
            llvm::function_ref<bool(DIERef ref)> callback) const;

  /// \a unit must be the skeleton unit if possible, not GetNonSkeletonUnit().
  void
  FindAllEntriesForUnit(DWARFUnit &unit,
                        llvm::function_ref<bool(DIERef ref)> callback) const;

  void
  ForEach(llvm::function_ref<bool(llvm::StringRef name, const DIERef &die_ref)>
              callback) const;

  /// Decode a serialized version of this object from data.
  ///
  /// \param data
  ///   The decoder object that references the serialized data.
  ///
  /// \param offset_ptr
  ///   A pointer that contains the offset from which the data will be decoded
  ///   from that gets updated as data gets decoded.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  bool Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
              const StringTableReader &strtab);

  /// Encode this object into a data encoder object.
  ///
  /// This allows this object to be serialized to disk.
  ///
  /// \param encoder
  ///   A data encoder object that serialized bytes will be encoded into.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  void Encode(DataEncoder &encoder, ConstStringTable &strtab) const;

  /// Used for unit testing the encoding and decoding.
  bool operator==(const NameToDIE &rhs) const;

  bool IsEmpty() const { return m_map.empty(); }

  void Clear() { m_map.clear(); }

protected:

  llvm::DenseMap<llvm::StringRef, llvm::SmallVector<DIERef, 2>> m_map;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_NAMETODIE_H
