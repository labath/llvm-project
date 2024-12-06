//===-- NameToDIE.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NameToDIE.h"
#include "DWARFUnit.h"
#include "lldb/Core/DataFileCache.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

void NameToDIE::Insert(llvm::StringRef name, const DIERef &die_ref) {
  m_map[name].push_back(die_ref);
}

bool NameToDIE::Find(llvm::StringRef name,
                     llvm::function_ref<bool(DIERef ref)> callback) const {
  auto it = m_map.find(name);
  if (it == m_map.end())
    return true;
  for (const auto &ref : it->second)
    if (!callback(ref))
      return false;
  return true;
}

bool NameToDIE::Find(const RegularExpression &regex,
                     llvm::function_ref<bool(DIERef ref)> callback) const {
  for (const auto &[name, refs] : m_map)
    if (regex.Execute(name)) {
      for (const auto &ref : refs)
        if (!callback(ref))
          return false;
    }
  return true;
}

void NameToDIE::FindAllEntriesForUnit(
    DWARFUnit &s_unit, llvm::function_ref<bool(DIERef ref)> callback) const {
  const DWARFUnit &ns_unit = s_unit.GetNonSkeletonUnit();
  for (const auto &[_, refs] : m_map) {
    for (const auto &die_ref : refs) {
      if (
          ns_unit.GetDebugSection() == die_ref.section() &&ns_unit.GetSymbolFileDWARF().GetFileIndex() == die_ref.file_index() &&
          ns_unit.GetOffset() <= die_ref.die_offset() &&
          die_ref.die_offset() < ns_unit.GetNextUnitOffset()) {
        if (!callback(die_ref))
          return;
      }
    }
  }
}

void NameToDIE::Dump(Stream *s) {
  for (const auto &[name, refs] : m_map)
    for (const auto &ref : refs)
      s->Format("{0} \"{1}\"\n", name, ref);
}

void NameToDIE::ForEach(
    llvm::function_ref<bool(llvm::StringRef name, const DIERef &die_ref)>
        callback) const {
  for (const auto &[name, refs] : m_map)
    for (const auto &ref : refs)
      if (!callback(name, ref))
        return;
}

void NameToDIE::Append(const NameToDIEX &other) {
  const char *prev_name = nullptr;
  auto it = m_map.end();
  for (const auto &[name, ref] : other.m_map) {
    if (name == prev_name) {
      it->second.push_back(ref);
    } else {
      prev_name = name;
      it = m_map.try_emplace(name).first;
      it->second.push_back(ref);
    }
  }
}

bool NameToDIE::Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
                       const StringTableReader &strtab) {
  m_map.clear();
  return true;
}

void NameToDIE::Encode(DataEncoder &encoder, ConstStringTable &strtab) const {
}

bool NameToDIE::operator==(const NameToDIE &rhs) const {
  return m_map == rhs.m_map;
}
