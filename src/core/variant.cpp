#include "nav/core/variant.hpp"

#include <algorithm>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

void append_unique(std::vector<std::string>& into, const std::vector<std::string>& add) {
    for (const auto& a : add)
        if (std::find(into.begin(), into.end(), a) == into.end()) into.push_back(a);
}

std::string key_of(const std::string& opt) { return opt.substr(0, opt.find('=')); }

} // namespace

const Variant* find_variant(const ProjectConfig& cfg, const std::string& name) {
    for (const auto& v : cfg.variants)
        if (v.name == name) return &v;
    return nullptr;
}

std::string variant_names(const ProjectConfig& cfg) {
    if (cfg.variants.empty()) return "none";
    std::string out;
    for (const auto& v : cfg.variants) out += (out.empty() ? "" : ", ") + v.name;
    return out;
}

std::optional<ProjectConfig> apply_variant(const ProjectConfig& cfg, const std::string& name, std::string& error) {
    if (name.empty()) return cfg;
    const Variant* v = find_variant(cfg, name);
    if (!v) {
        error = "unknown variant '" + name + "' (known: " + variant_names(cfg) + ")";
        return std::nullopt;
    }
    ProjectConfig out = cfg;
    BuildSettings& b = out.build;
    append_unique(b.sources, v->sources_add);
    append_unique(b.exclude, v->sources_remove);
    append_unique(b.defines, v->defines);
    if (!v->output.empty()) b.output = v->output;
    b.declared = true;  // a variant always needs the [build]-aware CMakeLists.txt
    return out;
}

fs::path variant_build_dir(const std::string& variant) {
    return variant.empty() ? fs::path("build") : fs::path("build") / variant;
}

std::string firmware_name(const ProjectConfig& cfg, const std::string& variant) {
    if (!variant.empty())
        if (const Variant* v = find_variant(cfg, variant); v && !v->output.empty()) return v->output;
    return cfg.project_name;
}

fs::path variant_config_path(const ProjectConfig& cfg, const std::string& variant) {
    if (!variant.empty())
        if (const Variant* v = find_variant(cfg, variant); v && !v->kconfig.empty()) return cfg.root / v->kconfig;
    return cfg.root / ".config";
}

std::vector<std::string> merge_options(const std::vector<std::string>& base, const std::vector<std::string>& overrides) {
    std::vector<std::string> out = base;
    for (const auto& o : overrides) {
        auto it = std::find_if(out.begin(), out.end(), [&](const std::string& e) { return key_of(e) == key_of(o); });
        if (it != out.end()) *it = o;
        else out.push_back(o);
    }
    return out;
}

bool apply_variant_options(std::vector<ResolvedLib>& libs, const Variant& variant, std::string& error) {
    for (const auto& [name, opts] : variant.dep_options) {
        auto it = std::find_if(libs.begin(), libs.end(), [&](const ResolvedLib& l) { return l.name == name; });
        if (it == libs.end()) {
            std::string known;
            for (const auto& l : libs) known += (known.empty() ? "" : ", ") + l.name;
            error = "[variant." + variant.name + ".dependencies." + name + "]: no such dependency (known: " +
                    (known.empty() ? "none" : known) + ")";
            return false;
        }
        it->options = merge_options(it->options, opts);
    }
    return true;
}

} // namespace nav::core
