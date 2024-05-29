#include "Utils.h"

// Linux
#include <unistd.h>

std::string Utils::GetCurrentPath() {
  char curr_path[1024];
  getcwd(curr_path, 1024);
  sprintf(curr_path, "%s/", curr_path);
  std::string currentPath = curr_path;
  return currentPath;
}


