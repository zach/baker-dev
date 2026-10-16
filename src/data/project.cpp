// Project files (D172): a Config's layers, pack levels, include directories, and defines,
// read from one file that the game and its tools share.

#include <baker/project.hpp>

#include "common/containers.hpp"
#include "text.hpp"

namespace baker {
namespace {

Error at(const Array& a, std::string_view message) {
    return make_error(ErrorCode::Syntax, message, a.file(), a.line());
}

// A path, or a string given as a symbol, from a value: "data/x.dta" or data.
bool text_of(const Node& n, std::string_view& out) {
    if (auto s = n.try_str()) {
        out = *s;
        return true;
    }
    if (auto s = n.try_sym()) {
        out = s->str();
        return true;
    }
    return false;
}

Symbol joined(std::string_view dir, std::string_view path) {
    if (dir.empty() || detail::is_absolute_path(path)) return Symbol(path);
    return Symbol(detail::join_path(dir, path).view());
}

// Each value of `a` (after its tag) as a symbol, a path joined with `dir` if `paths`.
Expected<List<Symbol>> names(const Array& a, std::string_view dir, bool paths) {
    Vec<Symbol> out;
    for (size_t i = 1; i < a.size(); ++i) {
        std::string_view text;
        if (!text_of(a[i], text))
            return Unexpected(at(a, "(" + Str(a.tag().str()) + " ...) lists names or paths"));
        out.push_back(paths ? joined(dir, text) : Symbol(text));
    }
    return List<Symbol>(std::span<const Symbol>(out.data(), out.size()));
}

}  // namespace

Expected<Project> read_project(std::string_view path, const LoadSession::Reader& reader) {
    LoadSession session;
    if (reader) session.set_reader(reader);
    session.text_only = session.refuse_includes = true;  // a project file is the whole project
    auto tree = read_file(path, session);
    if (!tree) return Unexpected(std::move(tree).error());
    const Array* root = (*tree)->try_find("project");
    if (!root)
        return Unexpected(make_error(ErrorCode::NotFound,
                                     "a project file holds (project (layers ...) ...)",
                                     Symbol(path), 1));
    const std::string_view dir = detail::parent_path(path);
    Project p;
    p.file = Symbol(path);
    bool has_layers = false;
    for (size_t i = 1; i < root->size(); ++i) {
        const Array* part = (*root)[i].try_array();
        if (!part || part->empty())
            return Unexpected(at(*root, "(project ...) holds (key ...) forms"));
        const Symbol key = part->tag();
        if (key == Symbol("pack_levels") || key == Symbol("defines")) {
            auto list = names(*part, dir, false);
            if (!list) return Unexpected(std::move(list).error());
            (key == Symbol("defines") ? p.defines : p.pack_levels) = std::move(*list);
        } else if (key == Symbol("include_dirs")) {
            auto list = names(*part, dir, true);
            if (!list) return Unexpected(std::move(list).error());
            p.include_dirs = std::move(*list);
        } else if (key == Symbol("layers")) {
            has_layers = true;
            Vec<ProjectLayer> layers;
            for (size_t j = 1; j < part->size(); ++j) {
                const Array* l = (*part)[j].try_array();
                std::string_view file;
                if (!l || l->size() < 2 || l->tag().empty() || !text_of((*l)[1], file))
                    return Unexpected(
                        at(l ? *l : *part, "a layer is (name \"path\" [optional] [disabled])"));
                ProjectLayer layer{l->tag(), joined(dir, file)};
                for (size_t k = 2; k < l->size(); ++k) {
                    if ((*l)[k].is(Symbol("optional")))
                        layer.optional = true;
                    else if ((*l)[k].is(Symbol("disabled")))
                        layer.enabled = false;
                    else
                        return Unexpected(
                            at(*l, "layer '" + Str(l->tag().str()) +
                                       "': after the path, only optional and disabled"));
                }
                for (const ProjectLayer& seen : layers)
                    if (seen.name == layer.name)
                        return Unexpected(
                            at(*l, "two layers named '" + Str(layer.name.str()) + "'"));
                layers.push_back(layer);
            }
            p.layers =
                List<ProjectLayer>(std::span<const ProjectLayer>(layers.data(), layers.size()));
        } else {
            return Unexpected(at(*part, "a project has no '" + Str(key.str()) +
                                            "': it holds pack_levels, include_dirs, defines, "
                                            "and layers"));
        }
    }
    if (!has_layers) return Unexpected(at(*root, "a project needs (layers ...)"));
    return p;
}

Expected<void> open_project(Config& cfg, const Project& project, const ProjectOptions& options) {
    LoadSession& session = cfg.session();
    if (!project.pack_levels.empty()) session.set_pack_levels(project.pack_levels.items());
    for (const Symbol dir : project.include_dirs) session.add_include_dir(dir.str());
    for (const Symbol flag : project.defines) session.define(flag);
    for (const ProjectLayer& l : project.layers) {
        bool skipped = false;
        for (const std::string_view s : options.skip) skipped = skipped || l.name.str() == s;
        if (skipped) continue;
        if (auto r = cfg.push_layer(l.name.str(), l.path.str(),
                                    {.optional = l.optional, .enabled = l.enabled});
            !r)
            return r;
    }
    return {};
}

}  // namespace baker
