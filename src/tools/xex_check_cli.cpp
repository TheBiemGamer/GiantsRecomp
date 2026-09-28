#include "tools/xex_check_exit_code.h"
#include "xex_verify.h"

#include <cstdio>
#include <filesystem>

#ifndef GIANTRECOMP_XEX_SHA256
#error "GIANTRECOMP_XEX_SHA256 must be defined by the build (see CMakeLists.txt)"
#endif

namespace {

int RunCheck(const std::filesystem::path& xex) {
  const auto result = giantrecomp::VerifyXex(xex, GIANTRECOMP_XEX_SHA256);
  const auto problem = giantrecomp::DescribeXexProblem(xex, result, GIANTRECOMP_XEX_SHA256);

  if (problem.empty()) {
    std::puts("OK");
    return giantrecomp::kExitMatch;
  }
  std::fputs(problem.c_str(), stderr);
  std::fputc('\n', stderr);
  return giantrecomp::ExitCodeForStatus(result.status);
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
                  argc > 0 ? argv[0] : L"giantrecomp_xexcheck");
    return giantrecomp::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#else

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <path-to-default.xex>\n",
                 argc > 0 ? argv[0] : "giantrecomp_xexcheck");
    return giantrecomp::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#endif
