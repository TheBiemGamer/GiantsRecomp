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

}  // namespace giantrecomp
