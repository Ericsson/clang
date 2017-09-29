//=--- LoadMetadata.h - Provides loading of metadata ---------------*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Provides functions for loading API metadata from YAML files for the api
// checkers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CHECKERS_LOADMETADATA_H
#define LLVM_CLANG_STATICANALYZER_CHECKERS_LOADMETADATA_H

#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/YAMLTraits.h"

namespace clang {
namespace ento {
namespace metadata {

enum class YamlReadError { Success, FileNotFound, ReadFail, VersionFail };

template <class T>
YamlReadError innerLoadYAMLData(const llvm::StringRef DataDirPath,
                                const llvm::StringRef DataFileName,
                                const llvm::StringRef DataFileVersion,
                                T &DataStructure) {
  if (DataDirPath.empty())
    return YamlReadError::FileNotFound;
  llvm::SmallString<512> DataFilePath(DataDirPath);
  llvm::sys::path::append(DataFilePath, DataFileName);
  // Try to open the data file.
  auto DataFile = llvm::MemoryBuffer::getFile(DataFilePath);
  if (DataFile.getError() == std::errc::no_such_file_or_directory)
    return YamlReadError::FileNotFound;
  if (!DataFile)
    return YamlReadError::ReadFail;

  // Check the version of the metadata file.
  // The version comment should match "# [A-Za-z]+ metadata format ([\d\.]+)"
  std::string Version;
  for (auto it = llvm::line_iterator(**DataFile), end = llvm::line_iterator();
       it != end; ++it) {
    llvm::StringRef line = it->ltrim();
    if (!line.startswith("# "))
      continue;
    std::string VersionPrefix("metadata format ");
    size_t pos = line.find(VersionPrefix, 2);
    if (pos == std::string::npos)
      continue;
    line = line.substr(pos + VersionPrefix.size());
    for (auto line_it = line.begin(), end = line.end();
         line_it != end && (isdigit(*line_it) || *line_it == '.'); ++line_it)
      Version += *line_it;
    break;
  }

  if (Version.empty())
    return YamlReadError::ReadFail;
  if (Version != DataFileVersion)
    return YamlReadError::VersionFail;

  llvm::yaml::Input yin((*DataFile)->getBuffer());
  yin >> DataStructure;
  return yin.error() ? YamlReadError::ReadFail : YamlReadError::Success;
}

/// Read the data in the given file using YAML traits.
///
/// The template parameter T must provide appropriate YAML traits, a default
/// constructor, an empty() method and a clear() method.
///
/// The file is expected to contain a version string of the form:
///   # [A-Za-z]+ metadata format <DataFileVersion>
///
/// Produces a warning message if the data was not read successfully.
template <class T>
void loadYAMLData(const llvm::StringRef DataDirPath,
                  const llvm::StringRef DataFileName,
                  const llvm::StringRef DataFileVersion,
                  const CheckName &CheckerName,
                  llvm::Optional<T> &DataStructure) {
  if (DataStructure.hasValue())
    return;
  DataStructure.emplace();

  YamlReadError EC = innerLoadYAMLData(
      DataDirPath, DataFileName, DataFileVersion, DataStructure.getValue());
  if (EC == YamlReadError::Success && !DataStructure->empty())
    return;
  // Clear any data that might have been loaded before an error.
  DataStructure->clear();

  if (EC == YamlReadError::VersionFail) {
    llvm::errs() << "warning: API data for " << CheckerName.getName()
                 << " is not compatable with the current version, "
                 << "skipping checks\n";
    return;
  }

  if (EC == YamlReadError::FileNotFound) {
    llvm::errs() << "warning: Could not find API data for "
                 << CheckerName.getName() << ", skipping checks\n";
    return;
  }

  llvm::errs() << "warning: Could not read API data for "
               << CheckerName.getName() << ", skipping checks\n";
}

} // end namespace metadata
} // end namespace ento
} // end namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CHECKERS_LOADMETADATA_H
