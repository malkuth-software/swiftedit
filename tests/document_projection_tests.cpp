#include "document_projection.hpp"
#include "selection_set.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace gf = gui_forms;
namespace se = swiftedit;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
gf::DocumentPageRequest request(const se::Session &session, std::uint64_t begin, std::uint64_t end) {
    const se::DocumentStamp stamp = session.stamp();
    gf::DocumentPageRequest result{};
    result.revision = {stamp.identity.value, stamp.revision.value};
    result.serial = 1;
    result.permitted = {gf::SourceByteOffset(begin), gf::SourceByteOffset(end)};
    result.viewport.anchor = gf::SourceByteOffset(begin);
    return result;
}
void finish(se::DocumentProjection &producer, const se::Session &session) {
    for (std::size_t step = 0; step < 12 && producer.state() == se::ProjectionState::pending; ++step)
        static_cast<void>(producer.step(session));
}
void write(const std::filesystem::path &path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    check(static_cast<bool>(file), "Fixture write failed");
}
void verify_decoded_projection(const std::filesystem::path &directory) {
    const std::string prefix = "head\r\n";
    const std::string suffix = std::string("\xc3\xa9\0", 3) + "e\xcc\x81\xf0\x9f\x98\x80\r\n";
    const std::string source = prefix + suffix;
    const notepad::Encoding encodings[] = {notepad::Encoding::utf8, notepad::Encoding::utf8_bom,
        notepad::Encoding::utf16_le, notepad::Encoding::utf16_be};
    const std::filesystem::path path = directory / "decoded.txt";
    for (const notepad::Encoding encoding : encodings) {
        write(path, notepad::encode(source, encoding, notepad::TextControls::preserve));
        se::Session session{};
        session.open_decoded(path);
        gf::DocumentViewState view{};
        const gf::DocumentPageRequest initial = request(session, prefix.size(), session.size());
        check(view.bind(initial.revision, gf::SourceByteOffset(session.size())) == gf::DocumentViewStatus::success,
              "Decoded view binds logical UTF-8 size");
        const gf::DocumentRequestResult issued = view.request_page(initial.viewport, initial.permitted);
        check(issued.request.has_value(), "Decoded page request issued");
        const gf::DocumentPageRequest token = *issued.request;
        se::DocumentProjection producer(session, token);
        finish(producer, session);
        gf::DocumentPage page{};
        check(producer.publish(session, page), "Decoded page prepared");
        check(page.display_utf8 == "\xc3\xa9[U+0000]e\xcc\x81\xf0\x9f\x98\x80\r\n",
              "Decoded controls project without BOM or encoded-file offsets");
        check(view.publish(std::move(page)) == gf::DocumentViewStatus::success, "Decoded page adopted");
        const gf::SourceMappingResult begin = view.source_position(token, gf::DisplayByteOffset(2));
        const gf::SourceMappingResult end = view.source_position(token, gf::DisplayByteOffset(10));
        check(begin.position.has_value() && end.position.has_value() &&
                  (*begin.position).value == 8 && (*end.position).value == 9,
              "Generated NUL label endpoints map to absolute logical source bytes");
        const gf::SourceMappingResult interior = view.source_position(token, gf::DisplayByteOffset(3));
        check(!interior.position.has_value(), "Generated label interior cannot become source caret");
        const gf::DisplayMappingResult inverse = view.display_position(token, gf::SourceByteOffset(9));
        check(inverse.position.has_value() && (*inverse.position).value == 10,
              "Decoded source position round trips through public view mapping");
        bool refused = false;
        try {
            const se::SelectionSet split(session, view, token,
                {{gf::DisplayByteOffset(10), gf::DisplayByteOffset(11)}});
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "Scalar-valid projected position cannot split combining source grapheme");
        refused = false;
        try {
            const se::SelectionSet split_label(session, view, token,
                {{gf::DisplayByteOffset(3), gf::DisplayByteOffset(10)}});
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "Projected selection refuses generated label interior");
        const se::SelectionSet parallel(session, view, token,
            {{gf::DisplayByteOffset(0), gf::DisplayByteOffset(2)},
             {gf::DisplayByteOffset(10), gf::DisplayByteOffset(13)}});
        check(parallel.can_rewrite(), "Different byte lengths with equal source grapheme counts can rewrite");
        const std::vector<se::SourceClipboard> parts = parallel.copy(session);
        check(parts.size() == 2 && parts[0].bytes() == "\xc3\xa9" && parts[1].bytes() == "e\xcc\x81",
              "Projected discontiguous copy preserves separate original Unicode parts");
        const se::SelectionSet unequal(session, view, token,
            {{gf::DisplayByteOffset(0), gf::DisplayByteOffset(2)},
             {gf::DisplayByteOffset(10), gf::DisplayByteOffset(17)}});
        check(!unequal.can_rewrite(), "Unequal projected grapheme counts remain copy-only");
        refused = false;
        try {
            unequal.rewrite(session, "X");
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused && session.text() == source && !session.dirty(),
              "Refused projected rewrite preserves source and saved state");
        const se::SelectionSet selected(session, view, token,
            {{gf::DisplayByteOffset(2), gf::DisplayByteOffset(10)}});
        const std::vector<se::SourceClipboard> copied = selected.copy(session);
        check(copied.size() == 1 && copied[0].bytes() == std::string_view("\0", 1),
              "Mapped copy returns source NUL, never its label");
        selected.rewrite(session, "X");
        std::string expected = source;
        expected.replace(8, 1, "X");
        check(session.text() == expected && session.encoding() == encoding,
              "Mapped edit replaces only decoded source control and retains codec");
        refused = false;
        try {
            const se::SelectionSet stale(session, view, token,
                {{gf::DisplayByteOffset(2), gf::DisplayByteOffset(10)}});
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "Changed Session refuses stale selection even before view rebind");
        const se::DocumentStamp changed = session.stamp();
        check(view.bind({changed.identity.value, changed.revision.value}, gf::SourceByteOffset(session.size())) ==
                  gf::DocumentViewStatus::success, "Changed Session revision rebinds view");
        check(!view.source_position(token, gf::DisplayByteOffset(2)).position.has_value(),
              "Rebound view refuses old projected position");
        check(session.undo() && session.text() == source, "Mapped replacement is one undoable edit");
        session.save();
        std::ifstream saved(path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        check(bytes == notepad::encode(source, encoding, notepad::TextControls::preserve),
              "Projection/edit/undo/save preserves original encoded bytes");
    }
}
void verify_dense_labels(const std::filesystem::path &directory) {
    const std::filesystem::path path = directory / "dense-controls.txt";
    const std::string source(65536, '\0');
    write(path, source);
    se::Session session{};
    session.open(path);
    gf::DocumentViewState view{};
    const gf::DocumentPageRequest initial = request(session, 0, session.size());
    check(view.bind(initial.revision, gf::SourceByteOffset(session.size())) == gf::DocumentViewStatus::success,
          "Dense-label view binds");
    const gf::DocumentRequestResult issued = view.request_page(initial.viewport, initial.permitted);
    check(issued.request.has_value(), "Dense-label request issued");
    se::DocumentProjection producer(session, *issued.request);
    finish(producer, session);
    gf::DocumentPage page{};
    check(producer.publish(session, page) && page.mapping.size() == source.size(),
          "Every dense control keeps its own atomic mapping at the mapping limit");
    check(view.publish(std::move(page)) == gf::DocumentViewStatus::success,
          "Installed provider accepts complete dense-label mapping");
    const gf::SourceMappingResult boundary = view.source_position(*issued.request, gf::DisplayByteOffset(262144));
    const gf::SourceMappingResult interior = view.source_position(*issued.request, gf::DisplayByteOffset(262145));
    check(boundary.position.has_value() && (*boundary.position).value == 32768 && !interior.position.has_value(),
          "Adjacent identical labels keep independent boundaries and refuse label interiors");
    check(session.text() == source && !session.dirty(), "Dense-label projection preserves every source byte");
}
int main() {
    try {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-projection-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(std::filesystem::create_directory(directory), "Unique projection fixture directory required");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() {
                std::error_code error{};
                std::filesystem::remove_all(directory, error);
            }
        } cleanup{directory};
        const std::filesystem::path path = directory / "bytes.txt";
        const std::string source = std::string("[U+0000]\0\xff\r\n", 12) + "e\xcc\x81\nlast";
        write(path, source);
        se::Session session{};
        session.open(path);
        gf::DocumentViewState view{};
        const gf::DocumentPageRequest initial = request(session, 0, session.size());
        check(view.bind(initial.revision, gf::SourceByteOffset(session.size())) == gf::DocumentViewStatus::success,
              "Installed D1 view accepts Session revision");
        const gf::DocumentRequestResult issued = view.request_page(initial.viewport, initial.permitted);
        check(issued.request.has_value(), "Installed D1 issues page request");
        se::DocumentProjection producer(session, *issued.request);
        check(producer.step(session) == se::ProjectionState::pending, "Read and projection are separate steps");
        finish(producer, session);
        gf::DocumentPage page{};
        check(producer.publish(session, page), "Complete current projection publishes");
        check(page.display_utf8 == "[U+0000][U+0000][BYTE FF]\r\ne\xcc\x81\nlast",
              "Literal label collisions, invalid bytes, controls and combining source are preserved");
        check(page.mapping.size() == 4 && page.mapping[0].kind == gf::DocumentMapKind::identity_utf8 &&
                  page.mapping[0].source.end.value == 8 &&
                  page.mapping[1].kind == gf::DocumentMapKind::atomic_token &&
                  page.mapping[2].kind == gf::DocumentMapKind::atomic_token,
              "Literal label source differs from generated atomic control label");
        check(view.publish(std::move(page)) == gf::DocumentViewStatus::success,
              "Installed public D1 accepts produced payload");
        const gf::SourceMappingResult literal_position = view.source_position(*issued.request, gf::DisplayByteOffset(4));
        check(literal_position.position.has_value() && (*literal_position.position).value == 4,
              "Coalesced literal label keeps interior source positions");
        const gf::SourceMappingResult generated_position = view.source_position(*issued.request, gf::DisplayByteOffset(9));
        check(!generated_position.position.has_value(), "Generated label interior stays atomic beside identity run");
        check(session.text() == source && !session.dirty(), "Projection never mutates document bytes");
        check(!producer.publish(session, page), "Projection publication is single use");
        se::DocumentProjection split(session, request(session, 0, 11));
        check(split.step(session) == se::ProjectionState::context_required, "Split CRLF end requires context");
        se::DocumentProjection interior(session, request(session, 11, session.size()));
        check(interior.step(session) == se::ProjectionState::context_required, "Split CRLF start requires context");
        se::DocumentProjection valid_suffix(session, request(session, 12, session.size()));
        finish(valid_suffix, session);
        check(valid_suffix.publish(session, page) && page.mapping.front().source.begin.value == 12,
              "Nonzero page uses absolute source mapping");
        se::DocumentProjection occupied(session, initial);
        finish(occupied, session);
        const std::string retained = page.display_utf8;
        check(!occupied.publish(session, page) && page.display_utf8 == retained,
              "Occupied output remains unchanged");
        occupied.cancel();
        check(occupied.state() == se::ProjectionState::cancelled, "Cancel retires prepared payload");
        se::DocumentProjection stale(session, initial);
        static_cast<void>(stale.step(session));
        session.replace_ranges({{0, 1}}, "X", session.stamp());
        check(stale.step(session) == se::ProjectionState::stale, "Revision change cancels source preparation");
        gf::DocumentPage empty{};
        check(!stale.publish(session, empty) && !empty.request.serial, "Stale work cannot publish");
        se::DocumentProjection late(session, request(session, 0, session.size()));
        finish(late, session);
        session.reset();
        check(!late.publish(session, empty) && late.state() == se::ProjectionState::stale,
              "Document replacement revokes already completed projection");
        se::DocumentProjection blank(session, request(session, 0, 0));
        finish(blank, session);
        check(blank.publish(session, empty) && empty.display_utf8.empty() && empty.mapping.empty(),
              "Empty document publishes without invented mapping");
        std::string context(8191, 'a');
        context += "e\xcc\x81\r\nend";
        write(path, context);
        session.open(path);
        se::DocumentProjection crossing(session, request(session, 0, session.size()));
        finish(crossing, session);
        gf::DocumentPage crossed{};
        check(crossing.publish(session, crossed) && crossed.display_utf8 == context,
              "Read-step boundary does not split or relabel a combining grapheme");
        se::DocumentProjection interrupted(session, request(session, 0, session.size()));
        check(interrupted.step(session) == se::ProjectionState::pending, "Partial source preparation pending");
        interrupted.cancel();
        gf::DocumentPage cancelled{};
        check(interrupted.step(session) == se::ProjectionState::cancelled && !interrupted.publish(session, cancelled),
              "Cancelled partial source cannot resume or publish");
        gf::DocumentPageRequest invalid = request(session, 0, session.size());
        invalid.serial = 0;
        se::DocumentProjection missing_serial(session, invalid);
        check(missing_serial.state() == se::ProjectionState::invalid_request, "Missing request identity refused");
        invalid = request(session, 0, session.size());
        ++invalid.revision.document;
        se::DocumentProjection wrong_document(session, invalid);
        check(wrong_document.state() == se::ProjectionState::stale, "Foreign document stamp refused");
        write(path, std::string(70000, 'a'));
        session.open(path);
        se::DocumentProjection unfinished_line(session, request(session, 0, 65536));
        check(unfinished_line.step(session) == se::ProjectionState::context_required,
              "Long paragraph is not falsely certified complete at the source budget");
        const std::filesystem::path large_path = directory / "large.txt";
        std::string block{};
        for (std::size_t index = 0; index < 32768; ++index)
            block += "a\n";
        {
            std::ofstream file(large_path, std::ios::binary);
            for (std::size_t index = 0; index < 256; ++index)
                file.write(block.data(), static_cast<std::streamsize>(block.size()));
            file.close();
            check(static_cast<bool>(file), "Large fixture write");
        }
        session.open(large_path);
        check(session.read_only(), "Large fixture is read only");
        se::DocumentProjection large(session, request(session, 0, 65536));
        finish(large, session);
        gf::DocumentPage large_page{};
        check(large.publish(session, large_page) && large_page.display_utf8 == block && !session.dirty(),
              "Read-only Session supplies exact bounded document pages");
        check(large_page.mapping.size() == 1 && large_page.mapping.front().source.begin.value == 0 &&
                  large_page.mapping.front().source.end.value == block.size(),
              "Full plain-text page uses one identity mapping without dropping source coverage");
        se::DocumentProjection too_large(session, request(session, 0, 65537));
        check(too_large.state() == se::ProjectionState::budget_exceeded, "Projection enforces installed source budget");
        verify_decoded_projection(directory);
        verify_dense_labels(directory);
        std::cout << "Session document projection tests passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
