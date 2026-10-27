#include "save_review.hpp"
#include <stdexcept>

namespace swiftedit {
SaveReview::SaveReview(std::filesystem::path original, DocumentStamp stamp,
                       notepad::Encoding encoding)
    : original_(std::filesystem::absolute(original)), stamp_(stamp), encoding_(encoding) {
    if (!stamp.identity.value || !stamp.revision.value)
        throw std::runtime_error("Save review requires a live document identity and revision.");
}
void SaveReview::choose(ConflictChoice choice) {
    if (consumed_)
        throw std::runtime_error("This save review was already used.");
    ready_ = false;
    if (choice != ConflictChoice::save_over && choice != ConflictChoice::new_copy)
        throw std::runtime_error("Unknown conflict save choice.");
    std::filesystem::path target =
        choice == ConflictChoice::new_copy ? versioned_name(original_) : original_;
    target_ = std::move(target);
    choice_ = choice;
    observed_ = {};
    chosen_ = true;
    ready_ = false;
}
void SaveReview::review(const std::filesystem::path &target, notepad::Encoding encoding,
                        SaveEndings endings) {
    if (!chosen_ || consumed_)
        throw std::runtime_error("Choose Save Over or New Copy before reviewing the destination.");
    // Any failed attempt invalidates earlier consent rather than reusing it.
    ready_ = false;
    if ((encoding != notepad::Encoding::utf8 && encoding != notepad::Encoding::utf8_bom &&
         encoding != notepad::Encoding::utf16_le && encoding != notepad::Encoding::utf16_be) ||
        (endings != SaveEndings::preserve && endings != SaveEndings::lf &&
         endings != SaveEndings::crlf && endings != SaveEndings::cr))
        throw std::runtime_error("Unknown save encoding or line-ending choice.");
    if (target.empty())
        throw std::runtime_error("Choose a filename for the reviewed save.");
    std::filesystem::path absolute = std::filesystem::absolute(target);
    notepad::FileSnapshot observed = notepad::read_file(absolute);
    if (choice_ == ConflictChoice::new_copy && observed.exists)
        throw std::runtime_error("A new copy must use a filename that does not already exist.");
    target_ = std::move(absolute);
    observed_ = std::move(observed);
    encoding_ = encoding;
    endings_ = endings;
    ready_ = true;
}
void SaveReview::authorize(const DocumentStamp current) {
    if (!ready_ || consumed_)
        throw std::runtime_error("Review the destination and confirm Save before writing.");
    ready_ = false;
    if (current.identity != stamp_.identity || current.revision != stamp_.revision)
        throw std::runtime_error(
            "The document changed during save review. Review the new version.");
}
std::string SaveReview::prepare(const std::string_view source) const {
    std::string prepared{};
    switch (endings_) {
    case SaveEndings::preserve:
        prepared = source;
        break;
    case SaveEndings::lf:
        prepared = normalize_newlines(source, "\n");
        break;
    case SaveEndings::crlf:
        prepared = normalize_newlines(source, "\r\n");
        break;
    case SaveEndings::cr:
        prepared = normalize_newlines(source, "\r");
        break;
    default:
        throw std::runtime_error("Unknown line-ending choice.");
    }
    return prepared;
}
void SaveReview::publish(notepad::Document &document, std::string_view source,
                         DocumentStamp current) {
    authorize(current);
    const std::string prepared = prepare(source);
    document.save_encoded(target_, prepared, observed_, encoding_);
    consumed_ = true;
}
void SaveReview::publish(Session &session) {
    authorize(session.stamp());
    std::string prepared = prepare(session.text());
    session.save_reviewed(target_, std::move(prepared), observed_, encoding_);
    consumed_ = true;
}
} // namespace swiftedit
