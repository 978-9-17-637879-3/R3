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

struct Options {
  int verbose = 0;
  int dryRun = 0;

  fs::path rootSearchDirectory;
  std::string find;
  std::string replace;
};

// TODO: Refactor.
[[nodiscard]] Options readOptions(int argc, char **argv) {
  Options options;

  int flagChar;
  option long_options[] = {/* These options set a flag. */
                           {"verbose", no_argument, &options.verbose, 1},
                           {"dry-run", no_argument, &options.dryRun, 1},
                           /* These options don’t set a flag.
                              We distinguish them by their indices. */
                           {"dir", required_argument, nullptr, 'd'},
                           {"find", required_argument, nullptr, 'f'},
                           {"replace", required_argument, nullptr, 'r'},
                           {nullptr, 0, nullptr, 0}};
  std::unordered_set<int> seenFlags;
  while (true) {

    /* getopt_long stores the option index here. */
    int option_index = 0;

    flagChar = getopt_long(argc, argv, "d:f:r:", long_options, &option_index);

    /* Detect the end of the options. */
    if (flagChar == -1)
      break;

    switch (flagChar) {
    case 0:
      /* If this option sets a flag, do nothing else now. */
      if (long_options[option_index].flag != 0) {
        break;
      }
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

    case '?':
      /* getopt_long already printed an error message. */
      break;

    default:
      abort();
    }

    seenFlags.insert(flagChar);
  }

  for (auto &long_option : long_options) {
    if (long_option.flag == nullptr && long_option.val != 0 &&
        seenFlags.find(long_option.val) == seenFlags.end()) {
      throw std::runtime_error("Option <" + std::string(long_option.name) +
                               "> must be supplied!");
    }
  }

  return options;
}

int main(int argc, char **argv) {
  std::ios_base::sync_with_stdio(false);

  Options options = readOptions(argc, argv);

  // Path directory ending with / should be stripped of the trailing slash
  // so that they can have their basename read.
  // The root path "/" is an exception, because it does not have a name.
  auto dirPath = std::string_view(options.rootSearchDirectory.c_str());
  if (dirPath.back() == '/' && dirPath != "/") {
    std::string dirPathWithoutSlash(dirPath);
    dirPathWithoutSlash.pop_back();
    options.rootSearchDirectory = fs::path(std::move(dirPathWithoutSlash));
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
    auto path = search.front();
    search.pop_front();

    if (std::regex_search(path.filename().c_str(), findRegex)) {
      auto renamedPath = path;
      renamedPath.replace_filename(std::regex_replace(
          path.filename().c_str(), findRegex, options.replace));

      pathsToRename.emplace_back(path, std::move(renamedPath));
    }

    if (options.verbose) {
      std::cout << "Searching " << search.size() << " inodes... Matched "
                << pathsToRename.size() << "...\n";
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