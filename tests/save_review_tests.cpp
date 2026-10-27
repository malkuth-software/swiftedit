#include "save_review.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "platform.hpp"

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void write(const std::filesystem::path &path, std::string_view text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output)
        throw std::runtime_error("Fixture write failed");
}
struct Cleanup {
    std::filesystem::path path{};
    ~Cleanup() {
        std::error_code error{};
        std::filesystem::remove_all(path, error);
    }
};
void verify_session_review(const std::filesystem::path &dir) {
    const std::filesystem::path path = dir / "session.txt";
    std::string original = "A\r\nB\rC\n";
    original.push_back('\0');
    write(path, notepad::encode(original, notepad::Encoding::utf16_le, notepad::TextControls::preserve));
    swiftedit::Session session{};
    session.open_decoded(path);
    session.replace_ranges({{0, 1}}, "Mine", session.stamp());
    const std::string edited = session.text();
    write(path, "external");
    swiftedit::SaveReview review(path, session.stamp(), session.encoding());
    bool unreviewed_refused = false;
    try { review.publish(session); }
    catch (const std::exception &) { unreviewed_refused = true; }
    check(unreviewed_refused && notepad::read_file(path).bytes == "external",
          "Session publication cannot skip review");
    review.choose(swiftedit::ConflictChoice::save_over);
    review.review(path, notepad::Encoding::utf16_be, swiftedit::SaveEndings::lf);
    write(path, "later external");
    const swiftedit::DocumentStamp before_failure = session.stamp();
    bool raced = false;
    try { review.publish(session); }
    catch (const std::exception &) { raced = true; }
    check(raced && !review.ready() && session.text() == edited &&
              session.encoding() == notepad::Encoding::utf16_le && session.path() == path &&
              session.stamp().identity == before_failure.identity &&
              session.stamp().revision == before_failure.revision &&
              notepad::read_file(path).bytes == "later external",
          "Reviewed Session race preserves disk, logical source, encoding, path and revision");
    check(session.undo() && session.text() == original && session.redo() && session.text() == edited,
          "Failed reviewed Session save preserves undo and redo");
    swiftedit::SaveReview confirmed(path, session.stamp(), session.encoding());
    confirmed.choose(swiftedit::ConflictChoice::save_over);
    confirmed.review(path, notepad::Encoding::utf16_be, swiftedit::SaveEndings::lf);
    confirmed.publish(session);
    const std::string normalized = swiftedit::normalize_newlines(edited, "\n");
    check(session.text() == normalized && !session.dirty() && !session.undo() &&
              session.encoding() == notepad::Encoding::utf16_be &&
              notepad::read_file(path).bytes == notepad::encode(normalized,
                  notepad::Encoding::utf16_be, notepad::TextControls::preserve),
          "Reviewed Session success adopts normalized text, encoding and save baseline together");
    session.restore_opened();
    check(session.text() == original && session.dirty() && session.undo() && session.text() == normalized,
          "Reviewed publication preserves the independent as-opened source and undoable restoration");
    bool reused = false;
    try { confirmed.publish(session); }
    catch (const std::exception &) { reused = true; }
    check(reused, "Session review publication is one use");
    swiftedit::SaveReview copy(path, session.stamp(), session.encoding());
    copy.choose(swiftedit::ConflictChoice::new_copy);
    const std::filesystem::path copy_path = copy.target();
    copy.review(copy_path, notepad::Encoding::utf8_bom, swiftedit::SaveEndings::preserve);
    write(copy_path, "raced copy");
    bool copy_race = false;
    try { copy.publish(session); }
    catch (const std::exception &) { copy_race = true; }
    check(copy_race && !copy.ready() && session.path() == path &&
              session.encoding() == notepad::Encoding::utf16_be &&
              notepad::read_file(copy_path).bytes == "raced copy",
          "Reviewed new-copy race cannot acquire overwrite authority");
    const std::filesystem::path fresh = dir / "session-fresh.txt";
    copy.review(fresh, notepad::Encoding::utf8_bom, swiftedit::SaveEndings::preserve);
    copy.publish(session);
    check(session.path() == fresh && session.encoding() == notepad::Encoding::utf8_bom &&
              !session.dirty() && session.text() == normalized &&
              notepad::read_file(fresh).bytes == notepad::encode(normalized,
                  notepad::Encoding::utf8_bom, notepad::TextControls::preserve),
          "Reviewed new-copy success changes the Session target and codec after publication");
    swiftedit::SaveReview stale(fresh, session.stamp(), session.encoding());
    stale.choose(swiftedit::ConflictChoice::save_over);
    stale.review(fresh, notepad::Encoding::utf8, swiftedit::SaveEndings::crlf);
    session.replace_ranges({{0, 0}}, "X", session.stamp());
    bool changed = false;
    try { stale.publish(session); }
    catch (const std::exception &) { changed = true; }
    check(changed && !stale.ready() && session.dirty() && session.text().starts_with("XMine") &&
              notepad::read_file(fresh).bytes == notepad::encode(normalized,
                  notepad::Encoding::utf8_bom, notepad::TextControls::preserve),
          "Changed Session revision invalidates reviewed write authority");
    const std::filesystem::path bad = dir / "session-illegal.txt";
    write(bad, "\xff");
    session.open(bad);
    swiftedit::SaveReview illegal(bad, session.stamp(), session.encoding());
    illegal.choose(swiftedit::ConflictChoice::new_copy);
    const std::filesystem::path safe = illegal.target();
    illegal.review(safe, notepad::Encoding::utf8, swiftedit::SaveEndings::preserve);
    bool illegal_refused = false;
    try { illegal.publish(session); }
    catch (const std::exception &) { illegal_refused = true; }
    check(illegal_refused && !illegal.ready() && !std::filesystem::exists(safe) &&
              session.text() == "\xff" && session.path() == bad,
          "Reviewed UTF-8 publication cannot bypass illegal-byte refusal");
    const std::filesystem::path large = dir / "session-read-only.txt";
    write(large, std::string(swiftedit::editable_limit, 'x'));
    session.open(large);
    swiftedit::SaveReview read_only(large, session.stamp(), session.encoding());
    read_only.choose(swiftedit::ConflictChoice::new_copy);
    const std::filesystem::path refused_target = read_only.target();
    read_only.review(refused_target, notepad::Encoding::utf8, swiftedit::SaveEndings::preserve);
    bool large_refused = false;
    try { read_only.publish(session); }
    catch (const std::exception &) { large_refused = true; }
    check(large_refused && !read_only.ready() && session.read_only() &&
              !std::filesystem::exists(refused_target),
          "Reviewed save cannot turn a read-only paged Session into an empty output");
    const std::filesystem::path expanding = dir / "session-ending-expansion.txt";
    const std::string lines(swiftedit::editable_limit / 2, '\n');
    write(expanding, lines);
    session.open(expanding);
    const swiftedit::DocumentStamp before_expansion = session.stamp();
    swiftedit::SaveReview expansion(expanding, session.stamp(), session.encoding());
    expansion.choose(swiftedit::ConflictChoice::save_over);
    expansion.review(expanding, notepad::Encoding::utf8, swiftedit::SaveEndings::crlf);
    bool expansion_refused = false;
    try { expansion.publish(session); }
    catch (const std::exception &) { expansion_refused = true; }
    check(expansion_refused && !expansion.ready() && session.text() == lines && !session.dirty() &&
              session.stamp().revision == before_expansion.revision &&
              notepad::read_file(expanding).bytes == lines,
          "Reviewed ending expansion cannot exceed the editable limit or mutate baseline/disk");
}
} // namespace
int main() {
    try {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() /
            ("swiftedit-save-review-" + std::to_string(test_process_id()));
        check(std::filesystem::create_directory(dir), "Unique fixture directory");
        Cleanup cleanup{dir};
        const std::filesystem::path path = dir / "report.txt";
        write(path, "opened");
        notepad::Document document{};
        document.open(path);
        swiftedit::Session identity{};
        const swiftedit::DocumentStamp stamp = identity.stamp();
        write(path, "external");
        swiftedit::SaveReview review(path, stamp, document.encoding);
        bool first_stage_refused = false;
        try {
            review.publish(document, "mine", stamp);
        } catch (const std::exception &) {
            first_stage_refused = true;
        }
        check(first_stage_refused && notepad::read_file(path).bytes == "external",
              "Cannot skip first choice and review");
        review.choose(swiftedit::ConflictChoice::save_over);
        check(!review.ready(), "First-stage choice is not write authority");
        review.review(path, notepad::Encoding::utf16_le, swiftedit::SaveEndings::crlf);
        check(review.ready() && review.destructive() && review.observed().bytes == "external",
              "Review exposes exact destructive target observation");
        bool invalid_metadata = false;
        try {
            review.review(path, static_cast<notepad::Encoding>(99), swiftedit::SaveEndings::crlf);
        } catch (const std::exception &) {
            invalid_metadata = true;
        }
        check(invalid_metadata && !review.ready(), "Invalid metadata revokes earlier review");
        review.review(path, notepad::Encoding::utf16_le, swiftedit::SaveEndings::crlf);
        write(path, "later external");
        bool race_refused = false;
        try {
            review.publish(document, "mine\n", stamp);
        } catch (const std::exception &) {
            race_refused = true;
        }
        check(race_refused && !review.ready() && document.encoding == notepad::Encoding::utf8 &&
                  document.saved_text == "opened" &&
                  notepad::read_file(path).bytes == "later external",
              "Race invalidates consent and preserves external file and document metadata");
        review.review(path, notepad::Encoding::utf16_le, swiftedit::SaveEndings::crlf);
        review.publish(document, "mine\n", stamp);
        const notepad::Decoded written = notepad::decode(notepad::read_file(path).bytes);
        check(written.text == "mine\r\n" && document.saved_text == written.text &&
                  document.encoding == notepad::Encoding::utf16_le,
              "Confirmed encoding and ending choices publish together");
        bool reused = false;
        try {
            review.choose(swiftedit::ConflictChoice::save_over);
        } catch (const std::exception &) {
            reused = true;
        }
        check(reused, "Published review is one use");
        swiftedit::SaveReview copy(path, stamp, document.encoding);
        copy.choose(swiftedit::ConflictChoice::new_copy);
        const std::filesystem::path copy_path = copy.target();
        check(copy_path.filename() == "report.1.txt", "Copy suggests dot-version before extension");
        copy.review(copy_path, notepad::Encoding::utf8, swiftedit::SaveEndings::preserve);
        check(!copy.destructive(), "New copy has no destructive target");
        copy.publish(document, "copy\n", stamp);
        check(notepad::read_file(copy_path).bytes == "copy\n" &&
                  notepad::decode(notepad::read_file(path).bytes).text == "mine\r\n",
              "Copy preserves original and changes the document target only after success");
        swiftedit::SaveReview collision(path, stamp, document.encoding);
        collision.choose(swiftedit::ConflictChoice::new_copy);
        bool collision_refused = false;
        try {
            collision.review(copy_path, document.encoding, swiftedit::SaveEndings::preserve);
        } catch (const std::exception &) {
            collision_refused = true;
        }
        check(collision_refused && !collision.ready(),
              "New copy never inherits overwrite authority");
        swiftedit::SaveReview stale(path, stamp, document.encoding);
        stale.choose(swiftedit::ConflictChoice::save_over);
        stale.review(path, document.encoding, swiftedit::SaveEndings::preserve);
        identity.reset();
        bool stale_refused = false;
        try {
            stale.publish(document, "stale", identity.stamp());
        } catch (const std::exception &) {
            stale_refused = true;
        }
        check(stale_refused && !stale.ready(), "New document identity revokes prior review");
        verify_session_review(dir);
        std::cout << "Save review state and publication tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
