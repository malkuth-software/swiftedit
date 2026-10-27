#pragma once
#include "session.hpp"

namespace swiftedit {
enum class ConflictChoice { save_over, new_copy };
enum class SaveEndings { preserve, lf, crlf, cr };
// UI owns this state between the first choice and final explicit Save click.
// Editing review fields must call review() again before displaying confirmation.
class SaveReview {
public:
    SaveReview(std::filesystem::path original, DocumentStamp, notepad::Encoding);
    void choose(ConflictChoice);
    void review(const std::filesystem::path &, notepad::Encoding, SaveEndings);
    void publish(notepad::Document &, std::string_view source, DocumentStamp current);
    void publish(Session &);
    const std::filesystem::path &target() const { return target_; }
    const notepad::FileSnapshot &observed() const { return observed_; }
    notepad::Encoding encoding() const { return encoding_; }
    SaveEndings endings() const { return endings_; }
    bool destructive() const { return observed_.exists; }
    bool ready() const { return ready_; }

private:
    void authorize(DocumentStamp);
    [[nodiscard]] std::string prepare(std::string_view) const;
    std::filesystem::path original_{}, target_{};
    DocumentStamp stamp_{};
    notepad::FileSnapshot observed_{};
    notepad::Encoding encoding_{};
    SaveEndings endings_{SaveEndings::preserve};
    ConflictChoice choice_{ConflictChoice::new_copy};
    bool chosen_{}, ready_{}, consumed_{};
};
} // namespace swiftedit
