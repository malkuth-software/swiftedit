#pragma once
#include "display.hpp"
#include <gui_forms/document_view.hpp>

namespace swiftedit {
enum class ProjectionState { pending, complete, published, context_required, stale, invalid_request, budget_exceeded, cancelled };
// Consumer-owned producer for the installed D1 page contract. Called only on
// the Session's executor. No Session/control is retained or read by a worker.
// Exact requested coverage must have complete logical-line boundary context.
// Each read step copies <=8 KiB, plus at most two two-byte boundary probes on
// the first step; final bounded projection is a separate step.
// This is not a layout engine or support for arbitrarily long paragraphs.
// Adjacent unchanged text shares identity mappings; generated labels retain
// separate atomic mappings. Mapping spans do not certify grapheme boundaries.
class DocumentProjection final {
public:
    DocumentProjection(const Session &, gui_forms::DocumentPageRequest);
    DocumentProjection(const DocumentProjection &) = delete;
    DocumentProjection &operator=(const DocumentProjection &) = delete;
    ProjectionState step(const Session &);
    void cancel();
    ProjectionState state() const { return state_; }
    // Only complete/current tasks publish. Occupied output is preserved.
    bool publish(const Session &, gui_forms::DocumentPage &);
private:
    bool current(const Session &) const;
    gui_forms::DocumentPageRequest request_{};
    DocumentStamp stamp_{};
    std::uint64_t size_{};
    std::string source_{};
    gui_forms::DocumentPage prepared_{};
    ProjectionState state_{ProjectionState::pending};
    bool boundary_checked_{};
};
}
