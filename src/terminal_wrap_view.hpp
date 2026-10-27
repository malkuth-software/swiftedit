#pragma once
#include "terminal_wrap.hpp"
#include <stdexcept>

namespace swiftedit {
class TerminalWrapInterrupt final : public std::runtime_error {
public:
    TerminalWrapInterrupt()
        : std::runtime_error("Wrap cancelled. Ctrl+Home returns to start; next command retries.") {}
};
// Borrowed for synchronous calls only; must outlive the view. The probe must
// not mutate the buffer or reenter layout. No worker or retained source borrow.
class TerminalWrapControl {
public:
    virtual ~TerminalWrapControl() = default;
    virtual bool cancel_requested() = 0;
};
// Editable wrapped viewport. Retains only visible rows (at most 296), never a
// whole-document array of visual rows. Sparse source checkpoints cover up to
// 320 logical lines, spaced at least 4096 source bytes apart. Preparation scans
// only through the requested position; distant jumps still scan synchronously.
class TerminalWrapView {
public:
    explicit TerminalWrapView(TerminalWrapControl *control = nullptr) : control_(control) {}
    const std::vector<TerminalWrappedRow> &frame(TerminalBuffer &, std::size_t width,
                                                 std::size_t height);
    void move(TerminalBuffer &, TerminalMotion, bool extend, std::size_t count, std::size_t width);

private:
    void check_cancel(std::size_t source_bytes);
    struct Position {
        std::size_t line{}, offset{};
    };
    struct Entry {
        std::size_t line{}, last{}, frontier{};
        bool complete{};
        std::vector<std::size_t> starts{};
    };
    Entry &prepare(TerminalBuffer &, std::size_t line, std::size_t through);
    void extend(TerminalBuffer &, Entry &, std::size_t through);
    static std::size_t checkpoint(const Entry &, std::size_t offset, bool strictly_before);
    void synchronize(TerminalBuffer &, std::size_t width);
    Position locate(TerminalBuffer &);
    Position next(TerminalBuffer &, Position);
    Position previous(TerminalBuffer &, Position);
    static bool same(Position, Position);
    DocumentStamp stamp_{};
    std::size_t width_{}, expected_caret_{};
    std::size_t caret_row_{};
    Position top_{};
    bool current_{};
    std::optional<std::size_t> desired_column_{};
    std::vector<TerminalWrappedRow> rows_{};
    std::vector<Entry> entries_{};
    std::size_t next_eviction_{};
    TerminalWrapControl *control_{};
    std::size_t until_poll_{};
};
} // namespace swiftedit
