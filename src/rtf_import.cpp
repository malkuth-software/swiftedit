#include "rtf_import.hpp"
#include <stx_rtf2md/markdown.hpp>
#include <stx_rtf2md/rtf.hpp>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace notepad {
namespace {
struct LossPhrase {
    stx_rtf2md::LossKind kind{};
    const char *singular{};
    const char *plural{};
};
// The RTF loss kinds an import can report, in the order they are listed.
constexpr std::array<LossPhrase, 13> loss_phrases{{
    {stx_rtf2md::LossKind::picture_skipped, "picture was skipped", "pictures were skipped"},
    {stx_rtf2md::LossKind::object_skipped, "embedded object was skipped", "embedded objects were skipped"},
    {stx_rtf2md::LossKind::destination_skipped, "header, footer, note or hidden part was skipped",
     "headers, footers, notes or hidden parts were skipped"},
    {stx_rtf2md::LossKind::table_flattened, "table became paragraphs", "tables became paragraphs"},
    {stx_rtf2md::LossKind::field_result_kept, "field kept only its displayed text",
     "fields kept only their displayed text"},
    {stx_rtf2md::LossKind::control_word_ignored, "unsupported formatting instruction was ignored",
     "unsupported formatting instructions were ignored"},
    {stx_rtf2md::LossKind::font_dropped, "font choice was not kept", "font choices were not kept"},
    {stx_rtf2md::LossKind::underline_style_collapsed, "underline style became a plain underline",
     "underline styles became a plain underline"},
    {stx_rtf2md::LossKind::justify_read_as_left, "justified paragraph became left-aligned",
     "justified paragraphs became left-aligned"},
    {stx_rtf2md::LossKind::size_quantised, "text size was rounded to the nearest step",
     "text sizes were rounded to the nearest step"},
    {stx_rtf2md::LossKind::indent_dropped, "paragraph indent was not kept", "paragraph indents were not kept"},
    {stx_rtf2md::LossKind::spacing_dropped, "paragraph spacing or empty paragraph was not kept",
     "paragraph spacings or empty paragraphs were not kept"},
    {stx_rtf2md::LossKind::control_character_dropped, "control character was removed",
     "control characters were removed"},
}};

std::string body_text(const stx_rtf2md::BodySize &body) {
    const int whole = body.half_points / 2;
    std::string text = std::to_string(whole);
    if (body.half_points % 2 != 0)
        text += ".5";
    text += " pt";
    switch (body.rule) {
    case stx_rtf2md::BodyRule::default_character_format:
        text += " (the document's default character format)";
        break;
    case stx_rtf2md::BodyRule::normal_style:
        text += " (the Normal style)";
        break;
    case stx_rtf2md::BodyRule::most_common:
        text += " (the size of most of the text)";
        break;
    case stx_rtf2md::BodyRule::none:
        text += " (no sized text; the default)";
        break;
    }
    return text;
}

std::string failure_text(const stx_rtf2md::Result &result) {
    const std::string offset = std::to_string(result.stop.value);
    switch (result.status) {
    case stx_rtf2md::Status::malformed:
        return "The file is not readable RTF (stopped at byte " + offset + "). Nothing was imported.";
    case stx_rtf2md::Status::unsupported_encoding:
        return "The RTF uses a character set other than Windows-1252 (byte " + offset +
               "). SwiftEdit imports Windows-1252 and Unicode RTF only. Nothing was imported.";
    case stx_rtf2md::Status::limit_exceeded:
    case stx_rtf2md::Status::effort_exceeded:
        return "The RTF exceeds the import limits. Nothing was imported.";
    default:
        return "The RTF could not be converted. Nothing was imported.";
    }
}
} // namespace

bool looks_like_rtf(std::string_view bytes) {
    const bool rtf = bytes.substr(0, 5) == "{\\rtf";
    return rtf;
}

bool has_rtf_extension(std::string_view filename) {
    if (filename.size() < 4)
        return false;
    const std::string_view extension = filename.substr(filename.size() - 4);
    std::string lower{};
    for (const char character : extension)
        lower.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                             : character);
    const bool matched = lower == ".rtf";
    return matched;
}

RtfImport import_rtf(std::string_view bytes) {
    const stx_rtf2md::Limits limits{};
    stx_rtf2md::RtfReader reader(limits);
    stx_rtf2md::Document document{};
    stx_rtf2md::LossReport loss{};
    const std::span<const std::byte> input(reinterpret_cast<const std::byte *>(bytes.data()), bytes.size());
    const stx_rtf2md::Result read = reader.read(input, document, loss);
    if (read.status != stx_rtf2md::Status::ok)
        throw std::runtime_error(failure_text(read));
    stx_rtf2md::MarkdownWriter writer(limits);
    std::vector<char> output(bytes.size() * 2 + 4096);
    stx_rtf2md::Result written = writer.write(document, std::span<char>(output.data(), output.size()));
    if (written.status == stx_rtf2md::Status::output_too_small) {
        output.resize(written.required);
        written = writer.write(document, std::span<char>(output.data(), output.size()));
    }
    if (written.status != stx_rtf2md::Status::ok)
        throw std::runtime_error("The converted Markdown exceeds the import limits. Nothing was imported.");
    RtfImport result{};
    result.markdown.assign(output.data(), written.produced);
    for (const LossPhrase &phrase : loss_phrases) {
        const stx_rtf2md::LossEntry entry = loss.entry(phrase.kind);
        if (entry.count == 0)
            continue;
        result.losses += std::to_string(entry.count) + " " +
                         (entry.count == 1 ? phrase.singular : phrase.plural) + ".\n";
    }
    result.body = body_text(reader.last_body());
    return result;
}
} // namespace notepad
