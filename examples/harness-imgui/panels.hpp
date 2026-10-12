// The reference harness's panels: plain functions over a Config, a TweakRegistry, and a
// Context, each drawing one ImGui window. They own no window and no renderer, so a host
// drops them into its own ImGui setup, and a test runs them headless.
//
// A button sets a request in its panel's state, and the panel carries it out in the same
// call, after drawing. A test sets the same requests directly: it drives the code the
// buttons drive, without synthesizing mouse clicks.
#pragma once

#include <moira/config.hpp>
#include <moira/script.hpp>
#include <moira/shape.hpp>

#include <optional>
#include <string>
#include <vector>

namespace harness {

// "default.dta:3 (layer default)", the note if the data has one, or what is unsaved: the
// tooltip shown on hovering a value.
std::string provenance(const moira::Config& cfg, std::string_view path);

// --- Tweaks: a widget per registered tweak, from its type and metadata. ---
struct TweaksState {
    struct Request {
        enum Kind { Set, Save, KeepLocal, Revert } kind;
        std::string path;
        std::vector<double> values;  // for Set
    };
    std::optional<Request> request;
    std::string status;  // the last request's outcome, shown in the panel
};
void tweaks_panel(moira::Config& cfg, moira::TweakRegistry& tweaks, TweaksState& state);

// --- Shapes: a widget per field of every array a shape governs (moira::shape). ---
struct ShapesState {
    struct Request {
        enum Kind { Set, Save } kind;
        std::string path;  // dotted, as Config::set takes it
        double value = 0;
    };
    std::optional<Request> request;
    std::string status;
};
void shapes_panel(moira::Config& cfg, ShapesState& state);

// --- Console: eval, with its log and history. ---
struct ConsoleState {
    char input[256] = {};
    bool submit = false;           // evaluate `input`, as Enter does
    std::vector<std::string> log;  // commands and their results or errors
    std::vector<std::string> history;
};
void console_panel(moira::Context& ctx, ConsoleState& state);

// --- Loop (D42): the recent reloads, each a bar of its six timed stages. ---
void loop_panel(const moira::Config& cfg);

// --- Layers (D42): the stack, enable toggles, each layer's effect, promotion. ---
struct LayersState {
    std::optional<std::string> toggle;  // a layer to enable or disable
    struct Promote {
        std::string from, to;
    };
    std::optional<Promote> promote;
    std::string promote_to = "default";
    std::vector<std::string> report;  // the last promotion: lines written, or every refusal
};
void layers_panel(moira::Config& cfg, LayersState& state);

// --- Snapshots (D42): capture A, capture B, and flip between them. ---
struct SnapshotsState {
    moira::ConfigSnapshot a, b;
    enum Request { None, CaptureA, CaptureB, Flip } request = None;
    char showing = 0;  // 'A' or 'B' once restored
    std::string status;
};
void snapshots_panel(moira::Config& cfg, SnapshotsState& state);

}  // namespace harness
