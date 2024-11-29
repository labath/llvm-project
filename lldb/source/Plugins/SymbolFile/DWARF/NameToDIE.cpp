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

void NameToDIE::Finalize() { llvm::sort(m_map, &NameToDIE::Compare); }

void NameToDIE::Insert(llvm::StringRef name, const DIERef &die_ref) {
  m_map.emplace_back(name, die_ref);
}

bool NameToDIE::Find(llvm::StringRef name,
                     llvm::function_ref<bool(DIERef ref)> callback) const {
  for (const auto &[_, ref] : llvm::make_range(
           std::equal_range(m_map.begin(), m_map.end(), Pair(name, DIERef(0)),
                            &NameToDIE::Compare)))
    if (!callback(ref))
      return false;
  return true;
}

bool NameToDIE::Find(const RegularExpression &regex,
                     llvm::function_ref<bool(DIERef ref)> callback) const {
  for (const auto &[name, ref] : m_map)
    if (regex.Execute(name)) {
      if (!callback(ref))
        return false;
    }
  return true;
}

void NameToDIE::FindAllEntriesForUnit(
    DWARFUnit &s_unit, llvm::function_ref<bool(DIERef ref)> callback) const {
  const DWARFUnit &ns_unit = s_unit.GetNonSkeletonUnit();
  for (const auto &[_, die_ref] : m_map) {
    if (ns_unit.GetSymbolFileDWARF().GetFileIndex() == die_ref.file_index() &&
        ns_unit.GetDebugSection() == die_ref.section() &&
        ns_unit.GetOffset() <= die_ref.die_offset() &&
        die_ref.die_offset() < ns_unit.GetNextUnitOffset()) {
      if (!callback(die_ref))
        return;
    }
  }
}

void NameToDIE::Dump(Stream *s) {
  for (const auto &[name, ref] : m_map)
    s->Format("{0} \"{1}\"\n", name, ref);
}

void NameToDIE::ForEach(
    llvm::function_ref<bool(llvm::StringRef name, const DIERef &die_ref)>
        callback) const {
  for (const auto &[name, ref] : m_map)
    if (!callback(name, ref))
      break;
}

void NameToDIE::Append(const NameToDIE &other) {
  m_map.insert(m_map.end(), other.m_map.begin(), other.m_map.end());
}

constexpr llvm::StringLiteral kIdentifierNameToDIE("N2DI");

bool NameToDIE::Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
                       const StringTableReader &strtab) {
  m_map.clear();
  llvm::StringRef identifier((const char *)data.GetData(offset_ptr, 4), 4);
  if (identifier != kIdentifierNameToDIE)
    return false;
  const uint32_t count = data.GetU32(offset_ptr);
  m_map.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    llvm::StringRef str(strtab.Get(data.GetU32(offset_ptr)));
    // No empty strings allowed in the name to DIE maps.
    if (str.empty())
      return false;
    if (std::optional<DIERef> die_ref = DIERef::Decode(data, offset_ptr))
      m_map.emplace_back(str, *die_ref);
    else
      return false;
  }
  // We must sort the UniqueCStringMap after decoding it since it is a vector
  // of UniqueCStringMap::Entry objects which contain a ConstString and type T.
  // ConstString objects are sorted by "const char *" and then type T and
  // the "const char *" are point values that will depend on the order in which
  // ConstString objects are created and in which of the 256 string pools they
  // are created in. So after we decode all of the entries, we must sort the
  // name map to ensure name lookups succeed. If we encode and decode within
  // the same process we wouldn't need to sort, so unit testing didn't catch
  // this issue when first checked in.
  Finalize();
  return true;
}

void NameToDIE::Encode(DataEncoder &encoder, ConstStringTable &strtab) const {
  encoder.AppendData(kIdentifierNameToDIE);
  encoder.AppendU32(m_map.size());
  for (const auto &[name, ref] : m_map) {
    // Make sure there are no empty strings.
    assert(!name.empty());
    encoder.AppendU32(strtab.Add(ConstString(name)));
    ref.Encode(encoder);
  }
}

bool NameToDIE::operator==(const NameToDIE &rhs) const {
  return m_map == rhs.m_map;
}
