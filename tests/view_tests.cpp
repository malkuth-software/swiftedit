#include "markdown_view.hpp"
#include "query_field.hpp"
#include "csv_view.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace gf = gui_forms;
class ObservingPainter final : public gf::Painter {
public:
    std::size_t measurements{}, texts{};
    std::string drawn{};
    std::vector<std::string> labels{};
    std::vector<gf::Point> origins{};
    std::vector<gf::FontSpec> measured_fonts{};
    bool tall_metrics{}, slow_metrics{};
    std::size_t reference_outlines{};
    std::size_t tall_rules{};
    std::size_t tall_borders{};
    bool fail_measurement{};
    gf::Point translation{};
    void save() override {}
    void restore() override {}
    void translate(gf::Point point) override { translation = point; }
    void clip_rect(gf::Rect) override {}
    void fill_rect(gf::Rect bounds, gf::Color) override {
        if (bounds.width == 2 && bounds.height > 1000)
            ++tall_rules;
    }
    void stroke_rect(gf::Rect bounds, gf::Color, const double width) override {
        if (bounds.height > 1000)
            ++tall_borders;
        if (width == 2)
            ++reference_outlines;
    }
    void draw_line(gf::Point, gf::Point, gf::Color, double) override {}
    void draw_image(gf::ImageId, gf::Rect, double) override {
        throw std::runtime_error("Markdown must not load image resources.");
    }
    void draw_text_utf8(gf::Point origin, std::string_view text, gf::FontSpec, gf::Color) override {
        ++texts;
        drawn.append(text);
        labels.emplace_back(text);
        origins.push_back(origin);
    }
    gf::Size measure_text_utf8(std::string_view text, gf::FontSpec font) override {
        if (slow_metrics)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ++measurements;
        measured_fonts.push_back(font);
        if (fail_measurement)
            throw std::runtime_error("Injected measurement failure");
        const gf::Size result = gf::Painter::measure_text_utf8(text, font);
        return result;
    }
    gf::ResolvedTextLayout resolve_text_layout_utf8(std::string_view text, gf::FontSpec font) override {
        gf::ResolvedTextLayout result = gf::Painter::resolve_text_layout_utf8(text, font);
        if (tall_metrics) {
            result.ascent = 18;
            result.descent = 5;
            result.logical_size.height = 23;
        }
        return result;
    }
};
void check(bool good, const char *message) {
    if (!good)
        throw std::runtime_error(message);
}
void settle_formulas(notepad::CsvView &);
void settle_markdown(notepad::MarkdownView &view) {
    const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::seconds(5);
    while (view.preparation_pending()) {
        view.on_frame(gf::FrameClock::now());
        check(gf::FrameClock::now() < deadline, "Markdown worker readiness timeout");
        std::this_thread::yield();
    }
}
void paint_markdown(notepad::MarkdownView &view, gf::Painter &painter, gf::Rect bounds) {
    settle_markdown(view);
    const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::seconds(5);
    do {
        view.on_paint(painter, bounds);
        check(gf::FrameClock::now() < deadline, "Markdown layout readiness timeout");
    } while (view.layout_pending());
}
void verify_text_baselines() {
    ObservingPainter painter{};
    painter.tall_metrics = true;
    const std::shared_ptr<notepad::CsvView> grid =
        gf::make_control<notepad::CsvView>(gf::StableId("baseline.csv"));
    gf::Window grid_window(grid, {800, 600});
    (*grid).set_source("=1/0");
    grid_window.perform_layout();
    settle_formulas(*grid);
    (*grid).on_paint(painter, {0, 0, 800, 600});
    bool error_seen = false;
    for (std::size_t index = 0; index < painter.labels.size(); ++index) {
        if (painter.labels[index] == "#ERROR") {
            error_seen = true;
            check(painter.origins[index].y - 18 >= 68 && painter.origins[index].y + 5 <= 96,
                  "CSV text ascent/descent fit inside its row, including non-default metrics");
        }
    }
    check(error_seen, "Baseline fixture paints the error label");
    painter.labels.clear();
    painter.origins.clear();
    const std::shared_ptr<notepad::MarkdownView> markdown =
        gf::make_control<notepad::MarkdownView>(gf::StableId("baseline.markdown"));
    gf::Window markdown_window(markdown, {640, 480});
    (*markdown).set_source("body");
    markdown_window.perform_layout();
    paint_markdown(*markdown, painter, {0, 0, 640, 480});
    check(!painter.labels.empty() && painter.labels[0] == "body" &&
              painter.origins[0].y - 18 >= 24,
          "Markdown text remains below its block top using renderer ascent");
    painter.labels.clear();
    painter.origins.clear();
    const std::shared_ptr<notepad::QueryField> query =
        gf::make_control<notepad::QueryField>(gf::StableId("baseline.query"));
    gf::Window query_window(query, {300, 32});
    (*query).set_text("x");
    query_window.perform_layout();
    (*query).on_paint(painter, {0, 0, 300, 32});
    check(painter.labels.size() == 1 && painter.origins[0].y - 18 >= 2 &&
              painter.origins[0].y + 5 <= 30,
          "Query glyph fits the inner field using renderer ascent and descent");
}
void verify_duplicate_formulas() {
    const std::shared_ptr<notepad::CsvView> grid =
        gf::make_control<notepad::CsvView>(gf::StableId("formulas.csv"));
    gf::Window window(grid, {800, 600});
    (*grid).set_source("21,=A1*2,=A1*2\n5,=A1*2,=A1*2");
    window.perform_layout();
    settle_formulas(*grid);
    ObservingPainter first{};
    (*grid).on_paint(first, {0, 0, 800, 600});
    check(std::count(first.labels.begin(), first.labels.end(), "42") == 4,
          "Identical formulas at different addresses retain all displayed results");
    gf::PointerEvent hover{};
    hover.action = gf::PointerAction::move;
    hover.position = {354, 106}; // C2, a reused formula display.
    (*grid).on_pointer(hover);
    ObservingPainter highlighted{};
    (*grid).on_paint(highlighted, {0, 0, 800, 600});
    check(highlighted.reference_outlines == 1 && (*grid).status() == "=A1*2 | References: A1",
          "Reused formula preserves reference tooltip and visible reference outline");
    window.reset_activity_metrics();
    (*grid).on_pointer(hover);
    check(window.metrics().snapshot().dirty_marks == 0,
          "Repeated hover in the same cell does not dirty the view");
    hover.position = {4, 46}; // Row/column header area, still inside this control.
    (*grid).on_pointer(hover);
    ObservingPainter outside{};
    (*grid).on_paint(outside, {0, 0, 800, 600});
    check(outside.reference_outlines == 0 && (*grid).status().empty(),
          "Moving off cells clears stale formula highlights and hover detail");
    window.reset_activity_metrics();
    (*grid).on_pointer(hover);
    check(window.metrics().snapshot().dirty_marks == 0,
          "Repeated movement outside the grid does not dirty an already clear hover");
    (*grid).set_source("3,=A1*2,=A1*2\n5,=A1*2,=A1*2");
    settle_formulas(*grid);
    ObservingPainter edited{};
    (*grid).on_paint(edited, {0, 0, 800, 600});
    check(std::count(edited.labels.begin(), edited.labels.end(), "6") == 4 &&
              std::count(edited.labels.begin(), edited.labels.end(), "42") == 0,
          "Source change cannot reuse a previous refresh's calculation");
    (*grid).set_source("=B1,=B1\n1,=A2");
    settle_formulas(*grid);
    ObservingPainter cyclic{};
    (*grid).on_paint(cyclic, {0, 0, 800, 600});
    check(std::count(cyclic.labels.begin(), cyclic.labels.end(), "#ERROR") == 2,
          "Identical cyclic formulas remain errors at both root addresses");
    (*grid).set_source("7,=A1,=A1");
    settle_formulas(*grid);
    ObservingPainter recovered{};
    (*grid).on_paint(recovered, {0, 0, 800, 600});
    check(std::count(recovered.labels.begin(), recovered.labels.end(), "7") == 3 &&
              std::count(recovered.labels.begin(), recovered.labels.end(), "#ERROR") == 0,
          "Repairing formula source clears all previous errors");
}
void settle_formulas(notepad::CsvView &grid) {
    const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::seconds(5);
    while (grid.calculations_pending()) {
        grid.on_frame(gf::FrameClock::now());
        check(gf::FrameClock::now() < deadline, "Calculation worker completes within test timeout");
        std::this_thread::yield();
    }
}
struct ConvertedSource {
    std::string *output{};
    void operator()(const std::string &source) const { *output = source; }
};
void verify_cached_conversion() {
    const std::shared_ptr<notepad::CsvView> grid =
        gf::make_control<notepad::CsvView>(gf::StableId("conversion.csv"));
    gf::Window window(grid, {800, 600});
    window.perform_layout();
    std::string output{};
    const gf::SubscriptionToken subscription = (*grid).changed().subscribe(ConvertedSource{&output});
    (*grid).set_source("1,=A1/8,=A1/8\r\nkeep,\"quoted,value\",tail");
    settle_formulas(*grid);
    (*grid).select_cell({0, 2});
    (*grid).convert_to_value();
    check(output == "1,=A1/8,0.125\r\nkeep,\"quoted,value\",tail",
          "Completed duplicate formula converts its exact result and preserves unrelated source");
    output.clear();
    (*grid).set_source("8,=A1/8,=A1/8\r\nkeep,\"quoted,value\",tail");
    (*grid).convert_to_value();
    settle_formulas(*grid);
    check(output == "8,=A1/8,1\r\nkeep,\"quoted,value\",tail",
          "Source replacement cannot convert using the preceding source's cached result");
    output.clear();
    (*grid).set_source("=B1,=A1");
    settle_formulas(*grid);
    (*grid).select_cell({0, 1});
    (*grid).convert_to_value();
    check(output.empty() && (*grid).status().find("Circular") != std::string::npos,
          "Erroneous formula conversion publishes no source change");
    // No frame is pumped between request and cancellation. Even if the worker
    // finishes immediately, only the UI frame may adopt and publish its result.
    for (std::size_t scenario = 0; scenario < 8; ++scenario) {
        (*grid).set_source("8,=A1/8,=A1/8");
        (*grid).select_cell({0, 2});
        (*grid).convert_to_value();
        check(output.empty(), "Pending conversion returns before worker adoption");
        if (scenario == 0) {
            gf::KeyEvent escape{};
            escape.physical_key = gf::PhysicalKey::escape;
            (*grid).on_key(escape);
            check(escape.handled, "Escape cancels a pending conversion");
        } else if (scenario == 1) {
            (*grid).select_cell({0, 1});
        } else if (scenario == 2) {
            (*grid).set_source("16,=A1/8,=A1/8");
        } else if (scenario == 3) {
            (*grid).cancel_calculations();
        } else if (scenario == 4) {
            (*grid).on_focus_changed(false);
        } else if (scenario == 5) {
            (*grid).select_all();
        } else {
            const std::shared_ptr<gf::TextBox> entry =
                std::dynamic_pointer_cast<gf::TextBox>(window.find("csv.entry"));
            check(static_cast<bool>(entry), "Conversion fixture has a cell entry");
            if (scenario == 6)
                (*entry).set_text("=A1/4");
            else
                (*entry).cancelled().emit();
        }
        settle_formulas(*grid);
        check(output.empty(), "Cancelled or obsolete conversion cannot publish later");
        // Force fresh source next iteration, including after explicit cancel.
        (*grid).set_source("4,=A1/8,=A1/8");
    }
}
void verify_cooperative_formulas() {
    const std::shared_ptr<notepad::CsvView> grid =
        gf::make_control<notepad::CsvView>(gf::StableId("cooperative.csv"));
    gf::Window window(grid, {800, 600});
    window.perform_layout();
    std::string source{};
    for (std::size_t row = 0; row < 40; ++row) {
        if (row)
            source += '\n';
        source += "=" + std::to_string(1000 + row);
    }
    (*grid).set_source(source);
    check((*grid).calculations_pending(), "Distinct visible formulas yield instead of blocking input");
    ObservingPainter pending{};
    (*grid).on_paint(pending, {0, 0, 800, 600});
    check(std::count(pending.labels.begin(), pending.labels.end(), "...") > 0,
          "Pending formulas are visibly distinct from computed results");
    (*grid).cancel_calculations();
    check(!(*grid).calculations_pending(), "Leaving table view cancels pending calculations");
    (*grid).set_source(source);
    check((*grid).calculations_pending(), "Returning to unchanged table restarts cancelled work");
    gf::PointerEvent hover{};
    hover.action = gf::PointerAction::move;
    hover.position = {60, 493}; // A16 is beyond the first eight-formula slice.
    (*grid).on_pointer(hover);
    check((*grid).status() == "Calculating...", "Hover describes a pending result");
    settle_formulas(*grid);
    check((*grid).status() == "=1015 | References:",
          "Completing a hovered cell updates its detail without another mouse move");
    (*grid).cancel_calculations();
    (*grid).set_source(source);
    (*grid).set_source("7,=A1*2,=A1*2");
    settle_formulas(*grid);
    (*grid).on_frame(gf::FrameClock::now());
    ObservingPainter replacement{};
    (*grid).on_paint(replacement, {0, 0, 800, 600});
    check(std::count(replacement.labels.begin(), replacement.labels.end(), "14") == 2 &&
              std::count(replacement.labels.begin(), replacement.labels.end(), "1000") == 0,
          "Replacing source revokes stale queued formula results");
    (*grid).set_source(source);
    gf::PointerEvent wheel{};
    wheel.action = gf::PointerAction::wheel;
    wheel.wheel_delta.y = -1;
    (*grid).on_pointer(wheel);
    settle_formulas(*grid);
    ObservingPainter scrolled{};
    (*grid).on_paint(scrolled, {0, 0, 800, 600});
    check(std::count(scrolled.labels.begin(), scrolled.labels.end(), "1000") == 0 &&
              std::count(scrolled.labels.begin(), scrolled.labels.end(), "...") == 0,
          "Scrolling replaces pending work with only the new viewport");
    window.reset_activity_metrics();
    check(!(*grid).calculations_pending(), "Completed view retains no calculation work");
    (*grid).on_frame(gf::FrameClock::now());
    const gf::MetricsSnapshot idle = window.metrics().snapshot();
    check(idle.dirty_marks == 0 && idle.scheduled_frame_requests == 0,
          "A late frame after completion does not repaint or schedule more work");
}
void verify_csv_page_navigation() {
    const std::shared_ptr<notepad::CsvView> grid =
        gf::make_control<notepad::CsvView>(gf::StableId("navigation.csv"));
    gf::Window window(grid, {800, 200});
    (*grid).set_source("a,b,c\nd,e,f\ng,h,i\nj,k,l\nm,n,o\np\nq,r,s\nt,u,v\nw,x,y");
    window.perform_layout();
    (*grid).select_cell({0, 1});
    gf::KeyEvent key{};
    key.action = gf::KeyAction::down;
    key.physical_key = gf::PhysicalKey::page_down;
    key.modifiers = gf::Modifier::shift;
    (*grid).on_key(key);
    check(key.handled && (*grid).selected().row == 3 && (*grid).selected().column == 1,
          "CSV Page Down moves by the visible row count");
    check((*grid).copy_cells() == "b\r\ne\r\nh\r\nk", "Shift Page Down retains the rectangle anchor");
    key.physical_key = gf::PhysicalKey::page_up;
    key.modifiers = gf::Modifier::none;
    (*grid).on_key(key);
    check((*grid).selected().row == 0 && (*grid).copy_cells() == "b", "Page Up returns and collapses selection");
    key.physical_key = gf::PhysicalKey::end;
    (*grid).on_key(key);
    check((*grid).selected().column == 2, "End reaches the last cell in this row");
    key.modifiers = gf::Modifier::meta;
    (*grid).on_key(key);
    check((*grid).selected().row == 8 && (*grid).selected().column == 2, "Cmd End reaches final existing cell");
    // LLVM/PE leaf HScrollBar/VScrollBar RTTI is not canonical across the DLL.
    // Inspect the exported behavioral interface and verify its orientation.
    const std::shared_ptr<gf::ScrollBar> vertical =
        std::dynamic_pointer_cast<gf::ScrollBar>(window.find("csv.vertical"));
    check(static_cast<bool>(window.find("csv.vertical")), "CSV vertical scrollbar is registered");
    check(vertical && (*vertical).orientation() == gf::Orientation::vertical,
          "CSV scrollbar exposes the public vertical ScrollBar contract");
    const double before = (*vertical).value();
    gf::PointerEvent wheel{};
    wheel.action = gf::PointerAction::wheel;
    wheel.wheel_delta.x = 1;
    (*grid).on_pointer(wheel);
    check((*vertical).value() == before, "Horizontal-only wheel never scrolls CSV upward");
    key.physical_key = gf::PhysicalKey::page_up;
    key.modifiers = gf::Modifier::none;
    (*grid).on_key(key);
    check((*grid).selected().row == 5 && (*grid).selected().column == 0, "Page navigation clamps to a ragged row's real cells");
    key.physical_key = gf::PhysicalKey::home;
    key.modifiers = gf::Modifier::control;
    (*grid).on_key(key);
    check((*grid).selected().row == 0 && (*grid).selected().column == 0, "Ctrl Home reaches the first cell");
}
void verify_markdown_preparation() {
    const std::shared_ptr<notepad::MarkdownView> view =
        gf::make_control<notepad::MarkdownView>(gf::StableId("preparation.markdown"));
    gf::Window window(view, {640, 480});
    window.perform_layout();
    (*view).set_source("# Obsolete");
    (*view).set_source("# Current");
    check((*view).preparation_pending() && (*view).block_count() == 0,
          "Markdown submission yields before model adoption and revokes old presentation");
    ObservingPainter pending{};
    (*view).on_paint(pending, {0, 0, 640, 480});
    check(pending.drawn.find("Preparing Markdown") != std::string::npos,
          "Pending Markdown is explicitly visible");
    ObservingPainter current{};
    paint_markdown(*view, current, {0, 0, 640, 480});
    check(current.drawn.find("Current") != std::string::npos &&
          current.drawn.find("Obsolete") == std::string::npos && (*view).presentation_ready(),
          "Only current Markdown becomes visible");
    window.reset_activity_metrics();
    (*view).on_frame(gf::FrameClock::now());
    (*view).set_source("# Current");
    const gf::MetricsSnapshot idle = window.metrics().snapshot();
    check(idle.scheduled_frame_requests == 0 && idle.dirty_marks == 0,
          "Completed Markdown retains no polling or redundant preparation");
    (*view).set_source("# Cancel me");
    gf::KeyEvent escape{};
    escape.physical_key = gf::PhysicalKey::escape;
    (*view).on_key(escape);
    (*view).on_frame(gf::FrameClock::now());
    ObservingPainter cancelled{};
    (*view).on_paint(cancelled, {0, 0, 640, 480});
    check(escape.handled && !(*view).preparation_pending() &&
          cancelled.drawn.find("cancelled") != std::string::npos,
          "Escape cancels preparation without later result adoption");
    (*view).set_source("# Cancel me");
    check((*view).preparation_pending(), "Reopening cancelled source requests preparation again");
    settle_markdown(*view);
    (*view).set_source("\xff");
    ObservingPainter invalid{};
    paint_markdown(*view, invalid, {0, 0, 640, 480});
    check(invalid.drawn.find("valid UTF-8") != std::string::npos,
          "Worker parse failures are shown instead of a stale presentation");
    ObservingPainter changed_metrics{};
    window.set_text_metrics_provider(&changed_metrics);
    window.perform_layout();
    paint_markdown(*view, changed_metrics, {0, 0, 640, 480});
    check(changed_metrics.drawn.find("valid UTF-8") != std::string::npos &&
              !(*view).presentation_ready(),
          "Metrics invalidation cannot turn rejected Markdown into an empty successful view");
}
void verify_tall_markdown_visibility() {
    const std::shared_ptr<notepad::MarkdownView> view =
        gf::make_control<notepad::MarkdownView>(gf::StableId("tall.markdown"));
    gf::Window window(view, {640, 140});
    window.perform_layout();
    std::string source = "> ";
    for (std::size_t word = 0; word < 10000; ++word)
        source += "word ";
    source += "[tail](https://example.invalid/tail)";
    (*view).set_source(source);
    ObservingPainter initial{};
    paint_markdown(*view, initial, {0, 0, 640, 140});
    check(initial.measurements < 10,
          "Repeated Markdown words share metrics within one font and layout rebuild");
    for (const std::string &label : initial.labels)
        check(label.find_first_not_of(' ') != std::string::npos,
              "Space-only layout runs do not submit native text drawing");
    const std::shared_ptr<gf::ScrollBar> scroll =
        std::dynamic_pointer_cast<gf::ScrollBar>(window.find("markdown.vertical"));
    check(scroll && (*scroll).orientation() == gf::Orientation::vertical,
          "Tall Markdown has a vertical scrollbar");
    (*scroll).set_value(3000);
    ObservingPainter middle{};
    paint_markdown(*view, middle, {0, 0, 640, 140});
    check(middle.texts < 200 && middle.tall_rules == 1,
          "Tall quote preserves its intersecting rule without submitting off-screen text");
    gf::KeyEvent end{};
    end.physical_key = gf::PhysicalKey::end;
    end.modifiers = gf::Modifier::control;
    (*view).on_key(end);
    ObservingPainter bottom{};
    paint_markdown(*view, bottom, {0, 0, 640, 140});
    bool found_tail = false;
    for (std::size_t index = 0; index < bottom.labels.size(); ++index) {
        if (bottom.labels[index] != "tail")
            continue;
        gf::PointerEvent hover{};
        hover.action = gf::PointerAction::move;
        hover.position = {bottom.origins[index].x + bottom.translation.x + 1,
                          bottom.origins[index].y + bottom.translation.y - 2};
        (*view).on_pointer(hover);
        found_tail = (*view).hovered_url() == "https://example.invalid/tail";
    }
    check(found_tail && bottom.texts < 200, "Indexed tail remains painted and link hover remains exact");
    source = "> | ";
    for (std::size_t word = 0; word < 10000; ++word)
        source += "word ";
    source += "|\n> | --- |\n";
    (*view).set_source(source);
    ObservingPainter table_initial{};
    paint_markdown(*view, table_initial, {0, 0, 640, 140});
    (*scroll).set_value(3000);
    ObservingPainter table_middle{};
    paint_markdown(*view, table_middle, {0, 0, 640, 140});
    check(table_middle.texts < 200 && table_middle.tall_borders == 1 && table_middle.tall_rules == 1,
          "Tall table retains both overlapping decorations while pruning off-screen text");
    (*view).set_source("*word* **word** `word` word");
    ObservingPainter styles{};
    paint_markdown(*view, styles, {0, 0, 640, 140});
    bool italic = false, bold = false, monospace = false;
    for (const gf::FontSpec font : styles.measured_fonts) {
        italic = italic || font.italic;
        bold = bold || font.weight == 700;
        monospace = monospace || font.role == gf::FontRole::monospace;
    }
    check(italic && bold && monospace,
          "Identical Markdown words are measured separately when font style changes");
}
void verify_markdown_layout_slices() {
    ObservingPainter slow{};
    slow.slow_metrics = true;
    const std::shared_ptr<notepad::MarkdownView> view =
        gf::make_control<notepad::MarkdownView>(gf::StableId("sliced.markdown"));
    gf::Window window(view, {640, 200});
    window.set_text_metrics_provider(&slow);
    window.perform_layout();
    std::string source{};
    for (std::size_t index = 0; index < 20000; ++index)
        source += "word" + std::to_string(index) + " ";
    (*view).set_source(source);
    settle_markdown(*view);
    (*view).on_paint(slow, {0, 0, 640, 200});
    check((*view).layout_pending() && !(*view).presentation_ready() && slow.measurements < 10,
          "Slow native measurements yield before exhausting the word quota");
    check(slow.drawn.find("Laying out Markdown") != std::string::npos &&
              slow.drawn.find("word0") == std::string::npos,
          "Partial layout never publishes incomplete document geometry");
    window.reset_activity_metrics();
    (*view).on_frame(gf::FrameClock::now());
    check((*view).layout_pending() && window.metrics().snapshot().dirty_marks == 0,
          "UI timer advances unfinished layout without repainting its unchanged placeholder");
    gf::KeyEvent escape{};
    escape.physical_key = gf::PhysicalKey::escape;
    (*view).on_key(escape);
    check(escape.handled && !(*view).layout_pending(), "Escape revokes partial layout");
    window.reset_activity_metrics();
    (*view).on_frame(gf::FrameClock::now());
    check(window.metrics().snapshot().scheduled_frame_requests == 0,
          "Cancelled layout does not restart its scheduler");
    (*view).set_source(source);
    settle_markdown(*view);
    ObservingPainter first{};
    (*view).on_paint(first, {0, 0, 640, 200});
    check((*view).layout_pending(), "A large fast layout also yields at its work quota");
    ObservingPainter replacement{};
    replacement.tall_metrics = true;
    window.set_text_metrics_provider(&replacement);
    window.perform_layout();
    (*view).arrange({0, 0, 400, 200});
    (*view).on_paint(replacement, {0, 0, 400, 200});
    check((*view).layout_pending(), "Readiness fixture retains work after its first paint");
    const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::seconds(5);
    while ((*view).layout_pending()) {
        (*view).on_frame(gf::FrameClock::now());
        check(gf::FrameClock::now() < deadline, "Timer layout readiness timeout");
    }
    check(!(*view).presentation_ready(), "Timer completion alone does not claim a completed paint");
    paint_markdown(*view, replacement, {0, 0, 400, 200});
    check((*view).presentation_ready() && !replacement.origins.empty(),
          "Provider and width changes restart unfinished layout successfully");
    bool first_word = false;
    for (std::size_t index = 0; index < replacement.labels.size(); ++index) {
        if (replacement.labels[index] == "word0") {
            first_word = true;
            check(replacement.origins[index].y == 44,
                  "Restarted geometry uses the new metrics even for its first word");
        }
    }
    check(first_word, "Restarted layout retains the first word");
    ObservingPainter before_resize{};
    before_resize.tall_metrics = true;
    (*view).on_paint(before_resize, {0, 0, 400, 200});
    (*view).arrange({0, 0, 640, 200});
    (*view).on_paint(replacement, {0, 0, 640, 200});
    check((*view).layout_pending(), "Resize fixture has partial geometry at the wider width");
    (*view).arrange({0, 0, 400, 200});
    paint_markdown(*view, replacement, {0, 0, 400, 200});
    ObservingPainter after_resize{};
    after_resize.tall_metrics = true;
    (*view).on_paint(after_resize, {0, 0, 400, 200});
    check(before_resize.labels == after_resize.labels &&
              before_resize.origins.size() == after_resize.origins.size(),
          "Returning to the published width discards an unfinished wider layout");
    for (std::size_t index = 0; index < before_resize.origins.size(); ++index)
        check(before_resize.origins[index].x == after_resize.origins[index].x &&
                  before_resize.origins[index].y == after_resize.origins[index].y,
              "Width reversal reproduces the original complete geometry");
    (*view).set_source(source + " old");
    settle_markdown(*view);
    (*view).on_paint(first, {0, 0, 400, 200});
    check((*view).layout_pending(), "Source replacement fixture has unfinished layout");
    (*view).set_source("Current");
    ObservingPainter current{};
    paint_markdown(*view, current, {0, 0, 400, 200});
    check(current.drawn.find("Current") != std::string::npos &&
              current.drawn.find("word0") == std::string::npos,
          "Source replacement discards all partial runs and borrowed links");
}
void verify_markdown_metric_invalidation() {
    ObservingPainter original{};
    ObservingPainter replacement{};
    replacement.tall_metrics = true;
    const std::shared_ptr<notepad::MarkdownView> view =
        gf::make_control<notepad::MarkdownView>(gf::StableId("metrics.markdown"));
    gf::Window window(view, {640, 200});
    window.set_text_metrics_provider(&original);
    window.perform_layout();
    (*view).set_source("word");
    paint_markdown(*view, original, {0, 0, 640, 200});
    check(!original.origins.empty(), "Original Markdown baseline exists");
    const double baseline = original.origins.front().y;
    window.set_text_metrics_provider(&replacement);
    window.perform_layout();
    paint_markdown(*view, replacement, {0, 0, 640, 200});
    check(replacement.measurements != 0 && !replacement.origins.empty() &&
              replacement.origins.front().y != baseline,
          "A same-width metrics-provider change rebuilds Markdown geometry");
    const std::size_t measurements = replacement.measurements;
    paint_markdown(*view, replacement, {0, 0, 640, 200});
    check(replacement.measurements == measurements,
          "Stable Markdown painting reuses the replacement provider geometry");
}
int main() {
    try {
        verify_markdown_layout_slices();
        verify_markdown_metric_invalidation();
        verify_csv_page_navigation();
        verify_text_baselines();
        verify_duplicate_formulas();
        verify_cooperative_formulas();
        verify_cached_conversion();
        verify_markdown_preparation();
        verify_tall_markdown_visibility();
        const std::shared_ptr<notepad::CsvView> grid =
            gf::make_control<notepad::CsvView>(gf::StableId("test.csv"));
        gf::Window grid_window(grid, {800, 600});
        (*grid).set_source("a,b,c,d,e,f,g,h");
        grid_window.perform_layout();
        const std::shared_ptr<gf::ScrollBar> horizontal =
            std::dynamic_pointer_cast<gf::ScrollBar>(grid_window.find("csv.horizontal"));
        check(horizontal && (*horizontal).orientation() == gf::Orientation::horizontal,
              "CSV horizontal scrollbar exists");
        for (std::size_t column = 0; column < 5; ++column) {
            gf::KeyEvent right{};
            right.physical_key = gf::PhysicalKey::right;
            (*grid).on_key(right);
            check(right.handled, "CSV arrow navigation handled");
        }
        check((*grid).selected() == swiftedit::CellAddress{0, 5} && (*horizontal).value() == 1,
              "Selecting the partially visible sixth cell scrolls it fully into view");
        (*grid).select_cell({0, 7});
        check((*horizontal).value() == 3, "Explicit cell selection also reveals its whole column");
        (*grid).arrange({0, 0, 400, 600});
        check((*horizontal).value() == 6,
              "Narrowing the viewport retains whole selected-cell visibility");
        (*grid).arrange({0, 0, 1600, 600});
        check((*horizontal).value() == 0, "Expanding to fit all columns resets horizontal offset");
        (*grid).arrange({0, 0, 400, 600});
        (*grid).set_source("a,b");
        check((*horizontal).value() == 0, "A smaller source resets obsolete horizontal extent");
        const std::shared_ptr<gf::ScrollBar> vertical =
            std::dynamic_pointer_cast<gf::ScrollBar>(grid_window.find("csv.vertical"));
        gf::PointerEvent fitting_wheel{};
        fitting_wheel.action = gf::PointerAction::wheel;
        fitting_wheel.wheel_delta.y = -1;
        (*grid).on_pointer(fitting_wheel);
        check(vertical && (*vertical).orientation() == gf::Orientation::vertical && (*vertical).value() == 0,
              "Wheel input cannot scroll fitting rows into a disabled placeholder range");
        (*grid).set_source("a,b,c\r\nd\ne,f");
        for (const gf::Modifier modifier : {gf::Modifier::control, gf::Modifier::meta}) {
            (*grid).select_cell({0, 0});
            gf::KeyEvent select_all{};
            select_all.physical_key = gf::PhysicalKey::a;
            select_all.modifiers = modifier;
            grid_window.request_focus(grid);
            grid_window.dispatch_key(select_all);
            check((*grid).copy_cells() == "a\tb\tc\r\nd\r\ne\tf",
                  "Ctrl/Cmd+A selects existing cells across ragged rows");
            gf::KeyEvent right{};
            right.physical_key = gf::PhysicalKey::right;
            grid_window.dispatch_key(right);
            check((*grid).copy_cells() == "b", "Arrow navigation leaves whole-table selection");
        }
        const std::shared_ptr<notepad::QueryField> query =
            gf::make_control<notepad::QueryField>(gf::StableId("test.query"));
        gf::Window query_window(query, {300, 32});
        (*query).set_text("a?c");
        query_window.perform_layout();
        ObservingPainter query_painter{};
        (*query).on_paint(query_painter, {0, 0, 300, 32});
        check(query_painter.drawn == "a?c", "Question mark is initially rendered literally");
        (*query).select(gf::Utf8Offset(1), gf::Utf8Offset(1));
        (*query).on_focus_changed(true);
        gf::KeyEvent toggle{};
        toggle.physical_key = 0x38;
        toggle.modifiers = gf::Modifier::control | gf::Modifier::shift;
        (*query).on_key(toggle);
        const swiftedit::SearchPattern flagged = (*query).pattern();
        check(toggle.handled && flagged.slots()[1].wildcard && (*query).text() == "a?c",
              "Ctrl-question toggles metadata without rewriting the literal query");
        query_painter.drawn.clear();
        (*query).on_paint(query_painter, {0, 0, 300, 32});
        check(query_painter.drawn == "a\xe2\x80\xa2"
                                     "c",
              "Flagged position renders as a dot");
        gf::PointerEvent right_click{};
        right_click.action = gf::PointerAction::down;
        right_click.button = gf::PointerButton::secondary;
        right_click.position = {20, 10};
        (*query).on_pointer(right_click);
        const swiftedit::SearchPattern unflagged = (*query).pattern();
        check(right_click.handled && !unflagged.slots()[1].wildcard,
              "Right-click toggles the hit character back to literal");
        (*query).toggle_slot(1);
        (*query).select(gf::Utf8Offset(3), gf::Utf8Offset(3));
        gf::TextInputEvent appended{"d"};
        (*query).on_text_input(appended);
        check(appended.handled && (*query).text() == "a?cd", "Query append routes through text input");
        const swiftedit::SearchPattern extended = (*query).pattern();
        check(extended.slots()[1].wildcard && !extended.slots()[3].wildcard,
              "Typing outside a flagged position retains its flag");
        (*query).set_text("a\xc3\xa9"
                          "c");
        const swiftedit::SearchPattern edited = (*query).pattern();
        check(!edited.slots()[1].wildcard, "Replaced query character starts as literal");
        (*query).select(gf::Utf8Offset(3), gf::Utf8Offset(3));
        gf::TextInputEvent inserted{};
        inserted.text_utf8 = "?";
        (*query).on_text_input(inserted);
        check(inserted.handled && (*query).text() == "a\xc3\xa9?c",
              "Query field routes standard Unicode text input");
        (*query).on_focus_changed(false);
        (*query).set_text(std::string(256, 'a'));
        for (std::size_t slot = 0; slot < 256; ++slot)
            (*query).toggle_slot(slot);
        query_painter.measurements = 0;
        query_painter.texts = 0;
        (*query).on_paint(query_painter, {0, 0, 300, 32});
        check(query_painter.measurements == 2, "Query measures baseline and one repeated wildcard label");
        check(query_painter.texts < 30, "Query submits only visible cells");
        const std::size_t measured_query_labels = query_painter.measurements;
        (*query).select(gf::Utf8Offset(256), gf::Utf8Offset(256));
        (*query).on_paint(query_painter, {0, 0, 300, 32});
        check(query_painter.measurements == measured_query_labels + 1,
              "Query caret scrolling reuses prepared label geometry");
        const std::shared_ptr<notepad::MarkdownView> view =
            gf::make_control<notepad::MarkdownView>(gf::StableId("test.markdown"));
        gf::Window window(view, {640, 480});
        (*view).set_source("# Title\n\n**Bold** [link](file:///inert)\n\n"
                           "| A | B |\n|---|---|\n| one | two |\n");
        window.perform_layout();
        ObservingPainter painter{};
        paint_markdown(*view, painter, {0, 0, 640, 480});
        check(painter.drawn.find("Title") != std::string::npos &&
                  painter.drawn.find("Bold") != std::string::npos &&
                  painter.drawn.find("one") != std::string::npos,
              "Native Markdown paint contains parsed headings, styled text and table cells");
        const std::size_t measured = painter.measurements;
        paint_markdown(*view, painter, {0, 0, 640, 480});
        check(painter.measurements == measured, "Warm Markdown paint reuses layout measurements");
        gf::PointerEvent click{};
        click.action = gf::PointerAction::down;
        click.position = {50, 70};
        (*view).on_pointer(click);
        check(click.handled, "Rendered clicks are inert");
        (*view).set_source("[first](file:///first)\n\nsecond\n\nthird\n\nfourth\n");
        (*view).arrange({0, 0, 640, 90});
        paint_markdown(*view, painter, {0, 0, 640, 90});
        gf::PointerEvent link_hover{};
        link_hover.action = gf::PointerAction::move;
        link_hover.position = {26, 54};
        (*view).on_pointer(link_hover);
        check((*view).hovered_url() == "file:///first", "Visible link exposes its inert URL");
        std::shared_ptr<gf::ScrollBar> markdown_scroll{};
        for (const gf::Control::Ptr &child : (*view).children()) {
            const std::shared_ptr<gf::ScrollBar> candidate =
                std::dynamic_pointer_cast<gf::ScrollBar>(child);
            if (candidate && (*candidate).orientation() == gf::Orientation::vertical)
                markdown_scroll = candidate;
        }
        check(static_cast<bool>(markdown_scroll), "Markdown scrollbar exists");
        const std::size_t before_navigation = painter.measurements;
        gf::KeyEvent navigation{};
        navigation.action = gf::KeyAction::down;
        navigation.physical_key = gf::PhysicalKey::page_down;
        (*view).on_key(navigation);
        check(navigation.handled && (*markdown_scroll).value() > 0 && (*view).hovered_url().empty(),
              "Page Down scrolls rendered Markdown and revokes the stale hover");
        navigation.handled = false;
        navigation.physical_key = gf::PhysicalKey::home;
        navigation.modifiers = gf::Modifier::meta;
        (*view).on_key(navigation);
        check(navigation.handled && (*markdown_scroll).value() == 0, "Mac document Home returns to preview start");
        navigation.physical_key = gf::PhysicalKey::end;
        navigation.modifiers = gf::Modifier::control;
        (*view).on_key(navigation);
        const double preview_end = (*markdown_scroll).value();
        check(preview_end > 0, "Document End reaches preview bottom");
        navigation.physical_key = gf::PhysicalKey::down;
        navigation.modifiers = gf::Modifier::none;
        (*view).on_key(navigation);
        check((*markdown_scroll).value() == preview_end, "Arrow scrolling clamps at preview bottom");
        paint_markdown(*view, painter, {0, 0, 640, 90});
        check(painter.measurements == before_navigation, "Keyboard navigation reuses prepared Markdown geometry");
        (*markdown_scroll).set_value(32);
        check((*view).hovered_url().empty(), "Scrolling clears the prior link tooltip");
        link_hover.position = {26, 20}; // The old link lies behind the fixed ruler.
        (*view).on_pointer(link_hover);
        check((*view).hovered_url().empty(), "Ruler cannot expose a clipped link tooltip");
        (*markdown_scroll).set_value(0);
        link_hover.position = {26, 54};
        (*view).on_pointer(link_hover);
        check((*view).hovered_url() == "file:///first", "Visible link can be hovered again");
        (*view).set_source("[replacement](file:///replacement)");
        (*view).on_pointer(link_hover);
        check((*view).hovered_url().empty(), "Pending layout cannot expose stale source links");
        (*view).arrange({0, 0, 640, 480});
        (*view).set_source("# Changed\n\nAfter revision.");
        painter.drawn.clear();
        paint_markdown(*view, painter, {0, 0, 640, 480});
        check(painter.drawn.find("Changed") != std::string::npos,
              "Changed source invalidates render cache");
        gf::PointerEvent wheel{};
        wheel.action = gf::PointerAction::wheel;
        wheel.wheel_delta.y = -1;
        (*view).on_pointer(wheel);
        paint_markdown(*view, painter, {0, 0, 640, 480});
        check(painter.translation.y == 28, "Fitting content cannot scroll into artificial range");
        navigation.physical_key = gf::PhysicalKey::page_down;
        (*view).on_key(navigation);
        check((*markdown_scroll).value() == 0, "Keyboard cannot scroll fitting content into artificial range");
        (*view).arrange({0, 0, 640, 90});
        (*view).on_pointer(wheel);
        paint_markdown(*view, painter, {0, 0, 640, 90});
        check(painter.translation.y < 28, "Height-only shrink updates scroll extent");
        const std::size_t before_expand = painter.measurements;
        (*view).arrange({0, 0, 640, 480});
        paint_markdown(*view, painter, {0, 0, 640, 480});
        check(painter.translation.y == 28 && painter.measurements == before_expand,
              "Height-only expansion resets scrolling without measuring text again");
        (*view).set_source("```\n" + std::string(300, 'x') + "\n```\n");
        paint_markdown(*view, painter, {0, 0, 640, 480});
        const std::size_t before_horizontal = painter.measurements;
        navigation.physical_key = gf::PhysicalKey::right;
        for (unsigned step = 0; step < 3; ++step)
            (*view).on_key(navigation);
        painter.labels.clear();
        painter.origins.clear();
        paint_markdown(*view, painter, {0, 0, 640, 480});
        check(painter.translation.x == -96 && painter.measurements == before_horizontal,
              "Horizontal keyboard navigation scrolls code without remeasuring");
        bool ruler_aligned = false;
        for (std::size_t index = 0; index < painter.labels.size(); ++index) {
            if (painter.labels[index] == "2" && painter.origins[index].x == 3)
                ruler_aligned = true;
        }
        check(ruler_aligned && std::find(painter.labels.begin(), painter.labels.end(), "0") == painter.labels.end(),
              "Ruler follows the horizontally scrolled document coordinates");
        (*view).set_source("# Recovery");
        painter.fail_measurement = true;
        painter.drawn.clear();
        paint_markdown(*view, painter, {0, 0, 640, 480});
        check(painter.drawn.find("Injected measurement failure") != std::string::npos,
              "Layout failures are reported");
        painter.fail_measurement = false;
        painter.drawn.clear();
        (*view).arrange({0, 0, 600, 480});
        paint_markdown(*view, painter, {0, 0, 600, 480});
        check(painter.drawn.find("Recovery") != std::string::npos &&
                  painter.drawn.find("Injected measurement failure") == std::string::npos,
              "Successful layout retry clears the previous error");
        std::string word_links = "[";
        for (std::size_t index = 0; index < 300; ++index)
            word_links += "word ";
        word_links += "](https://example.com/";
        word_links.append(60000, 'a');
        word_links += ')';
        (*view).set_source(word_links);
        painter.drawn.clear();
        paint_markdown(*view, painter, {0, 0, 600, 480});
        check((*view).presentation_ready() && painter.drawn.find("word") != std::string::npos &&
                  painter.drawn.find("Markdown layout exceeds") == std::string::npos,
              "Words in one long link share its prepared URL without per-word copies");
        (*view).set_source("Recovered after link storage limit");
        painter.drawn.clear();
        paint_markdown(*view, painter, {0, 0, 600, 480});
        check(painter.drawn.find("Recovered") != std::string::npos,
              "Layout storage refusal permits a later valid source");
        std::string decorated_limit = "> | ";
        for (std::size_t index = 0; index < 125000; ++index)
            decorated_limit += "x ";
        decorated_limit += "|\n> | --- |\n";
        (*view).set_source(decorated_limit);
        painter.drawn.clear();
        paint_markdown(*view, painter, {0, 0, 600, 480});
        check(painter.drawn.find("Markdown layout exceeds display run budget.") != std::string::npos,
              "Final table borders and quote decorations count against the run budget");
        (*view).set_source("Recovered after decoration limit");
        painter.drawn.clear();
        paint_markdown(*view, painter, {0, 0, 600, 480});
        check(painter.drawn.find("Recovered") != std::string::npos,
              "Decoration budget refusal permits a later valid source");
        std::cout << "Native Markdown paint/cache tests passed.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
