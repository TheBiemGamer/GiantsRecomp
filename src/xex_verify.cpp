#include "xex_verify.h"

#include <windows.h>
#include <bcrypt.h>

#include <cctype>
#include <fstream>
#include <vector>

namespace giantrecomp {

namespace {

constexpr size_t kChunkBytes = 64 * 1024;

std::optional<std::string> NormalizeHash(std::string_view s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  s = s.substr(begin, end - begin);
  if (s.size() != 64) return std::nullopt;
  std::string out;
  out.reserve(64);
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace

std::optional<std::string> Sha256File(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;

  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
    return std::nullopt;
  }
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return std::nullopt;
  }

  bool ok = true;
  std::vector<char> buf(kChunkBytes);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize n = in.gcount();
    if (n > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()),
                                static_cast<ULONG>(n), 0) != 0) {
      ok = false;
      break;
    }
  }
  if (in.bad()) ok = false;

  UCHAR digest[32] = {};
  if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) ok = false;
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  if (!ok) return std::nullopt;

  static const char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (UCHAR b : digest) {
    hex.push_back(kHex[b >> 4]);
    hex.push_back(kHex[b & 0xF]);
  }
  return hex;
}

XexCheckResult VerifyXex(const std::filesystem::path& xex, std::string_view expected_sha256) {
  const auto expected = NormalizeHash(expected_sha256);
  if (!expected) return {XexCheck::BadExpected, {}};
  const auto actual = Sha256File(xex);
  if (!actual) return {XexCheck::Unreadable, {}};
  return {*actual == *expected ? XexCheck::Match : XexCheck::Mismatch, *actual};
}

std::string Utf8(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::wstring DescribeXexProblem(const std::filesystem::path& xex, const XexCheckResult& result,
                                std::string_view expected_sha256) {
  switch (result.status) {
    case XexCheck::Match:
      return {};
    case XexCheck::Unreadable:
      return L"Cannot read " + xex.wstring() +
             L"\nPut your extracted Skylanders Giants disc contents in the game folder "
             L"(default.xex must be at its top level).";
    case XexCheck::Mismatch:
      return L"default.xex is not the supported build.\nExpected SHA-256: " +
             std::wstring(expected_sha256.begin(), expected_sha256.end()) +
             L"\nFound SHA-256:    " +
             std::wstring(result.actual_sha256.begin(), result.actual_sha256.end());
    case XexCheck::BadExpected:
      break;
  }
  return L"Internal error: the pinned SHA-256 is malformed.";
}

}  // namespace giantrecomp
