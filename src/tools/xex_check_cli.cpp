#include "tools/xex_check_exit_code.h"
#include "xex_verify.h"

#include <cstdio>
#include <filesystem>

#ifndef GIANTRECOMP_XEX_SHA256
#error "GIANTRECOMP_XEX_SHA256 must be defined by the build (see CMakeLists.txt)"
#endif

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <path-to-default.xex>\n",
                 argc > 0 ? argv[0] : "giantrecomp_xexcheck");
    return giantrecomp::kExitBadExpected;
  }

  const std::filesystem::path xex(argv[1]);
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
