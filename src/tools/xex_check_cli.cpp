#include "tools/xex_check_exit_code.h"
#include "xex_verify.h"

#include <cstdio>
#include <filesystem>

#ifndef GIANTSRECOMP_XEX_SHA256
#error "GIANTSRECOMP_XEX_SHA256 must be defined by the build (see CMakeLists.txt)"
#endif

namespace {

int RunCheck(const std::filesystem::path& xex) {
  const auto result = giantsrecomp::VerifyXex(xex, GIANTSRECOMP_XEX_SHA256);
  const auto problem = giantsrecomp::DescribeXexProblem(xex, result, GIANTSRECOMP_XEX_SHA256);

  if (problem.empty()) {
    std::puts("OK");
    return giantsrecomp::kExitMatch;
  }
  std::fputs(problem.c_str(), stderr);
  std::fputc('\n', stderr);
  return giantsrecomp::ExitCodeForStatus(result.status);
}

}  // namespace

#ifdef _WIN32

// A plain main(argc, char** argv) receives argv narrowed to the system code page on Windows, so any
// path character outside it (e.g. a non-ASCII Windows username under a user's own AppData path)
// silently becomes '?' and the file is reported as unreadable. wmain gets the real Unicode argv,
// and std::filesystem::path's wstring constructor preserves it exactly -- no narrowing anywhere.
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    std::fwprintf(stderr, L"usage: %s <path-to-default.xex>\n",
                  argc > 0 ? argv[0] : L"giantsrecomp_xexcheck");
    return giantsrecomp::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#else

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <path-to-default.xex>\n",
                 argc > 0 ? argv[0] : "giantsrecomp_xexcheck");
    return giantsrecomp::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#endif
