#include "theme_bridge.hpp"
#include <algorithm>

namespace swiftedit {
namespace gf = gui_forms;
namespace hl = swiftedit::highlight;

namespace {

std::string finding_message(const gf::ThemeCompileDiagnostic &finding) {
    std::string result = finding.code;
    if (!finding.name.empty())
        result += " " + finding.name;
    if (!finding.rule.empty())
        result += " in " + finding.rule;
    if (!finding.property.empty())
        result += " (" + finding.property + ")";
    if (!finding.detail.empty())
        result += ": " + finding.detail;
    return result;
}

void append_field(const gf::CompiledThemeRules::FieldProvenance &field, std::string_view sheet,
                  std::size_t first_appended, std::vector<hl::ByteOffset> &out) {
    if (!field.authored || field.source == nullptr)
        return;
    const gf::RuleProvenance &source = *field.source;
    if (!names_sheet(source.file, sheet))
        return;
    const hl::ByteOffset offset{static_cast<std::size_t>(source.offset)};
    for (std::size_t index = first_appended; index < out.size(); ++index) {
        if (out[index] == offset)
            return;
    }
    out.push_back(offset);
}

} // namespace

bool names_sheet(std::string_view engine_file, std::string_view sheet) noexcept {
    if (sheet.empty() || engine_file == sheet)
        return true;
    if (engine_file.size() <= sheet.size())
        return false;
    const std::size_t separator = engine_file.size() - sheet.size() - 1U;
    const bool result = engine_file[separator] == '/' && engine_file.substr(separator + 1U) == sheet;
    return result;
}

void append_code_diagnostics(std::span<const gf::ThemeCompileDiagnostic> findings, std::string_view sheet,
                             std::vector<CodeDiagnostic> &out) {
    for (const gf::ThemeCompileDiagnostic &finding : findings) {
        if (finding.line == 0U || !names_sheet(finding.file, sheet))
            continue;
        CodeDiagnostic diagnostic{};
        diagnostic.place.position = hl::SourcePosition{finding.line, std::max<std::uint32_t>(finding.column, 1U)};
        diagnostic.place.severity = finding.severity == gf::ThemeDiagnosticSeverity::error
                                        ? hl::DiagnosticSeverity::error
                                        : hl::DiagnosticSeverity::warning;
        diagnostic.message = finding_message(finding);
        out.push_back(std::move(diagnostic));
    }
}

void append_rule_offsets(const gf::Theme &theme, const gf::Control &control, gf::ControlVisualRole role,
                         std::string_view sheet, std::vector<hl::ByteOffset> &out) {
    gf::CompiledThemeRules::PartProvenance provenance{};
    theme.provenance(control, role, gf::ControlVisualContext{}, provenance);
    const std::size_t first_appended = out.size();
    for (std::size_t index = 0U; index < provenance.parameter_count; ++index)
        append_field(provenance.parameters[index], sheet, first_appended, out);
    for (const gf::CompiledThemeRules::FieldProvenance &field : provenance.materials)
        append_field(field, sheet, first_appended, out);
    for (const gf::CompiledThemeRules::FieldProvenance &field : provenance.recipe_fields)
        append_field(field, sheet, first_appended, out);
}

} // namespace swiftedit
