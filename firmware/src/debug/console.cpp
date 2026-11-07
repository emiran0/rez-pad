#include "console.h"

#include <string.h>

namespace rp {
namespace console {

namespace {

constexpr size_t kLineMax = 96;
constexpr int kArgMax = 8;

const Command* gCommands = nullptr;
size_t gCount = 0;
char gLine[kLineMax];
size_t gLen = 0;
bool gOverflow = false;

void printHelp(Print& out) {
  out.println("commands:");
  out.println("  help");
  for (size_t i = 0; i < gCount; ++i) {
    out.printf("  %s %s\n", gCommands[i].name, gCommands[i].usage);
  }
}

void run(char* line) {
  char* argv[kArgMax];
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < kArgMax; tok = strtok(nullptr, " \t")) {
    argv[argc++] = tok;
  }
  if (argc == 0) return;
  if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
    printHelp(Serial);
    return;
  }
  for (size_t i = 0; i < gCount; ++i) {
    if (strcmp(argv[0], gCommands[i].name) == 0) {
      gCommands[i].fn(argc, argv, Serial);
      return;
    }
  }
  Serial.printf("unknown command '%s', type help\n", argv[0]);
}

}  // namespace

void begin(const Command* commands, size_t count) {
  gCommands = commands;
  gCount = count;
}

void poll() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (gOverflow) {
        Serial.println("line too long, ignored");
      } else if (gLen > 0) {
        gLine[gLen] = '\0';
        Serial.printf("> %s\n", gLine);
        run(gLine);
      }
      gLen = 0;
      gOverflow = false;
    } else if (gLen < kLineMax - 1) {
      gLine[gLen++] = static_cast<char>(c);
    } else {
      gOverflow = true;
    }
  }
}

}  // namespace console
}  // namespace rp
