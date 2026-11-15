#pragma once
// GUI.Forms' XML documents (cp34): form documents (.form, rooted in
// <form xmlns="urn:gui-forms:form:1">) and composite control documents
// (.control, rooted in <control xmlns="urn:gui-forms:control:1">). Both are
// GUI.Forms' bounded XML subset: elements, attributes, comments, the leading
// XML declaration, the predefined entities and character references; no DTD,
// no CDATA, no other processing instruction.
//
// Token kinds: an element's name is a type (a form object's kebab type, or a
// control document's element), an attribute's name a property, an `id`
// attribute's value the stable ID, every other attribute value a string, a
// comment a comment, the declaration's `?xml` an at-rule, an entity or
// character reference an identifier, and the angle brackets, `/`, `?` and
// `=` punctuation. Text between elements is plain (a form holds none; a
// control document's <description> and <look> hold text, whose CSS is not
// coloured here).
//
// The lexer is line-at-a-time. Its state carries what crosses a line: an open
// comment, a tag (GUI.Forms' converted documents write one attribute per
// line), whether an attribute's `=` or value is still to come, an open quoted
// value and its quote, whether that value is an `id`, and whether the tag is
// the XML declaration. Nesting depth is not tracked: colours never depend on
// it.
#include "language.hpp"

namespace swiftedit::highlight {

enum class XmlDialect : std::uint8_t {
    form_document,     // a .form file
    control_document,  // a .control file
};

class XmlLanguage final : public Language {
public:
    explicit XmlLanguage(XmlDialect dialect) noexcept : dialect_(dialect) {}
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view display_name() const noexcept override;
    [[nodiscard]] LexState initial_state() const noexcept override;
    [[nodiscard]] LexState lex_line(std::string_view line, LexState entry,
                                    std::vector<LineToken> &tokens) const override;
    [[nodiscard]] XmlDialect dialect() const noexcept { return dialect_; }

private:
    XmlDialect dialect_{XmlDialect::form_document};
};

// The two dialects, with static storage duration (the registry's entries).
// Their names are "form" and "control".
[[nodiscard]] const XmlLanguage &xml_form_document_language() noexcept;
[[nodiscard]] const XmlLanguage &xml_control_document_language() noexcept;

} // namespace swiftedit::highlight
