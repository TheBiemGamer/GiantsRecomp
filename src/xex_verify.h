#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace giantsrecomp {

enum class XexCheck {
  Match,        // file hashes to the expected value
  Mismatch,     // file readable but hash differs (includes empty and truncated files)
  Unreadable,   // file missing, a directory, or a read error
  BadExpected,  // expected hash is not 64 hex characters
};

struct XexCheckResult {
  XexCheck status;
  std::string actual_sha256;  // empty unless the file was readable
};

// Lowercase hex SHA-256 of the file at `path`, or nullopt if it cannot be read completely.
std::optional<std::string> Sha256File(const std::filesystem::path& path);

// Compares the file's SHA-256 with `expected_sha256` (64 hex chars, any case, surrounding
// whitespace ignored).
XexCheckResult VerifyXex(const std::filesystem::path& xex, std::string_view expected_sha256);

// UTF-8 form of a path. Unlike path::string(), never throws for characters outside the ANSI code page.
std::string Utf8(const std::filesystem::path& p);

// Human-readable explanation for anything other than XexCheck::Match; empty for a match. UTF-8, so
// a game folder with any Unicode characters is shown exactly as it was tried (see Utf8()).
std::string DescribeXexProblem(const std::filesystem::path& xex, const XexCheckResult& result,
                               std::string_view expected_sha256);

}  // namespace giantsrecomp
