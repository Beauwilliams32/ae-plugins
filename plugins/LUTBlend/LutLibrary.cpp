// LutLibrary.cpp — implements the recovered .cube grammar.
//
// Every rule below is cited to the recovery note it came from. Where a rule was
// read off the shipped binary it says so; where this file had to make a decision
// the original did not pin down, it says that instead.

#include "LutLibrary.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>

namespace lutlib {
namespace {

// A row is keyed by its first token. Per the recovered dispatch these are the
// seven recognised directives and their exclusive arities, where arity counts the
// KEYWORD plus its values — which is how the original tests token counts
// (tokens.size() == 2, 3 or 4).
const char* kTitle = "TITLE";
const char* kSize3D = "LUT_3D_SIZE";
const char* kSize1D = "LUT_1D_SIZE";
const char* kDomainMin = "DOMAIN_MIN";
const char* kDomainMax = "DOMAIN_MAX";
const char* kRange3D = "LUT_3D_INPUT_RANGE";
const char* kRange1D = "LUT_1D_INPUT_RANGE";

std::string linePrefix(int lineNo) {
  std::ostringstream os;
  os << "line " << lineNo << ": ";
  return os.str();
}

// The recovered numeric acceptance test, all three parts:
//   strtod must consume something, must leave nothing trailing, and the result
//   must be finite. inf and NaN are refused rather than stored — one inf in a LUT
//   would poison every sample taken through it.
bool parseNumber(const std::string& tok, double* out) {
  if (tok.empty()) return false;
  errno = 0;
  const char* begin = tok.c_str();
  char* end = nullptr;
  const double v = std::strtod(begin, &end);
  if (end == begin) return false;          // nothing consumed
  if (end == nullptr || *end != '\0') return false;  // trailing garbage
  if (!std::isfinite(v)) return false;     // |v| < +inf
  *out = v;
  return true;
}

std::string lower(const std::string& s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return r;
}

bool endsWithCube(const std::string& s) {
  if (s.size() < 5) return false;
  return lower(s.substr(s.size() - 5)) == ".cube";
}

std::string basenameNoExt(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
  if (endsWithCube(base)) base = base.substr(0, base.size() - 5);
  return base;
}

}  // namespace

bool parseCubeText(const std::string& text, lutblend::CubeLUT* out, std::string* err) {
  if (!out) return false;

  // Built locally. Nothing is written through `out` until everything succeeded.
  lutblend::CubeLUT lut;
  std::vector<float> data;

  bool haveSize = false;
  bool is3D = true;
  int size = 0;
  double dmin[3] = {0.0, 0.0, 0.0};
  double dmax[3] = {1.0, 1.0, 1.0};

  std::istringstream file(text);
  std::string line;
  int lineNo = 0;

  while (std::getline(file, line)) {
    ++lineNo;

    // RECOVERED: the trailing CR is dropped before anything else, so CRLF files
    // work. The '#' comment is then stripped to end of line.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);

    // RECOVERED: whitespace-delimited tokens via stream extraction, so a
    // directive's values must share its line.
    std::vector<std::string> tok;
    {
      std::istringstream ls(line);
      std::string t;
      while (ls >> t) tok.push_back(t);
    }
    if (tok.empty()) continue;  // blank or comment-only: next line, no dispatch

    const std::string& kw = tok[0];

    // RECOVERED: TITLE is accepted and IGNORED. Its arm has no arity check and no
    // content validation, and branches straight to the next line.
    if (kw == kTitle) continue;

    if (kw == kSize3D || kw == kSize1D) {
      if (tok.size() != 2) {
        *err = linePrefix(lineNo) + kw + " needs exactly one value";
        return false;
      }
      // RECOVERED: a second size declaration is an error, not a last-wins
      // overwrite.
      if (haveSize) {
        *err = linePrefix(lineNo) + "duplicate LUT size declaration";
        return false;
      }
      double v = 0.0;
      if (!parseNumber(tok[1], &v)) {
        *err = linePrefix(lineNo) + kw + " has a non-numeric value";
        return false;
      }
      // RECOVERED: fcvtzs truncates, so 33.9 becomes 33. Reproduced deliberately,
      // truncation and all, because it is what the shipped plugin does.
      const int n = static_cast<int>(v);
      // RECOVERED: the bound is [2,256], tested as (unsigned)(n - 2) < 255. The
      // unsigned compare is also what rejects n < 2, for free.
      if (static_cast<unsigned>(n - 2) >= 255u) {
        *err = linePrefix(lineNo) + "LUT size " + std::to_string(n) +
               " out of range [2,256]";
        return false;
      }
      size = n;
      is3D = (kw == kSize3D);
      haveSize = true;
      continue;
    }

    if (kw == kDomainMin || kw == kDomainMax) {
      if (tok.size() != 4) {
        *err = linePrefix(lineNo) + kw + " needs 3 values";
        return false;
      }
      double* dst = (kw == kDomainMin) ? dmin : dmax;
      for (int i = 0; i < 3; ++i) {
        if (!parseNumber(tok[static_cast<size_t>(i) + 1], &dst[i])) {
          *err = linePrefix(lineNo) + kw + " has a non-numeric value";
          return false;
        }
      }
      continue;
    }

    if (kw == kRange3D || kw == kRange1D) {
      if (tok.size() != 3) {
        *err = linePrefix(lineNo) + kw + " needs 2 values";
        return false;
      }
      double lo = 0.0, hi = 0.0;
      if (!parseNumber(tok[1], &lo) || !parseNumber(tok[2], &hi)) {
        *err = linePrefix(lineNo) + kw + " has a non-numeric value";
        return false;
      }
      // An input range IS a domain, so it writes the same fields DOMAIN_MIN and
      // DOMAIN_MAX do. INFERRED, not read: the two directives share a handler in
      // the binary but how their writes interleave is one of the open items in
      // RECOVERY-NOTES.md. Writing the domain is the only coherent reading, since
      // the plugin has nowhere else to keep an input range.
      for (int i = 0; i < 3; ++i) { dmin[i] = lo; dmax[i] = hi; }
      continue;
    }

    // Not a directive, so this line must be a data row.
    double first = 0.0;
    if (!parseNumber(kw, &first)) {
      // RECOVERED message: a first token that is neither a keyword nor a number.
      *err = linePrefix(lineNo) + "unrecognised keyword or malformed data row '" +
             kw + "'";
      return false;
    }
    // RECOVERED: a data row before any size declaration is an error rather than
    // something to buffer and interpret later.
    if (!haveSize) {
      *err = linePrefix(lineNo) + "data before LUT_1D_SIZE/LUT_3D_SIZE";
      return false;
    }
    if (tok.size() != 3) {
      *err = linePrefix(lineNo) + "unrecognised keyword or malformed data row '" +
             kw + "'";
      return false;
    }

    double triple[3] = {first, 0.0, 0.0};
    for (int i = 1; i < 3; ++i) {
      if (!parseNumber(tok[static_cast<size_t>(i)], &triple[i])) {
        // RECOVERED message, distinct from the one above: the row WAS a data row
        // (its first token was numeric), but a later value is not.
        *err = linePrefix(lineNo) + "non-numeric data value '" +
               tok[static_cast<size_t>(i)] + "'";
        return false;
      }
    }
    for (int i = 0; i < 3; ++i) data.push_back(static_cast<float>(triple[i]));
  }

  // --- whole-file checks, which is why these two carry no "line N:" prefix ----

  // RECOVERED: data rows are COUNTED against the size, so a truncated or
  // over-long file is rejected rather than silently padded or ignored.
  if (!haveSize) {
    *err = "missing LUT_1D_SIZE/LUT_3D_SIZE";
    return false;
  }

  const size_t want = is3D
      ? 3u * static_cast<size_t>(size) * static_cast<size_t>(size) * static_cast<size_t>(size)
      : 3u * static_cast<size_t>(size);
  const size_t gotRows = data.size() / 3u;
  const size_t wantRows = want / 3u;
  if (data.size() != want) {
    *err = "expected " + std::to_string(wantRows) + " data rows, got " +
           std::to_string(gotRows);
    return false;
  }

  // RECOVERED: DOMAIN_MAX must STRICTLY exceed DOMAIN_MIN on each axis.
  for (int i = 0; i < 3; ++i) {
    if (!(dmax[i] > dmin[i])) {
      *err = "degenerate domain: DOMAIN_MAX must exceed DOMAIN_MIN";
      return false;
    }
  }

  lut.is3D = is3D;
  lut.size = size;
  lut.minR = static_cast<float>(dmin[0]);
  lut.minG = static_cast<float>(dmin[1]);
  lut.minB = static_cast<float>(dmin[2]);
  lut.maxR = static_cast<float>(dmax[0]);
  lut.maxG = static_cast<float>(dmax[1]);
  lut.maxB = static_cast<float>(dmax[2]);
  lut.data.swap(data);

  *out = lut;   // only now, and only on success
  return true;
}

std::shared_ptr<const lutblend::CubeLUT> loadCubeFile(const std::string& path,
                                                      std::string* err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (err) *err = "cannot open " + path;
    return nullptr;
  }
  std::ostringstream buf;
  buf << in.rdbuf();

