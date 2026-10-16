#pragma once

// A project file: what a Config is made of (its layers, pack levels, include directories,
// and defines), in one place that the game's setup and every tool read, so an editor or a
// checker sees the stack the game loads without a second copy of it.
//
//   ; baker-project.dta, at the root a tool opens
//   (project
//      (pack_levels engine game task)     ; hardest first
//      (include_dirs "data")              ; searched after an including file's directory
//      (defines PC)                       ; flags, as #ifdef sees them
//      (layers                            ; bottom to top
//         (default "data/default.dta")
//         (local "data/local.dta" optional)
//         (agent "data/agent.dta" optional)))
//
// Paths are relative to the project file's directory unless absolute. Every part but
// (layers ...) may be left out. A key the format does not know is an error, located, so a
// typo is found rather than ignored.
//
//   auto project = baker::read_project("baker-project.dta");
//   if (!project) ...;
//   baker::Config cfg;
//   if (auto r = baker::open_project(cfg, *project, {.skip = {"local"}}); !r) ...;

#include <baker/config.hpp>
#include <baker/list.hpp>
#include <baker/parse.hpp>
#include <baker/symbol.hpp>

#include <initializer_list>
#include <string_view>

namespace baker {

struct ProjectLayer {
    Symbol name;
    Symbol path;            // joined with the project file's directory, unless absolute
    bool optional = false;  // a missing file is an empty layer (LayerOptions::optional)
    bool enabled = true;    // `disabled` in the file: in the stack, skipped by the merge
};

struct Project {
    Symbol file;                // the project file, as it was named
    List<ProjectLayer> layers;  // bottom to top
    List<Symbol> pack_levels;   // hardest first; empty: the session's stay
    List<Symbol> include_dirs;  // joined with the project file's directory, unless absolute
    List<Symbol> defines;       // flags
};

// Reads a project file, through `reader` if given (an editor's unsaved buffer), else from
// the disk. It reaches no other file: an #include in it is refused.
[[nodiscard]] Expected<Project> read_project(std::string_view path,
                                             const LoadSession::Reader& reader = {});

struct ProjectOptions {
    // Layers to leave out, by name: a simulation or a check leaves out one person's own
    // tuning (`local`), so its results do not depend on whose machine runs it.
    // No default member initializer: MSVC does not implement list-initializing one (C2797).
    // Value-initialized, it is empty.
    std::initializer_list<std::string_view> skip;
};

// Sets `cfg.session()`'s pack levels, include directories, and defines from `project`, then
// pushes its layers, bottom to top. A Config to open a project into has no layers yet; set
// its session's reader first if the layers' text comes from somewhere other than the disk.
// Stops at the first layer that does not read, with its error.
[[nodiscard]] Expected<void> open_project(Config& cfg, const Project& project,
                                          const ProjectOptions& options = {});

}  // namespace baker
