#include "xex_verify.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using giantrecomp::VerifyXex;
using giantrecomp::XexCheck;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static const char kAbc[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static const char kEmpty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
static const char kMillionA[] = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

static fs::path WriteTemp(const char* name, const std::string& bytes) {
  fs::path p = fs::temp_directory_path() / name;
  std::ofstream(p, std::ios::binary) << bytes;
  return p;
}

int main() {
  // Known SHA-256 vectors.
  CHECK(giantrecomp::Sha256File(WriteTemp("gr_abc.bin", "abc")) == std::string(kAbc));
  CHECK(giantrecomp::Sha256File(WriteTemp("gr_empty.bin", "")) == std::string(kEmpty));
  // 1,000,000 bytes spans many read chunks.
  CHECK(giantrecomp::Sha256File(WriteTemp("gr_million.bin", std::string(1000000, 'a'))) ==
        std::string(kMillionA));

  // Unreadable inputs are nullopt, never a hash.
  CHECK(!giantrecomp::Sha256File(fs::temp_directory_path() / "gr_does_not_exist.bin"));
  CHECK(!giantrecomp::Sha256File(fs::temp_directory_path()));  // a directory

  // Paths with spaces and non-ASCII characters.
  fs::path odd = fs::temp_directory_path() / u8"gr odd éè dir";
  fs::create_directories(odd);
  {
    std::ofstream(odd / u8"défault.xex", std::ios::binary) << "abc";
  }
  CHECK(giantrecomp::Sha256File(odd / u8"défault.xex") == std::string(kAbc));

  // VerifyXex: match, case and whitespace tolerant.
  fs::path abc = WriteTemp("gr_abc2.bin", "abc");
  CHECK(VerifyXex(abc, kAbc).status == XexCheck::Match);
  CHECK(VerifyXex(abc, std::string("  ") +
                           "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD" + "\n")
            .status == XexCheck::Match);

  // Mismatch reports the actual hash; an empty or truncated file is a mismatch, not a match.
  auto wrong = VerifyXex(abc, kEmpty);
  CHECK(wrong.status == XexCheck::Mismatch);
  CHECK(wrong.actual_sha256 == std::string(kAbc));
  CHECK(VerifyXex(WriteTemp("gr_empty2.bin", ""), kAbc).status == XexCheck::Mismatch);
  CHECK(VerifyXex(WriteTemp("gr_trunc.bin", "ab"), kAbc).status == XexCheck::Mismatch);

  // Missing file and bad expected hash.
  CHECK(VerifyXex(fs::temp_directory_path() / "gr_missing.xex", kAbc).status ==
        XexCheck::Unreadable);
  CHECK(VerifyXex(abc, "xyz").status == XexCheck::BadExpected);
  CHECK(VerifyXex(abc, "").status == XexCheck::BadExpected);
  CHECK(VerifyXex(abc, std::string(64, 'g')).status == XexCheck::BadExpected);

  if (g_failures == 0) std::puts("all xex_verify tests passed");
  return g_failures == 0 ? 0 : 1;
}
