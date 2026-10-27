#include "document_projection.hpp"
#include <algorithm>
#include <cmath>

namespace swiftedit {
namespace gf = gui_forms;
namespace {
// DisplayPage units are ordered, contiguous source/display coverage. Identity
// spans may contain many graphemes; source selection still validates grapheme
// boundaries separately. Each generated label must retain its own atomic span.
std::size_t mapping_count(const std::vector<DisplayUnit> &units) {
    std::size_t count = 0;
    bool identity = false;
    for (const DisplayUnit &unit : units) {
        const bool text = unit.kind == DisplayKind::text;
        if (!text || !identity)
            ++count;
        identity = text;
    }
    return count;
}
}
DocumentProjection::DocumentProjection(const Session &session, gf::DocumentPageRequest request)
    : request_(request), stamp_(session.stamp()), size_(session.size()) {
    if (request.revision.document != stamp_.identity.value || request.revision.revision != stamp_.revision.value) {
        state_ = ProjectionState::stale;
        return;
    }
    const std::uint64_t begin = request.permitted.begin.value;
    const std::uint64_t end = request.permitted.end.value;
    if (!request.serial || begin > end || end > size_ || request.viewport.anchor.value < begin ||
        request.viewport.anchor.value > end || !std::isfinite(request.viewport.horizontal_dip) ||
        request.viewport.horizontal_dip < 0) {
        state_ = ProjectionState::invalid_request;
        return;
    }
    if (end - begin > gf::DocumentViewLimits::source_bytes) {
        state_ = ProjectionState::budget_exceeded;
        return;
    }
    source_.reserve(static_cast<std::size_t>(end - begin));
}
bool DocumentProjection::current(const Session &session) const {
    const DocumentStamp stamp = session.stamp();
    const bool equal = stamp.identity == stamp_.identity && stamp.revision == stamp_.revision && session.size() == size_;
    return equal;
}
void DocumentProjection::cancel() {
    std::string empty_source{};
    source_.swap(empty_source);
    gf::DocumentPage empty{};
    prepared_.swap(empty);
    state_ = ProjectionState::cancelled;
}
ProjectionState DocumentProjection::step(const Session &session) {
    if (state_ != ProjectionState::pending)
        return state_;
    if (!current(session)) {
        cancel();
        state_ = ProjectionState::stale;
        return state_;
    }
    const std::uint64_t begin = request_.permitted.begin.value;
    const std::uint64_t end = request_.permitted.end.value;
    if (!boundary_checked_) {
        // LF/CR boundaries isolate grapheme context, but CRLF is indivisible.
        bool complete = true;
        if (begin) {
            const Page edge = session.page(begin - 1, 2);
            complete = !edge.bytes.empty() && (edge.bytes[0] == '\n' || edge.bytes[0] == '\r');
            if (edge.bytes.size() == 2 && edge.bytes[0] == '\r' && edge.bytes[1] == '\n')
                complete = false;
        }
        if (end < size_) {
            if (end == begin)
                complete = false;
            else {
                const Page edge = session.page(end - 1, 2);
                const bool ending = !edge.bytes.empty() && (edge.bytes[0] == '\n' || edge.bytes[0] == '\r');
                const bool split = edge.bytes.size() == 2 && edge.bytes[0] == '\r' && edge.bytes[1] == '\n';
                complete = complete && ending && !split;
            }
        }
        if (!complete) {
            cancel();
            state_ = ProjectionState::context_required;
            return state_;
        }
        boundary_checked_ = true;
    }
    const std::uint64_t offset = begin + source_.size();
    if (offset < end) {
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(8192, end - offset));
        const Page page = session.page(offset, count);
        if (!current(session)) {
            cancel();
            state_ = ProjectionState::stale;
            return state_;
        }
        if (page.bytes.size() != count)
            throw std::runtime_error("Document projection source read was incomplete.");
        source_.append(page.bytes);
        return state_;
    }
    const DisplayPage display(source_);
    const std::size_t mappings = mapping_count(display.units());
    if (display.text().size() > gf::DocumentViewLimits::display_capacity ||
        mappings > gf::DocumentViewLimits::mapping_capacity) {
        cancel();
        state_ = ProjectionState::budget_exceeded;
        return state_;
    }
    gf::DocumentPage next{};
    next.request = request_;
    next.covered = request_.permitted;
    next.display_utf8 = display.text();
    next.mapping.reserve(mappings);
    for (const DisplayUnit &unit : display.units()) {
        gf::DocumentMapSpan map{};
        map.source = {gf::SourceByteOffset(begin + unit.source.offset),
                      gf::SourceByteOffset(begin + unit.source.offset + unit.source.length)};
        map.begin = gf::DisplayByteOffset(static_cast<std::uint32_t>(unit.display.offset));
        map.end = gf::DisplayByteOffset(static_cast<std::uint32_t>(unit.display.offset + unit.display.length));
        map.kind = unit.kind == DisplayKind::text ? gf::DocumentMapKind::identity_utf8 : gf::DocumentMapKind::atomic_token;
        if (map.kind == gf::DocumentMapKind::identity_utf8 && !next.mapping.empty() &&
            next.mapping.back().kind == gf::DocumentMapKind::identity_utf8) {
            gf::DocumentMapSpan &previous = next.mapping.back();
            previous.source.end = map.source.end;
            previous.end = map.end;
        } else
            next.mapping.push_back(map);
    }
    prepared_.swap(next);
    std::string empty_source{};
    source_.swap(empty_source);
    state_ = ProjectionState::complete;
    return state_;
}
bool DocumentProjection::publish(const Session &session, gf::DocumentPage &output) {
    if (state_ != ProjectionState::complete || output.request.serial ||
        !output.display_utf8.empty() || !output.mapping.empty())
        return false;
    if (!current(session)) {
        cancel();
        state_ = ProjectionState::stale;
        return false;
    }
    output.swap(prepared_);
    state_ = ProjectionState::published;
    return true;
}
}
