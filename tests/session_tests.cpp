#include "session.hpp"
#include "new_file_writer.hpp"
#include "session_text_copy.hpp"
#include "session_copy.hpp"
#include "text_copy_stream.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "platform.hpp"
#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}

void raw(const std::filesystem::path &p, std::string_view s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), s.size());
}
void verify_decoded_sessions(const std::filesystem::path &dir) {
    const std::array<notepad::Encoding, 4> encodings{notepad::Encoding::utf8,
        notepad::Encoding::utf8_bom, notepad::Encoding::utf16_le, notepad::Encoding::utf16_be};
    std::string original = "A\r\nB\rC\n\t\xc3\xa9\xf0\x9f\x98\x80";
    original.push_back('\0');
    original.push_back('\x01');
    for (std::size_t index = 0; index < encodings.size(); ++index) {
        const notepad::Encoding encoding = encodings[index];
        const std::filesystem::path source = dir / ("encoded-" + std::to_string(index));
        const std::filesystem::path copy = dir / ("encoded-copy-" + std::to_string(index));
        const std::string bytes = notepad::encode(original, encoding, notepad::TextControls::preserve);
        raw(source, bytes);
        swiftedit::Session session{};
        session.open_decoded(source);
        check(session.text() == original && session.encoding() == encoding && !session.dirty(),
              "Decoded Session retains controls, mixed endings, non-BMP Unicode and encoding");
        const swiftedit::SourceClipboard clipboard =
            session.copy_range({0, original.size()}, session.stamp());
        check(clipboard.bytes() == original, "Decoded clipboard retains logical UTF-8 source bytes");
        const std::filesystem::path text_copy = dir / ("utf8-copy-" + std::to_string(index));
        session.save_text_copy(text_copy);
        check(notepad::read_file(text_copy).bytes == original && session.encoding() == encoding &&
                  session.path() == source && !session.dirty(),
              "Explicit text copy is UTF-8 and does not change decoded source encoding or baseline");
        swiftedit::Session raw_session{};
        raw_session.open(source);
        check(raw_session.text() == bytes && raw_session.encoding() == notepad::Encoding::utf8,
              "Default Session open remains byte-faithful with no BOM decoding");
        bool legacy_refused = false;
        try { static_cast<void>(notepad::decode(bytes)); }
        catch (const std::exception &) { legacy_refused = true; }
        check(legacy_refused, "Existing GUI decoder still refuses invisible legacy controls");
        session.replace_ranges({{0, 1}}, "Changed", session.stamp());
        const std::string changed = "Changed" + original.substr(1);
        check(session.undo() && session.text() == original, "Decoded edit undo uses UTF-8 source offsets");
        check(session.redo() && session.text() == changed, "Decoded edit redo retains source");
        session.save();
        check(notepad::read_file(source).bytes ==
                  notepad::encode(changed, encoding, notepad::TextControls::preserve) &&
                  !session.dirty() && !session.undo(),
              "Save preserves BOM/byte order and establishes the decoded undo baseline");
        session.restore_opened();
        check(session.text() == original && session.dirty(), "Decoded as-opened baseline survives save");
        check(session.undo() && session.text() == changed, "Restore-opened is undoable after save");
        session.restore_opened();
        session.save_as(copy);
        check(notepad::read_file(copy).bytes == bytes && session.path() == copy && !session.dirty(),
              "Decoded Save As preserves encoding and exact original bytes");
        session.replace_ranges({{0, 1}}, "New", session.stamp());
        const swiftedit::DocumentStamp before_failure = session.stamp();
        bool collision_refused = false;
        try { session.save_as(source); }
        catch (const std::exception &) { collision_refused = true; }
        check(collision_refused && session.path() == copy && session.encoding() == encoding &&
                  session.stamp().identity == before_failure.identity &&
                  session.stamp().revision == before_failure.revision && session.undo(),
              "Failed encoded Save As preserves path, encoding, revision and undo");
        session.replace_ranges({{0, 1}}, "Again", session.stamp());
        raw(copy, "external");
        bool conflict_refused = false;
        try { session.save(); }
        catch (const std::exception &) { conflict_refused = true; }
        check(conflict_refused && notepad::read_file(copy).bytes == "external" &&
                  session.encoding() == encoding && session.dirty() && session.undo(),
              "Encoded save checks the original encoded snapshot and preserves undo on conflict");
        const std::filesystem::path malformed = dir / ("malformed-" + std::to_string(index));
        raw(malformed, std::string_view("\xff\xfe\x00\xd8", 4));
        const swiftedit::DocumentStamp before_open = session.stamp();
        bool malformed_refused = false;
        try { session.open_decoded(malformed); }
        catch (const std::exception &) { malformed_refused = true; }
        check(malformed_refused && session.path() == copy && session.text() == original &&
                  session.encoding() == encoding && session.stamp().identity == before_open.identity &&
                  session.stamp().revision == before_open.revision,
              "Failed decoding leaves the existing document lifetime and all baselines intact");
        session.open(malformed);
        check(session.encoding() == notepad::Encoding::utf8 && session.text().size() == 4 &&
                  session.illegal_bytes() != 0,
              "Explicit raw reopen preserves malformed encoded bytes and resets the output codec");
    }
    swiftedit::Session retained{};
    retained.replace_ranges({{0, 0}}, "Keep", retained.stamp());
    const swiftedit::DocumentStamp stamp = retained.stamp();
    const std::filesystem::path threshold = dir / "decoded-threshold";
    raw(threshold, std::string(swiftedit::editable_limit, 'x'));
    std::unique_ptr<swiftedit::DecodedPagedFile> prepared = std::make_unique<swiftedit::DecodedPagedFile>(threshold);
    (*prepared).step();
    check((*prepared).state() == swiftedit::DecodedFileState::preparing && retained.text() == "Keep",
          "Partial decoded preparation leaves the current document intact");
    bool pending_refused = false;
    try { retained.adopt_decoded_read_only(prepared, stamp); }
    catch (const std::exception &) { pending_refused = true; }
    check(pending_refused && prepared && retained.text() == "Keep",
          "Pending source cannot replace the current Session");
    while ((*prepared).state() == swiftedit::DecodedFileState::preparing)
        (*prepared).step();
    retained.replace_ranges({{0, 4}}, "Changed", retained.stamp());
    bool stale_refused = false;
    try { retained.adopt_decoded_read_only(prepared, stamp); }
    catch (const std::exception &) { stale_refused = true; }
    check(stale_refused && prepared && retained.text() == "Changed",
          "Stale decoded adoption preserves prepared source and current edits");
    retained.adopt_decoded_read_only(prepared, retained.stamp());
    check(!prepared && retained.read_only() && retained.size() == swiftedit::editable_limit &&
              retained.page(swiftedit::editable_limit - 3, 3).bytes == "xxx",
          "Decoded threshold adopts a paged read-only source");
    retained.reset();
    std::string encoded_threshold(swiftedit::editable_limit, '\0');
    encoded_threshold[0] = '\xfe';
    encoded_threshold[1] = '\xff';
    for (std::size_t index = 3; index < encoded_threshold.size(); index += 2)
        encoded_threshold[index] = 'x';
    raw(threshold, encoded_threshold);
    retained.open_decoded(threshold);
    check(retained.read_only() && retained.size() == swiftedit::editable_limit / 2 - 1 &&
              retained.encoding() == notepad::Encoding::utf16_be &&
              retained.page(retained.size() - 3, 3).bytes == "xxx",
          "Encoded-size threshold stays read-only even when UTF-16 decodes to smaller text");
    retained.reset();
    // CJK expands from two UTF-16 bytes to three UTF-8 bytes. One ASCII scalar
    // makes the decoded result exactly the read-only threshold, while the file
    // itself remains below it. The boundary is checked before baseline copies.
    std::string expanded = "\xff\xfe";
    expanded += 'a';
    expanded += '\0';
    for (std::size_t index = 0; index < swiftedit::editable_limit / 3; ++index) {
        expanded += '\0';
        expanded += '\x4e';
    }
    raw(threshold, expanded);
    retained.open_decoded(threshold);
    check(retained.read_only() && retained.size() == swiftedit::editable_limit &&
              retained.encoding() == notepad::Encoding::utf16_le &&
              retained.page(retained.size() - 3, 3).bytes == "\xe4\xb8\x80",
          "UTF-16 expansion at the threshold opens logical UTF-8 pages read-only");
    bool edit_refused = false;
    try { retained.replace_ranges({{0, 1}}, "B", retained.stamp()); }
    catch (const std::exception &) { edit_refused = true; }
    check(edit_refused && !retained.dirty(), "Decoded read-only source refuses editing");
    swiftedit::SessionCopy copied(retained, retained.size() - 6, 6);
    while (copied.state() == swiftedit::CopyState::running)
        copied.step(retained, 1);
    const swiftedit::SourceClipboard decoded_clipboard = copied.take();
    check(decoded_clipboard.bytes() == "\xe4\xb8\x80\xe4\xb8\x80",
          "Read-only copy assembles logical Unicode across single-byte page boundaries");
    retained.reset();
    const std::string small_utf16 = notepad::encode("A", notepad::Encoding::utf16_le);
    raw(threshold, small_utf16);
    retained.open_decoded(threshold);
    retained.replace_ranges({{0, 1}}, std::string(swiftedit::editable_limit / 2 - 1, 'x'), retained.stamp());
    const swiftedit::DocumentStamp before_save = retained.stamp();
    bool output_refused = false;
    try { retained.save(); }
    catch (const std::exception &) { output_refused = true; }
    check(output_refused && notepad::read_file(threshold).bytes == small_utf16 &&
              retained.encoding() == notepad::Encoding::utf16_le && retained.dirty() &&
              retained.stamp().revision == before_save.revision && retained.undo() && retained.text() == "A",
          "Encoded output at the read-only threshold preserves disk bytes, baseline, encoding and undo");
}
int main() {
    try {
        using namespace swiftedit;
        for (std::size_t pair = 0; pair < 65536; ++pair) {
            std::string source{};
            source += static_cast<char>(pair / 256);
            source += static_cast<char>(pair % 256);
            std::size_t invalid = 0;
            const std::string expected = text_copy(source, &invalid);
            TextCopyStream stream{};
            TextCopyChunk first = stream.append(std::string_view(source).substr(0, 1));
            const TextCopyChunk last = stream.append(std::string_view(source).substr(1), true);
            first.bytes += last.bytes;
            check(first.bytes == expected && first.invalid_bytes + last.invalid_bytes == invalid,
                  "Streaming text copy agrees for every split two-byte input");
        }
        const std::string sequences = "A\xf0\x9f\x98\x80\xe7\x95\x8c\xcc\x81\r\n"
                                      "\xed\xa0\x80\xf4\x90\x80\x80\xf0\x9f";
        for (std::size_t width = 1; width <= sequences.size(); ++width) {
            TextCopyStream stream{};
            std::string actual{};
            std::size_t invalid = 0;
            for (std::size_t offset = 0; offset < sequences.size(); offset += width) {
                const TextCopyChunk part = stream.append(std::string_view(sequences).substr(offset, width));
                actual += part.bytes;
                invalid += part.invalid_bytes;
            }
            const TextCopyChunk tail = stream.append({}, true);
            actual += tail.bytes;
            invalid += tail.invalid_bytes;
            std::size_t expected_invalid = 0;
            check(actual == text_copy(sequences, &expected_invalid) && invalid == expected_invalid,
                  "Streaming copy preserves split Unicode and sanitizes invalid and truncated sequences");
            bool rejected = false;
            try { static_cast<void>(stream.append("x")); }
            catch (const std::exception &) { rejected = true; }
            check(rejected, "Finished copy streams reject further input");
        }
        TextCopyStream bounded{};
        static_cast<void>(bounded.append("\xf0\x9f"));
        bool oversized_rejected = false;
        try { static_cast<void>(bounded.append(std::string(maximum_page + 1, 'x'))); }
        catch (const std::exception &) { oversized_rejected = true; }
        const TextCopyChunk recovered = bounded.append("\x98\x80", true);
        check(oversized_rejected && recovered.bytes == "\xf0\x9f\x98\x80" && !recovered.invalid_bytes,
              "Rejected oversized input preserves pending UTF-8 state");
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() /
            ("swiftedit-session-" + std::to_string(test_process_id()));
        const bool observed_5 = std::filesystem::create_directory(dir);
        check(observed_5, "unique fixture");
        struct Cleanup {
            std::filesystem::path dir{};
            ~Cleanup() {
                std::error_code ec{};
                std::filesystem::remove_all(dir, ec);
            }
        } cleanup{dir};
        const std::filesystem::path copy_target = dir / "streamed.txt";
        {
            notepad::NewFileWriter cancelled(copy_target);
            cancelled.append("unfinished");
            check(!std::filesystem::exists(copy_target), "Streaming output remains unpublished");
        }
        check(std::filesystem::is_empty(dir), "Cancelling removes the owned temporary");
        {
            notepad::NewFileWriter writer(copy_target);
            const std::string chunk(65536, 'q');
            for (std::size_t index = 0; index < 257; ++index)
                writer.append(chunk);
            writer.append("tail");
            writer.publish();
            bool repeat_refused = false;
            try { writer.publish(); }
            catch (const std::exception &) { repeat_refused = true; }
            check(repeat_refused, "Published writers reject repeated publication");
        }
        {
            PagedFile copied(copy_target);
            check(copied.size() == 257 * 65536 + 4 &&
                  copied.page(copied.size() - 4, 4).bytes == "tail" && copied.page(0, 4).bytes == "qqqq",
                  "Streaming publication supports copies larger than the editable limit");
        }
        bool existing_refused = false;
        try { notepad::NewFileWriter existing(copy_target); }
        catch (const std::exception &) { existing_refused = true; }
        check(existing_refused, "Streaming writer refuses an existing destination");
        const std::filesystem::path raced_target = dir / "raced.txt";
        {
            notepad::NewFileWriter raced(raced_target);
            raced.append("candidate");
            raw(raced_target, "winner");
            bool race_refused = false;
            try { raced.publish(); }
            catch (const std::exception &) { race_refused = true; }
            check(race_refused, "Publication refuses a destination created after preparation");
        }
        check(notepad::read_file(raced_target).bytes == "winner", "Raced destination remains unchanged");
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(dir))
            check(entry.path() == copy_target || entry.path() == raced_target,
                  "Successful and refused publication leave no temporary files");
        Session paged_copy_source{};
        paged_copy_source.open(copy_target);
        const DocumentStamp copy_stamp = paged_copy_source.stamp();
        const std::filesystem::path task_target = dir / "task-copy.txt";
        {
            SessionTextCopy task(paged_copy_source, task_target);
            check(!task.step(paged_copy_source, 1) && task.offset() == 1,
                  "Text copy task performs a bounded read");
        }
        check(!std::filesystem::exists(task_target), "Cancelled copy task does not publish output");
        paged_copy_source.save_text_copy(task_target);
        {
            PagedFile copied(task_target);
            check(copied.size() == paged_copy_source.size() && copied.page(copied.size() - 4, 4).bytes == "tail",
                  "Session Save Text Copy supports paged sources");
        }
        check(paged_copy_source.identity() == copy_stamp.identity &&
              paged_copy_source.revision() == copy_stamp.revision && !paged_copy_source.dirty(),
              "Copy publication preserves the source session");
        const std::filesystem::path background_target = dir / "background-copy.txt";
        {
            SessionTextCopy task(paged_copy_source, background_target);
            while (!task.step(paged_copy_source)) {}
#ifndef _WIN32
            sigset_t before_mask{}, after_mask{};
            check(pthread_sigmask(SIG_SETMASK, nullptr, &before_mask) == 0, "Capture caller signal mask");
#endif
            task.begin_publication(paged_copy_source);
#ifndef _WIN32
            check(pthread_sigmask(SIG_SETMASK, nullptr, &after_mask) == 0, "Capture restored caller signal mask");
            for (int signal = 1; signal < NSIG; ++signal)
                check(sigismember(&before_mask, signal) == sigismember(&after_mask, signal),
                      "Starting publication preserves the caller signal mask");
#endif
            check(task.state() == TextCopyState::publishing, "Publication transfers to owned worker");
            while (!task.publication_ready(std::chrono::milliseconds(8))) {}
            task.finish_publication();
            check(task.state() == TextCopyState::published && std::filesystem::exists(background_target),
                  "Background publication reports a completed file");
        }
        const std::filesystem::path joined_target = dir / "joined-copy.txt";
        {
            SessionTextCopy task(paged_copy_source, joined_target);
            while (!task.step(paged_copy_source)) {}
            task.begin_publication(paged_copy_source);
        }
        check(std::filesystem::exists(joined_target), "Publication owner joins work before destruction");
        const std::filesystem::path failed_background = dir / "failed-background.txt";
        {
            SessionTextCopy task(paged_copy_source, failed_background);
            while (!task.step(paged_copy_source)) {}
            raw(failed_background, "competing file");
            task.begin_publication(paged_copy_source);
            while (!task.publication_ready(std::chrono::milliseconds(8))) {}
            bool failed = false;
            try { task.finish_publication(); }
            catch (const std::exception &) { failed = true; }
            check(failed && task.state() == TextCopyState::failed,
                  "Worker publication failures reach the task owner");
        }
        check(notepad::read_file(failed_background).bytes == "competing file",
              "Worker publication failure preserves competing destination");
        const std::filesystem::path independent_output = dir / "independent-copy.txt";
        {
            Session source{};
            source.replace_ranges({{0, 0}}, "prepared snapshot", source.stamp());
            SessionTextCopy task(source, independent_output);
            check(task.step(source), "Independent publication fixture is prepared");
            task.begin_publication(source);
            source.reset();
            while (!task.publication_ready(std::chrono::milliseconds(8))) {}
            task.finish_publication();
        }
        check(notepad::read_file(independent_output).bytes == "prepared snapshot",
              "Publication worker owns its prepared file independently of Session lifetime");
        const std::filesystem::path stale_target = dir / "stale-copy.txt";
        {
            Session changing{};
            changing.replace_ranges({{0, 0}}, "before", changing.stamp());
            SessionTextCopy task(changing, stale_target);
            check(task.step(changing), "Small copy reaches ready state");
            changing.replace_ranges({{0, 6}}, "after", changing.stamp());
            bool stale_rejected = false;
            try { task.publish(changing); }
            catch (const std::exception &) { stale_rejected = true; }
            check(stale_rejected && task.state() == TextCopyState::failed &&
                  !std::filesystem::exists(stale_target), "Stale ready copy cannot publish");
        }
        const std::filesystem::path file = dir / "sample.txt";
        raw(file, "before old after\r\nsecond old after");
        Session s{};
        Session other{};
        const DocumentStamp initial_stamp = s.stamp();
        check(initial_stamp.identity.value != 0 && other.identity() != s.identity(),
              "Independent sessions have distinct nonzero document identities");
        check(other.revision() == s.revision(), "Cross-session test has matching revisions");
        bool foreign_refused = false;
        try {
            other.replace_ranges({{0, 0}}, "foreign", initial_stamp);
        } catch (const std::exception &) {
            foreign_refused = true;
        }
        check(foreign_refused && other.text().empty(), "Foreign document stamp refused intact");
        other.replace_ranges({{0, 0}}, "local", other.stamp());
        const DocumentIdentity before_reset = other.identity();
        other.reset();
        check(other.identity() != before_reset, "Reset starts a distinct document lifetime");
        s.open(file);
        check(s.identity() != initial_stamp.identity, "Open starts a distinct document lifetime");
        const DocumentIdentity opened_identity = s.identity();
        const DocumentRevision opened_revision = s.revision();
        bool missing_refused = false;
        try {
            s.open(dir / "missing.txt");
        } catch (const std::exception &) {
            missing_refused = true;
        }
        check(missing_refused && s.identity() == opened_identity && s.revision() == opened_revision,
              "Failed open preserves document identity and revision");
        check(!s.dirty(), "clean open");
        std::vector<Preview> p = s.preview("", "old", " after", "new");
        check(p.size() == 2, "ambiguity enumerated");
        s.commit(p[1].token, p[1].revision);
        check(s.text() == "before old after\r\nsecond new after", "selected exact occurrence");
        {
            bool refused = false;
            try {
                s.commit(p[0].token, p[0].revision);
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        const bool undone = s.undo();
        check(undone && s.text() == "before old after\r\nsecond old after", "undo exact bytes");
        const bool redone = s.redo();
        check(redone, "redo");
        s.save();
        check(s.identity() == opened_identity, "Editing, undo, redo and save preserve identity");
        const bool undo_after_save = s.undo();
        check(!undo_after_save && !s.dirty(), "successful save resets history");
        s.restore_opened();
        check(s.dirty() && s.text() == "before old after\r\nsecond old after",
              "opened baseline survives save");
        const bool restore_undone = s.undo();
        check(restore_undone && !s.dirty(), "restore itself undoable");
        const DocumentRevision revision = s.revision();
        {
            bool refused = false;
            try {
                static_cast<void>(s.preview("", "new", "", "\r\rL1\r\r"));
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        check(s.revision() == revision, "marker refusal atomic");
        p = s.preview("", "new", "", "changed");
        s.commit(p[0].token, p[0].revision);
        raw(file, "external");
        {
            bool refused = false;
            try {
                s.save();
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        check(s.dirty(), "failed save retains undo");
        const bool failed_save_undone = s.undo();
        check(failed_save_undone, "failed save retains undo");
        const notepad::FileSnapshot observed_1 = notepad::read_file(file);
        check(observed_1.bytes == "external", "external bytes preserved");
        const std::filesystem::path bad = dir / "bad.bin";
        std::string source = "A";
        source += static_cast<char>(0xff);
        source += "B\r\rC";
        raw(bad, source);
        s.open(bad);
        check(s.text() == source && s.illegal_bytes() == 1, "invalid source byte fidelity");
        {
            bool refused = false;
            try {
                s.save();
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        const std::filesystem::path copy = dir / "copy.txt";
        s.save_text_copy(copy);
        const notepad::FileSnapshot observed_2 = notepad::read_file(copy);
        check(observed_2.bytes == "A B\r\rC", "one illegal byte one space");
        const notepad::FileSnapshot observed_3 = notepad::read_file(bad);
        check(observed_3.bytes == source && !s.dirty(), "copy preserves original and state");
        {
            bool refused = false;
            try {
                s.save_text_copy(copy);
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        p = s.preview("A", std::string(1, static_cast<char>(0xff)), "B", "valid");
        s.commit(p[0].token, p[0].revision);
        s.save();
        const notepad::FileSnapshot observed_4 = notepad::read_file(bad);
        check(observed_4.bytes == "AvalidB\r\rC",
              "repair invalid bytes with source controls preserved");
        std::string all{};
        for (unsigned i = 0; i < 256; ++i)
            all += static_cast<char>(i);
        const std::string escaped_all = escape_field(all);
        const std::string roundtrip = unescape_field(escaped_all);
        check(roundtrip == all, "transport all bytes roundtrip");
        const std::string calculated_1 = escape_field("\x1b[31m");
        check(calculated_1 == "\\x1b[31m", "terminal controls escaped");
        {
            bool refused = false;
            try {
                static_cast<void>(unescape_field("\\xq0"));
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        const std::string calculated_2 = text_copy(std::string("\xc0\x80", 2));
        check(calculated_2 == "  ", "overlong sequence illegal per byte");
        const std::string calculated_3 = text_copy("e\xcc\x81\xf0\x9f\x98\x80");
        check(calculated_3 == "e\xcc\x81\xf0\x9f\x98\x80", "Unicode preserved");
        const std::string calculated_4 = normalize_newlines("a\r\nb\nc\rd", "\n");
        check(calculated_4 == "a\nb\nc\nd", "explicit normalization preserves missing EOF newline");
        const std::string calculated_5 = suggested_name("[notes.md]\r\nhello");
        check(calculated_5 == "notes.md", "bracket name");
        const std::string calculated_6 = suggested_name("[CON.txt]");
        check(calculated_6 == "Untitled.txt", "reserved name");
        const std::string calculated_7 = suggested_name("[../oops]");
        check(calculated_7 == "Untitled.txt", "path not filename");
        const std::filesystem::path calculated_8 = versioned_name("report.csv");
        check(calculated_8 == std::filesystem::path("report.1.csv"), "version suffix");
        const std::filesystem::path calculated_9 = versioned_name("report.9.csv");
        check(calculated_9 == std::filesystem::path("report.10.csv"), "version increment");
        const std::filesystem::path big = dir / "large.txt";
        {
            std::ofstream f(big, std::ios::binary);
            f.seekp(editable_limit - 1);
            f.put('Z');
        }
        s.open(big);
        check(s.read_only() && s.size() == editable_limit, "exact large threshold");
        const Page final_page = s.page(editable_limit - 1, 16);
        check(final_page.bytes == "Z", "bounded final page");
        {
            bool refused = false;
            try {
                static_cast<void>(s.preview("", "Z", "", "z"));
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                s.save();
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                static_cast<void>(s.page(0, maximum_page + 1));
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        const std::filesystem::path small = dir / "nearly-large.txt";
        raw(small, std::string(editable_limit - 1, 'x'));
        s.open(small);
        check(!s.read_only() && s.size() == editable_limit - 1, "below threshold editable");
        {
            bool refused = false;
            try {
                static_cast<void>(s.preview("", std::string(editable_limit - 1, 'x'), "",
                                            std::string(editable_limit, 'y')));
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        verify_decoded_sessions(dir);
        std::cout << "Session tests passed: exact previews, stale guard, ambiguity, save undo "
                     "boundary, original restore, malformed bytes, copy safety, escaped transport, "
                     "version names, bounded large pages.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