  auto lut = std::make_shared<lutblend::CubeLUT>();
  if (!parseCubeText(buf.str(), lut.get(), err)) return nullptr;
  return lut;
}

std::string lutFolderPath() {
  const char* home = std::getenv("HOME");
  if (!home) return std::string();
  return std::string(home) + "/Documents/Williams Media LUTs";
}

std::vector<std::string> scanLutFolder() {
  std::vector<std::string> found;
  const std::string dir = lutFolderPath();
  if (dir.empty()) return found;

  DIR* d = opendir(dir.c_str());
  if (!d) return found;
  while (struct dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    if (!endsWithCube(name)) continue;
    found.push_back(dir + "/" + name);
  }
  closedir(d);
  std::sort(found.begin(), found.end());
  return found;
}

std::string popupChoices(const std::vector<std::string>& paths) {
  if (paths.empty()) return "None";
  std::string s;
  for (size_t i = 0; i < paths.size(); ++i) {
    if (i) s += "|";
    s += basenameNoExt(paths[i]);
  }
  return s;
}

std::shared_ptr<const lutblend::CubeLUT> Library::get(const std::string& path,
                                                      std::string* err) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = cache_.find(path);
  if (it != cache_.end()) return it->second;
  auto lut = loadCubeFile(path, err);
  if (lut) cache_[path] = lut;
  return lut;
}

void Library::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
}

size_t Library::cachedCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.size();
}

}  // namespace lutlib
