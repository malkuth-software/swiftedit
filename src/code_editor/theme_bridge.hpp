#pragma once
// What the theme editor hands the code editor from GUI.Forms' theme engine,
// turned into the editor's terms: the compiler's findings for one sheet as
// diagnostics, and the rules that style one control as offsets into that
// sheet.
#include "code_editor/code_editor.hpp"
#include <gui_forms/gui_forms.hpp>
#include <span>
#include <string_view>
#include <vector>

namespace swiftedit {

// Whether a theme engine's file name names `sheet`: equal, or ending in
// "/<sheet>" (the engine names a package's sheet "<theme>/<path>"). An empty
// `sheet` matches every file.
[[nodiscard]] bool names_sheet(std::string_view engine_file, std::string_view sheet) noexcept;

// Appends one diagnostic per finding in `sheet` that has a place (line 1 or
// later), with the code, name, rule, property and detail as its message.
// Columns count from 1 in Unicode scalars, as the theme compiler counts them.
void append_code_diagnostics(std::span<const gui_forms::ThemeCompileDiagnostic> findings,
                             std::string_view sheet, std::vector<CodeDiagnostic> &out);

// Appends the byte offset of each authored rule in `sheet` that sets a field
// of `control`'s recipe for `role` in the default context (the theme's
// provenance query: the winning rule per field), each offset once. `theme`
// and `control` are borrowed for the call.
void append_rule_offsets(const gui_forms::Theme &theme, const gui_forms::Control &control,
                         gui_forms::ControlVisualRole role, std::string_view sheet,
                         std::vector<highlight::ByteOffset> &out);

} // namespace swiftedit
