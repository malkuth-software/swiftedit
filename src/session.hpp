#pragma once
#include "document.hpp"
#include "paged_file.hpp"
#include "decoded_paged_file.hpp"
#include <memory>
#include <vector>

namespace swiftedit {
class SearchPattern;
class SessionReplacement;
class SessionCopy;
class SaveReview;
constexpr std::size_t editable_limit = 16 * 1024 * 1024;
constexpr std::size_t paste_confirmation_bytes = 500000;
// Session-local identities are deliberately distinct from byte offsets and
// from one another. Transport conversion occurs only at the CLI boundary.
struct EditToken {
    std::uint64_t value{};
    bool operator==(const EditToken &other) const {
        const bool equal = value == other.value;
        return equal;
    }
};
struct DocumentRevision {
    std::uint64_t value{};
    bool operator==(const DocumentRevision &other) const {
        const bool equal = value == other.value;
        return equal;
    }
};
struct DocumentIdentity {
    std::uint64_t value{};
    bool operator==(const DocumentIdentity &other) const {
        const bool equal = value == other.value;
        return equal;
    }
};
struct DocumentStamp {
    DocumentIdentity identity{};
    DocumentRevision revision{};
};
struct Preview {
    EditToken token{};
    DocumentRevision revision{};
    std::size_t offset{}, length{};
    std::string before{}, removed{}, inserted{}, after{};
};
struct SourceRange {
    std::size_t offset{}, length{};
};
// Owned source bytes populated by Session or a completed SessionCopy range.
// Clipboard lifetime is independent of its source document.
class SourceClipboard {
public:
    SourceClipboard() = default;
    std::string_view bytes() const { return bytes_; }

private:
    friend class Session;
    friend class SessionCopy;
    std::string bytes_{};
};
// Byte-faithful command model. Malformed input remains editable; publication
// requires valid UTF-8 or an explicitly requested sanitized text copy. Explicit
// decoded opens use UTF-8 logical source with a separate output codec.
class Session {
public:
    Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    void open(const std::filesystem::path &);
    // Explicit compatibility path for decoded GUI documents. Uses the existing
    // BOM-aware decoder, refuses malformed input, and preserves the detected
    // encoding on save. Encoded or logical size >=16 MiB opens paged/read-only.
    // This convenience call is synchronous. UI callers prepare DecodedPagedFile
    // incrementally and adopt it instead. Default open remains byte-faithful;
    // offsets here describe decoded UTF-8, not encoded file bytes.
    void open_decoded(const std::filesystem::path &);
    // Adopt an incrementally prepared large decoded source only if this Session
    // still matches the observed stamp. Refusal preserves both owners. The
    // source must be ready and encoded or logical size must be >=16 MiB.
    void adopt_decoded_read_only(std::unique_ptr<DecodedPagedFile> &, DocumentStamp);
    void reset();
    [[nodiscard]] Page page(std::uint64_t offset = 0, std::size_t budget = 4096) const;
    [[nodiscard]] std::vector<std::size_t> find(std::string_view query,
                                                std::size_t maximum = 100) const;
    [[nodiscard]] std::vector<Preview> preview(std::string_view before, std::string_view old,
                                               std::string_view after,
                                               std::string_view replacement);
    void commit(EditToken token, DocumentRevision revision);
    // GUI/terminal edits use exact source ranges and an observed revision.
    // All ranges are validated before one undoable atomic change. Caller owns
    // the ranges and replacement through the synchronous call.
    void replace_ranges(const std::vector<SourceRange> &, std::string_view replacement,
                        DocumentRevision observed);
    // Deferred GUI work must match both document lifetime and revision.
    void replace_ranges(const std::vector<SourceRange> &, std::string_view replacement,
                        DocumentStamp observed);
    [[nodiscard]] SourceClipboard copy_range(SourceRange, DocumentStamp) const;
    void paste_range(SourceRange, const SourceClipboard &, DocumentStamp);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void restore_opened();
    [[nodiscard]] std::unique_ptr<SessionReplacement>
    prepare_replacement(SearchPattern, std::string replacement, bool match_case = false) const;
    std::size_t commit_replacement(SessionReplacement &);
    void save();
    void save_as(const std::filesystem::path &);              // new target only
    void save_text_copy(const std::filesystem::path &) const; // new target only
    [[nodiscard]] bool dirty() const {
        const bool changed = !large_ && !decoded_large_ && text_ != saved_;
        return changed;
    }
    [[nodiscard]] bool read_only() const {
        const bool large_file = static_cast<bool>(large_) || static_cast<bool>(decoded_large_);
        return large_file;
    }
    [[nodiscard]] DocumentRevision revision() const { return revision_; }
    [[nodiscard]] DocumentIdentity identity() const { return identity_; }
    [[nodiscard]] DocumentStamp stamp() const {
        const DocumentStamp result{identity_, revision_};
        return result;
    }
    [[nodiscard]] std::uint64_t size() const {
        if (decoded_large_)
            return (*decoded_large_).size();
        const std::uint64_t bytes =
            large_ ? (*large_).size() : static_cast<std::uint64_t>(text_.size());
        return bytes;
    }
    const std::filesystem::path &path() const { return path_; }
    const std::string &text() const { return text_; }
    // Output codec; raw open always uses utf8 and keeps any BOM in source bytes.
    [[nodiscard]] notepad::Encoding encoding() const { return encoding_; }
    [[nodiscard]] std::size_t illegal_bytes() const;

private:
    friend class SaveReview;
    // Only the authorized review path can supply a freshly observed overwrite
    // snapshot. All replacement state is allocated before publication.
    void save_reviewed(const std::filesystem::path &, std::string prepared,
                       const notepad::FileSnapshot &, notepad::Encoding);
    void editable() const;
    void change(std::string);
    void published(std::filesystem::path, std::string, notepad::FileSnapshot);
    std::filesystem::path path_{};
    std::unique_ptr<PagedFile> large_{};
    std::unique_ptr<DecodedPagedFile> decoded_large_{};
    notepad::FileSnapshot snapshot_{};
    notepad::Encoding encoding_{notepad::Encoding::utf8};
    std::string text_{}, saved_{}, opened_{};
    std::vector<std::string> undo_{}, redo_{};
    std::vector<Preview> previews_{};
    DocumentRevision revision_{1};
    DocumentIdentity identity_{};
    EditToken next_token_{1};
};
[[nodiscard]] std::size_t utf8_sequence_length(std::string_view, std::size_t offset);
[[nodiscard]] std::string text_copy(std::string_view, std::size_t *invalid = nullptr);
[[nodiscard]] std::string normalize_newlines(std::string_view, std::string_view ending);
[[nodiscard]] std::string suggested_name(std::string_view);
[[nodiscard]] std::filesystem::path versioned_name(const std::filesystem::path &);
// Transport escapes every control byte; terminal escape sequences never execute.
[[nodiscard]] std::string escape_field(std::string_view);
[[nodiscard]] std::string unescape_field(std::string_view);
} // namespace swiftedit
