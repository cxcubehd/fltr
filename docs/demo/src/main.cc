#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/app.hh"

namespace {

void usage() {
  std::printf(
      "drift -- the fltr walkthrough demo\n"
      "\n"
      "  --smoke [frames]  run a scripted sequence, assert, and exit\n"
      "  --dump            print the last frame's display list\n"
      "  --shot PATH       write a PNG of the last frame\n"
      "  --size W H        window size (default 1280 720)\n");
}

}  // namespace

int main(int argc, char** argv) {
  demo::Options options;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--smoke") == 0) {
      options.smoke = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') options.smokeFrames = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--dump") == 0) {
      options.dump = true;
    } else if (std::strcmp(arg, "--shot") == 0 && i + 1 < argc) {
      options.shot = argv[++i];
    } else if (std::strcmp(arg, "--size") == 0 && i + 2 < argc) {
      options.width = std::atoi(argv[++i]);
      options.height = std::atoi(argv[++i]);
    } else {
      usage();
      return arg[0] == '-' ? 2 : 0;
    }
  }

  demo::App app(options);
  return app.run();
}
