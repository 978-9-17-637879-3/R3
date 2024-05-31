#include <algorithm>
#include <cassert>
#include <filesystem>
#include <getopt.h>
#include <ios>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

struct R3Options {
  bool verbose = false;
  bool dryRun = false;

  fs::path rootSearchDirectory;
  std::string find;
  std::string replace;
};

[[nodiscard]] R3Options readOptions(int argc, char **argv) {
  R3Options options;

  opterr = false; // Let us handle all error output for command line options
  int choice;
  int index = 0;
  std::vector<option> long_options = {
      {"dry-run", no_argument, nullptr, 'y'},
      {"verbose", no_argument, nullptr, 'v'},
      {"dir", required_argument, nullptr, 'd'},
      {"find", required_argument, nullptr, 'f'},
      {"replace", required_argument, nullptr, 'r'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, '\0'},
  };

  std::unordered_set<std::string> requiredOptionsStillNeeded = {"dir", "find",
                                                                "replace"};

  while ((choice = getopt_long(argc, argv, "yvd:f:r:h", long_options.data(),
                               &index)) != -1) {
    auto optionIterator =
        std::find_if(long_options.begin(), long_options.end(),
                     [choice](const option &opt) { return opt.val == choice; });

    if (optionIterator != long_options.end()) {
      requiredOptionsStillNeeded.erase(optionIterator->name);
    }

    switch (choice) {
    case 'h':
      exit(0);
    case 'y':
      options.dryRun = true;
      break;
    case 'v':
      options.verbose = true;
      break;
    case 'd':
      options.rootSearchDirectory = optarg;
      break;
    case 'f':
      options.find = optarg;
      break;
    case 'r':
      options.replace = optarg;
      break;
    default:
      throw std::runtime_error("Error: invalid option");
    }
  }

  if (!requiredOptionsStillNeeded.empty()) {
    throw std::runtime_error("Option <" + *requiredOptionsStillNeeded.begin() +
                             "> must be supplied!");
  }

  return options;
}

int main(int argc, char **argv) {
  std::ios_base::sync_with_stdio(false);

  R3Options options = readOptions(argc, argv);

  // Root serach directory that doesn't end with / should have
  // a trailing slash added, which will prevent it from being renamed.
  auto dirPath = std::string_view(options.rootSearchDirectory.c_str());
  if (dirPath.back() != '/') {
    options.rootSearchDirectory =
        fs::path(std::string(options.rootSearchDirectory) + "/");
  }

  if (!fs::is_directory(options.rootSearchDirectory)) {
    std::cerr << "<dir> must be a directory!" << std::endl;
    return 1;
  }

  if (options.find.empty()) {
    std::cerr << "<find> must not be empty!" << std::endl;
    return 1;
  }

  std::regex findRegex(options.find, std::regex_constants::ECMAScript);

  std::vector<std::pair<fs::path, fs::path>> pathsToRename;
  std::deque<fs::path> search;

  search.push_back(options.rootSearchDirectory);
  while (!search.empty()) {
    if (options.verbose) {
      std::cout << "Searching " << search.size() << " inodes... Matched "
                << pathsToRename.size() << "...\n";
    }

    auto path = search.front();
    search.pop_front();

    if (std::regex_search(path.filename().c_str(), findRegex)) {
      auto renamedPath = path;
      renamedPath.replace_filename(std::regex_replace(
          path.filename().c_str(), findRegex, options.replace));

      pathsToRename.emplace_back(path, std::move(renamedPath));
    }

    if (fs::is_directory(path)) {
      for (auto const &child : fs::directory_iterator(path)) {
        search.push_back(child);
      }
    }
  }

  for (auto pathIt = pathsToRename.rbegin(); pathIt != pathsToRename.rend();
       ++pathIt) {
    auto const &[path, renamedPath] = *pathIt;

    if (options.verbose) {
      std::cout << path << ' ' << renamedPath << '\n';
    }
  }

  std::cout << "Matched " << pathsToRename.size() << " inodes.\n";

  if (options.dryRun) {
    return 0;
  }

  if (!pathsToRename.empty()) {
    std::cout << "Dry run is not enabled. Are you sure you want to procede?\n";
    char prompt = '\0';
    while (prompt != 'Y' && prompt != 'N') {
      std::cout << "[Y/N] ";
      std::cout.flush();
      std::cin >> prompt;
    }

    if (prompt == 'N') {
      return 0;
    }
  }

  for (auto pathIt = pathsToRename.rbegin(); pathIt != pathsToRename.rend();
       ++pathIt) {
    auto const &[path, renamedPath] = *pathIt;
    fs::rename(path, renamedPath);
  }
}