#include "session.hpp"
#include "session_text_copy.hpp"
#include "session_replace.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace swiftedit {
std::size_t utf8_sequence_length(std::string_view s, std::size_t i) {
    if (i >= s.size())
        return 0;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 128)
        return 1;
    std::size_t n = c >= 0xc2 && c <= 0xdf   ? 2
                    : c >= 0xe0 && c <= 0xef ? 3
                    : c >= 0xf0 && c <= 0xf4 ? 4
                                             : 0;
    if (!n || n > s.size() - i)
        return 0;
    for (std::size_t j = 1; j < n; ++j)
        if ((static_cast<unsigned char>(s[i + j]) & 0xc0) != 0x80)
            return 0;
    const unsigned char second = static_cast<unsigned char>(s[i + 1]);
    if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
        (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90))
        return 0;
    return n;
}
namespace {
DocumentIdentity allocate_identity() {
    // Process-wide identity allocation permits independent sessions on separate
    // threads. Session content itself remains confined to its owning thread.
    static std::atomic<std::uint64_t> next{1};
    std::uint64_t value = next.load(std::memory_order_relaxed);
    for (;;) {
        if (value == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Document identity space exhausted.");
        const bool acquired =
            next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed);
        if (acquired) {
            const DocumentIdentity identity{value};
            return identity;
        }
    }
}
void payload(std::string_view s) {
    if (s.find("\r\r") != s.npos)
        throw std::runtime_error(
            "Line markers are read-only metadata and must not be included in document text.");
    std::size_t bad{};
    for (std::size_t offset = 0; offset < s.size();) {
        const std::size_t length = utf8_sequence_length(s, offset);
        if (length == 0) {
            ++bad;
            ++offset;
        } else
            offset += length;
    }
    if (bad)
        throw std::runtime_error("Replacement must be valid UTF-8.");
}
void budget(std::size_t n) {
    if (!n || n > maximum_page)
        throw std::runtime_error("Page budget must be 1..65536 bytes.");
}
notepad::FileSnapshot write_session_text(const std::filesystem::path &path,
    const std::string_view text, const notepad::Encoding encoding,
    const notepad::FileSnapshot &expected) {
    std::string encoded{};
    std::string_view bytes = text;
    if (encoding != notepad::Encoding::utf8) {
        encoded = notepad::encode(text, encoding, notepad::TextControls::preserve);
        bytes = encoded;
    }
    if (bytes.size() >= editable_limit)
        throw std::runtime_error("Encoded output reaches the read-only file threshold. No file was changed.");
    notepad::FileSnapshot written = notepad::write_file(path, bytes, expected);
    return written;
}
} // namespace
std::string text_copy(std::string_view s, std::size_t *invalid) {
    std::string out{};
    out.reserve(s.size());
    std::size_t bad{};
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t n = utf8_sequence_length(s, i);
        if (n) {
            out.append(s.substr(i, n));
            i += n;
        } else {
            out += ' ';
            ++i;
            ++bad;
        }
    }
    if (invalid)
        *invalid = bad;
    return out;
}
Session::Session() {
    undo_.reserve(257);
    redo_.reserve(257);
    previews_.reserve(100);
    identity_ = allocate_identity();
}
void Session::open(const std::filesystem::path &source) {
    std::filesystem::path path = std::filesystem::absolute(source);
    std::unique_ptr<PagedFile> paged = std::make_unique<PagedFile>(path);
    if ((*paged).size() >= editable_limit) {
        reset();
        path_ = std::move(path);
        large_ = std::move(paged);
        return;
    }
    paged.reset();
    notepad::FileSnapshot snapshot = notepad::read_file(path);
    if (!snapshot.exists)
        throw std::runtime_error("File no longer exists.");
    if (snapshot.bytes.size() >= editable_limit)
        throw std::runtime_error("File grew; reopen it in read-only mode.");
    // Allocate all replacement copies before discarding the previous session.
    std::string text = snapshot.bytes;
    std::string saved = snapshot.bytes;
    std::string opened = snapshot.bytes;
    reset();
    path_ = std::move(path);
    snapshot_ = std::move(snapshot);
    text_ = std::move(text);
    saved_ = std::move(saved);
    opened_ = std::move(opened);
}
void Session::reset() {
    const DocumentIdentity next_identity = allocate_identity();
    large_.reset();
    decoded_large_.reset();
    path_.clear();
    snapshot_ = {};
    encoding_ = notepad::Encoding::utf8;
    text_.clear();
    saved_.clear();
    opened_.clear();
    undo_.clear();
    redo_.clear();
    previews_.clear();
    ++revision_.value;
    identity_ = next_identity;
}
void Session::open_decoded(const std::filesystem::path &source) {
    std::filesystem::path path = std::filesystem::absolute(source);
    bool needs_index = false;
    {
        const PagedFile probe(path);
        const Page header = probe.page(0, 4);
        const bool utf16 = header.bytes.starts_with("\xff\xfe") || header.bytes.starts_with("\xfe\xff");
        needs_index = probe.size() >= editable_limit || (utf16 && probe.size() > (editable_limit / 3) * 2);
    }
    if (needs_index) {
        std::unique_ptr<DecodedPagedFile> prepared = std::make_unique<DecodedPagedFile>(path);
        while ((*prepared).state() == DecodedFileState::preparing)
            (*prepared).step();
        if ((*prepared).encoded_size() >= editable_limit || (*prepared).size() >= editable_limit) {
            adopt_decoded_read_only(prepared, stamp());
            return;
        }
    }
    notepad::FileSnapshot snapshot = notepad::read_file(path);
    if (!snapshot.exists)
        throw std::runtime_error("File no longer exists.");
    if (snapshot.bytes.size() >= editable_limit)
        throw std::runtime_error("File grew while opening; reopen it to prepare read-only pages.");
    notepad::Decoded decoded = notepad::decode(snapshot.bytes, notepad::TextControls::preserve);
    if (decoded.text.size() >= editable_limit) {
        std::unique_ptr<DecodedPagedFile> prepared = std::make_unique<DecodedPagedFile>(path);
        while ((*prepared).state() == DecodedFileState::preparing)
            (*prepared).step();
        adopt_decoded_read_only(prepared, stamp());
        return;
    }
    // Complete all decoding and baseline allocations before replacing the
    // current session. snapshot keeps the actual encoded bytes for race checks.
    std::string saved = decoded.text;
    std::string opened = decoded.text;
    reset();
    path_ = std::move(path);
    snapshot_ = std::move(snapshot);
    encoding_ = decoded.encoding;
    text_ = std::move(decoded.text);
    saved_ = std::move(saved);
    opened_ = std::move(opened);
}
void Session::adopt_decoded_read_only(std::unique_ptr<DecodedPagedFile> &prepared, const DocumentStamp observed) {
    if (!(observed.identity == identity_) || !(observed.revision == revision_))
        throw std::runtime_error("The document changed while decoded opening was pending.");
    if (!prepared || (*prepared).state() != DecodedFileState::ready ||
        ((*prepared).encoded_size() < editable_limit && (*prepared).size() < editable_limit))
        throw std::runtime_error("A complete large decoded source is required.");
    std::filesystem::path path = (*prepared).path();
    // Recheck the retained file before publication; POSIX detects changed
    // metadata and Windows retains its deny-write-sharing handle.
    const Page checked = (*prepared).page((*prepared).size(), 1);
    static_cast<void>(checked);
    const notepad::Encoding encoding = (*prepared).encoding();
    reset();
    path_ = std::move(path);
    encoding_ = encoding;
    decoded_large_ = std::move(prepared);
}
void Session::editable() const {
    if (read_only())
        throw std::runtime_error("Files at or above 16 MiB are read-only.");
}
Page Session::page(std::uint64_t offset, std::size_t n) const {
    budget(n);
    if (decoded_large_) {
        Page result = (*decoded_large_).page(offset, n);
        return result;
    }
    if (large_) {
        Page result = (*large_).page(offset, n);
        return result;
    }
    if (offset > text_.size())
        throw std::runtime_error("Page offset exceeds document size.");
    std::string bytes = text_.substr(static_cast<std::size_t>(offset), n);
    Page result{offset, offset + bytes.size(), text_.size(), std::move(bytes)};
    return result;
}
std::vector<std::size_t> Session::find(std::string_view query, std::size_t maximum) const {
    editable();
    if (query.empty() || !maximum || maximum > 1000)
        throw std::runtime_error("Use a nonempty query and a match limit of 1..1000.");
    std::vector<std::size_t> hits{};
    hits.reserve(maximum);
    for (std::size_t i = 0; (i = text_.find(query, i)) != text_.npos && hits.size() < maximum; ++i)
        hits.push_back(i);
    return hits;
}
std::vector<Preview> Session::preview(std::string_view before, std::string_view old,
                                      std::string_view after, std::string_view replacement) {
    editable();
    payload(replacement);
    if (before.empty() && old.empty() && after.empty() && !text_.empty())
        throw std::runtime_error("An edit needs exact source context.");
    if (replacement.size() >= editable_limit)
        throw std::runtime_error("Replacement exceeds editable limit.");
    if (before.size() > text_.size() || old.size() > text_.size() - before.size() ||
        after.size() > text_.size() - before.size() - old.size())
        throw std::runtime_error("Exact source context not found; no changes made.");
    const std::size_t context_bytes = before.size() + old.size() + after.size();
    std::string needle{};
    needle.reserve(context_bytes);
    needle.append(before);
    needle.append(old);
    needle.append(after);
    std::vector<Preview> result{};
    result.reserve(100);
    EditToken next_token = next_token_;
    // Context lengths are bounded by the current document above. These sums
    // fit size_t and remain invariant while collecting matches.
    const std::size_t preview_bytes_per_match = old.size() + replacement.size() + 160;
    const std::size_t resulting_text_bytes = text_.size() - old.size() + replacement.size();
    constexpr std::size_t preview_budget = 32 * 1024 * 1024;
    for (std::size_t i = 0;; ++i) {
        const std::size_t found = text_.find(needle, i);
        if (found == text_.npos)
            break;
        if (result.size() == 100 || result.size() + 1 > preview_budget / preview_bytes_per_match)
            throw std::runtime_error("Preview is too broad; provide more exact context.");
        if (resulting_text_bytes >= editable_limit)
            throw std::runtime_error("Edit would reach the 16 MiB read-only threshold.");
        const std::size_t at = found + before.size();
        const std::size_t lo = at > 80 ? at - 80 : 0;
        Preview candidate{};
        candidate.token = next_token;
        candidate.revision = revision_;
        candidate.offset = at;
        candidate.length = old.size();
        candidate.before = text_.substr(lo, at - lo);
        candidate.removed = old;
        candidate.inserted = replacement;
        candidate.after = text_.substr(at + old.size(), 80);
        result.push_back(std::move(candidate));
        ++next_token.value;
        if (needle.empty())
            break;
        i = found;
    }
    if (result.empty())
        throw std::runtime_error("Exact source context not found; no changes made.");
    std::vector<Preview> retained = result;
    previews_.swap(retained);
    next_token_ = next_token;
    return result;
}
std::unique_ptr<SessionReplacement> Session::prepare_replacement(SearchPattern pattern,
                                                                 std::string replacement,
                                                                 bool match_case) const {
    editable();
    // Validate only caller-inserted bytes. Existing source is retained by the
    // engine's immutable snapshot, including legal pre-existing CRCR content.
    payload(replacement);
    std::unique_ptr<SessionReplacement> prepared(
        new SessionReplacement(*this, std::move(pattern), std::move(replacement), match_case));
    return prepared;
}
std::size_t Session::commit_replacement(SessionReplacement &prepared) {
    editable();
    if (prepared.stamp_.identity != identity_ || prepared.stamp_.revision != revision_)
        throw std::runtime_error("Replacement belongs to an older document.");
    if (!prepared.ready_ || prepared.consumed_)
        throw std::runtime_error("Finish replacement preparation before its one-use commit.");
    PatternReplacement result = (*prepared.scan_).take_result();
    prepared.consumed_ = true;
    prepared.scan_.reset();
    const std::size_t count = result.count;
    if (count)
        change(std::move(result.text));
    return count;
}
void Session::change(std::string next) {
    editable();
    if (next == text_)
        return;
    // Copy the current baseline before pruning history: allocation failure
    // preserves both the document and its complete undo history.
    std::string previous = text_;
    // Bound snapshot history to 32 MiB; never persist it to disk.
    std::size_t used = text_.size();
    for (const std::string &entry : undo_)
        used += entry.size();
    while (!undo_.empty() && (used > 32 * 1024 * 1024 || undo_.size() >= 256)) {
        used -= undo_.front().size();
        undo_.erase(undo_.begin());
    }
    undo_.push_back(std::move(previous));
    text_ = std::move(next);
    redo_.clear();
    previews_.clear();
    ++revision_.value;
}
void Session::commit(EditToken token, DocumentRevision revision) {
    editable();
    if (revision != revision_)
        throw std::runtime_error("Stale preview: document revision changed.");
    std::vector<Preview>::const_iterator p = previews_.begin();
    while (p != previews_.end() && (*p).token != token) {
        ++p;
    }
    if (p == previews_.end())
        throw std::runtime_error("Unknown or expired preview token.");
    std::string next = text_;
    next.replace((*p).offset, (*p).length, (*p).inserted);
    change(std::move(next));
    previews_.clear();
}
void Session::replace_ranges(const std::vector<SourceRange> &ranges, std::string_view replacement,
                             DocumentStamp observed) {
    if (observed.identity != identity_)
        throw std::runtime_error("Stale edit: document identity changed.");
    replace_ranges(ranges, replacement, observed.revision);
}
void Session::replace_ranges(const std::vector<SourceRange> &ranges, std::string_view replacement,
                             DocumentRevision observed) {
    editable();
    if (observed != revision_)
        throw std::runtime_error("Stale edit: document revision changed.");
    if (ranges.empty() || ranges.size() > 1000)
        throw std::runtime_error("Select between 1 and 1000 source ranges.");
    payload(replacement);
    std::size_t previous_end = 0;
    std::size_t previous_offset = 0;
    std::size_t previous_length = 0;
    std::size_t total_removed = 0;
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        const SourceRange &range = ranges[index];
        if (range.offset > text_.size() || range.length > text_.size() - range.offset)
            throw std::runtime_error("Edit range exceeds document.");
        if (index && (range.offset < previous_end || range.offset == previous_offset ||
                      (range.offset == previous_end && (!range.length || !previous_length))))
            throw std::runtime_error("Edit ranges must be ordered and disjoint.");
        previous_offset = range.offset;
        previous_end = range.offset + range.length;
        previous_length = range.length;
        total_removed += range.length;
    }
    const std::size_t retained = text_.size() - total_removed;
    if (replacement.size() > (editable_limit - 1 - retained) / ranges.size())
        throw std::runtime_error("Edit would reach the 16 MiB read-only threshold.");
    const std::size_t resulting_size = retained + replacement.size() * ranges.size();
    std::string next{};
    next.reserve(resulting_size);
    std::size_t copied = 0;
    for (const SourceRange &range : ranges) {
        next.append(text_, copied, range.offset - copied);
        next.append(replacement);
        copied = range.offset + range.length;
    }
    next.append(text_, copied, text_.size() - copied);
    change(std::move(next));
}
SourceClipboard Session::copy_range(SourceRange range, DocumentStamp observed) const {
    editable();
    if (observed.identity != identity_ || observed.revision != revision_)
        throw std::runtime_error("Stale copy: document changed.");
    if (range.offset > text_.size() || range.length > text_.size() - range.offset)
        throw std::runtime_error("Copy range exceeds document.");
    SourceClipboard result{};
    result.bytes_ = text_.substr(range.offset, range.length);
    return result;
}
void Session::paste_range(SourceRange range, const SourceClipboard &clipboard,
                          DocumentStamp observed) {
    editable();
    if (observed.identity != identity_ || observed.revision != revision_)
        throw std::runtime_error("Stale paste: document changed.");
    if (range.offset > text_.size() || range.length > text_.size() - range.offset)
        throw std::runtime_error("Paste range exceeds document.");
    const std::size_t retained = text_.size() - range.length;
    if (clipboard.bytes_.size() > editable_limit - 1 - retained)
        throw std::runtime_error("Paste would reach the 16 MiB read-only threshold.");
    std::string next{};
    next.reserve(retained + clipboard.bytes_.size());
    next.append(text_, 0, range.offset);
    next.append(clipboard.bytes_);
    next.append(text_, range.offset + range.length, text_.size() - range.offset - range.length);
    change(std::move(next));
}
bool Session::undo() {
    editable();
    if (undo_.empty())
        return false;
    redo_.push_back(text_);
    text_ = std::move(undo_.back());
    undo_.pop_back();
    previews_.clear();
    ++revision_.value;
    return true;
}
bool Session::redo() {
    editable();
    if (redo_.empty())
        return false;
    undo_.push_back(text_);
    text_ = std::move(redo_.back());
    redo_.pop_back();
    previews_.clear();
    ++revision_.value;
    return true;
}
void Session::restore_opened() {
    editable();
    change(opened_);
}
std::size_t Session::illegal_bytes() const {
    editable();
    std::size_t n{};
    for (std::size_t offset = 0; offset < text_.size();) {
        const std::size_t length = utf8_sequence_length(text_, offset);
        if (length == 0) {
            ++n;
            ++offset;
        } else
            offset += length;
    }
    return n;
}
void Session::published(std::filesystem::path path, std::string saved,
                        notepad::FileSnapshot snapshot) {
    path_ = std::move(path);
    snapshot_ = std::move(snapshot);
    saved_ = std::move(saved);
    undo_.clear();
    redo_.clear();
    previews_.clear();
    ++revision_.value;
}
void Session::save() {
    editable();
    if (path_.empty())
        throw std::runtime_error("Untitled document requires save-as.");
    if (illegal_bytes())
        throw std::runtime_error("Illegal UTF-8 bytes remain. Edit them or use Save Text Copy.");
    std::filesystem::path destination = path_;
    std::string saved = text_;
    notepad::FileSnapshot written = write_session_text(destination, text_, encoding_, snapshot_);
    published(std::move(destination), std::move(saved), std::move(written));
}
void Session::save_as(const std::filesystem::path &target) {
    editable();
    if (illegal_bytes())
        throw std::runtime_error("Illegal UTF-8 bytes remain. Edit them or use Save Text Copy.");
    std::filesystem::path path = std::filesystem::absolute(target);
    std::string saved = text_;
    notepad::FileSnapshot written = write_session_text(path, text_, encoding_, {});
    published(std::move(path), std::move(saved), std::move(written));
}
void Session::save_text_copy(const std::filesystem::path &target) const {
    SessionTextCopy copy(*this, target);
    while (!copy.step(*this)) {}
    copy.publish(*this);
}
void Session::save_reviewed(const std::filesystem::path &target, std::string prepared,
    const notepad::FileSnapshot &expected, const notepad::Encoding encoding) {
    editable();
    if (illegal_bytes())
        throw std::runtime_error("Illegal UTF-8 bytes remain. Edit them or use Save Text Copy.");
    if (prepared.size() >= editable_limit)
        throw std::runtime_error("Reviewed text exceeds the editable budget. No file was changed.");
    std::filesystem::path path = target;
    std::string saved = prepared;
    notepad::FileSnapshot written = write_session_text(path, prepared, encoding, expected);
    text_ = std::move(prepared);
    encoding_ = encoding;
    published(std::move(path), std::move(saved), std::move(written));
}
std::string normalize_newlines(std::string_view s, std::string_view ending) {
    if (ending != "\n" && ending != "\r" && ending != "\r\n")
        throw std::runtime_error("Expected LF, CR or CRLF.");
    std::string result{};
    if (s.size() > result.max_size() / 2)
        throw std::length_error("Normalized text is too large.");
    result.reserve(s.size() * 2);
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\r' || s[i] == '\n') {
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')
                ++i;
            result += ending;
        } else
            result += s[i];
    return result;
}
std::string suggested_name(std::string_view s) {
    const std::string_view first = s.substr(0, s.find_first_of("\r\n"));
    if (first.size() < 3 || first.front() != '[' || first.back() != ']')
        return "Untitled.txt";
    const std::string name = std::string(first.substr(1, first.size() - 2));
    if (name.size() > 200 || name.find_first_of("<>:\"/\\|?*") != name.npos || name.back() == '.' ||
        name.back() == ' ')
        return "Untitled.txt";
    for (unsigned char c : name)
        if (c < 32)
            return "Untitled.txt";
    std::string base = name.substr(0, name.find('.'));
    for (char &c : base)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
        (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) &&
         base[3] >= '1' && base[3] <= '9'))
        return "Untitled.txt";
    return name;
}
std::filesystem::path versioned_name(const std::filesystem::path &path) {
    std::wstring stem = path.stem().wstring(), ext = path.extension().wstring();
    const std::size_t dot = stem.rfind(L'.');
    std::uint64_t number = 1;
    if (dot != stem.npos && dot + 1 < stem.size()) {
        const std::wstring suffix = stem.substr(dot + 1);
        bool digits = true;
        for (const wchar_t c : suffix) {
            if (c < L'0' || c > L'9') {
                digits = false;
                break;
            }
        }
        if (digits) {
            try {
                const unsigned long long old = std::stoull(suffix);
                if (old == UINT64_MAX)
                    throw std::runtime_error("Version number exhausted.");
                number = old + 1;
                stem.resize(dot);
            } catch (const std::out_of_range &) {
                throw std::runtime_error("Version number exhausted.");
            }
        }
    }
    const std::wstring filename = stem + L'.' + std::to_wstring(number) + ext;
    const std::filesystem::path result = path.parent_path() / filename;
    return result;
}
std::string escape_field(std::string_view s) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out{};
    if (s.size() > out.max_size() / 4)
        throw std::length_error("Escaped field is too large.");
    out.reserve(s.size() * 4);
    for (unsigned char c : s)
        if (c == '\\')
            out += "\\\\";
        else if (c < 32 || c >= 127) {
            out += "\\x";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += static_cast<char>(c);
    return out;
}
int hex(char c) {
    int digit = -1;
    if (c >= '0' && c <= '9')
        digit = c - '0';
    else if (c >= 'a' && c <= 'f')
        digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        digit = c - 'A' + 10;
    return digit;
}
std::string unescape_field(std::string_view s) {
    std::string out{};
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out += s[i];
            continue;
        }
        ++i;
        if (i == s.size())
            throw std::runtime_error("Incomplete escape.");
        switch (s[i]) {
        case '\\':
            out += '\\';
            break;
        case 'n':
            out += '\n';
            break;
        case 'r':
            out += '\r';
            break;
        case 't':
            out += '\t';
            break;
        case 'x':
            if (i + 2 >= s.size() || hex(s[i + 1]) < 0 || hex(s[i + 2]) < 0)
                throw std::runtime_error("Invalid hex escape.");
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
            break;
        default:
            throw std::runtime_error("Unknown escape.");
        }
    }
    return out;
}
} // namespace swiftedit
