// LutLibrary.h — LUT discovery and .cube loading for the rebuilt LUTBlend.
//
// PROVENANCE: this is NEW code, not recovered code. What it implements is the
// .cube GRAMMAR AND VALIDATION recovered from the shipped plugin's parseCube —
// the seven directives, their arities, the [2,256] size bound, the
// finite-only numeric rule, and the exact error strings. The original parseCube
// body is a 1924-instruction function that is only partly read (see
// ../../src/lutblend/RECOVERY-NOTES.md), so this is a fresh implementation OF the
// recovered rules rather than a transcription of the original.
//
// When parseCube is fully recovered this should be replaced by it. Until then
// this is what makes the plugin do something.
//
// Deliberately NOT emitted: "line N: <keyword> is not an integer". That message
// exists in the binary, but the branch that reaches it has not been located, and
// the size handler demonstrably TRUNCATES with fcvtzs rather than rejecting a
// fractional value. Guessing a trigger for it would put a behaviour in the plugin
// that the shipped one may not have. The other twelve messages are emitted.

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lutblend.h"

namespace lutlib {

// Parses .cube text into `out`. On failure returns false and fills `*err` with a
// message in the shipped plugin's format.
//
// `out` is only written on SUCCESS. parseCube browses a local CubeLUT and there is
// no store through the output pointer before the end, so a failed parse cannot
// leave the caller holding a half-built LUT.
bool parseCubeText(const std::string& text, lutblend::CubeLUT* out, std::string* err);

// Reads and parses a .cube file. Returns null on any failure.
std::shared_ptr<const lutblend::CubeLUT> loadCubeFile(const std::string& path,
                                                      std::string* err);

// The folder the shipped plugin advertises: "~/Documents/Williams Media LUTs".
std::string lutFolderPath();

// Files in that folder, sorted, extension-matched case-insensitively against
// ".cube". Returns empty if the folder is missing or unreadable.
std::vector<std::string> scanLutFolder();

// The PF_ADD_POPUP choice string for a file list: basenames without the
// extension, pipe-separated, exactly as AE expects.
std::string popupChoices(const std::vector<std::string>& paths);

// A small name -> LUT cache. The shipped plugin keeps parsed LUTs in a
// std::map<std::string, std::shared_ptr<const CubeLUT>> so a frame does not
// re-read or re-parse them; this mirrors that behaviour.
//
// THREAD SAFETY IS REQUIRED, not optional. The plugin declares
// PF_OutFlag2_SUPPORTS_THREADED_RENDERING (recovered: eGL2 == 1 << 27), so AE is
// entitled to run renders concurrently and this cache is reachable from the
// render path. Access is serialised with a mutex; the parse itself happens inside
// the lock, which is acceptable because it happens once per file rather than per
// frame. Without this the plugin would be a data race waiting to happen.
class Library {
 public:
  // Returns the cached LUT, parsing on first use. Null if unavailable.
  std::shared_ptr<const lutblend::CubeLUT> get(const std::string& path,
                                               std::string* err);
  void clear();
  size_t cachedCount() const;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<const lutblend::CubeLUT>> cache_;
};

}  // namespace lutlib
