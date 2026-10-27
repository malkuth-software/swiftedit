#include "query_field.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

namespace gf = gui_forms;
// Counts draw submissions and uses the public fallback text metrics. This is
// control preparation/submission timing, not a native raster/compositor probe.
class QueryPainter final : public gf::Painter {
public:
    std::size_t texts{};
    void save() override {}
    void restore() override {}
    void translate(gf::Point) override {}
    void clip_rect(gf::Rect) override {}
    void fill_rect(gf::Rect, gf::Color) override {}
    void stroke_rect(gf::Rect, gf::Color, double) override {}
    void draw_line(gf::Point, gf::Point, gf::Color, double) override {}
    void draw_image(gf::ImageId, gf::Rect, double) override {
        throw std::runtime_error("Query unexpectedly submitted an image.");
    }
    void draw_text_utf8(gf::Point, std::string_view, gf::FontSpec, gf::Color) override { ++texts; }
};
void measure(std::ofstream &output, const std::string &name, const std::string &source, bool paint) {
    const std::shared_ptr<notepad::QueryField> query =
        gf::make_control<notepad::QueryField>(gf::StableId("query.bench"));
    gf::Window window(query, {640, 40});
    window.perform_layout();
    QueryPainter painter{};
    if (!window.request_focus(query))
        throw std::runtime_error("Query focus failed.");
    (*query).set_text(source);
    const std::size_t count = (*query).pattern().slots().size();
    for (std::size_t index = 0; index < count; ++index)
        (*query).toggle_slot(index);
    std::vector<double> insertions{}, undos{};
    insertions.reserve(200);
    undos.reserve(200);
    for (std::size_t sample = 0; sample < 220; ++sample) {
        (*query).select(gf::Utf8Offset(0), gf::Utf8Offset(0));
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const bool inserted = window.dispatch_text({"x"});
        if (paint)
            (*query).on_paint(painter, {0, 0, 640, 40});
        const std::chrono::steady_clock::time_point middle = std::chrono::steady_clock::now();
        const swiftedit::SearchPattern changed = (*query).pattern();
        if (!inserted || (*query).text() != "x" + source || changed.slots().size() != count + 1 ||
            changed.slots()[0].wildcard || !changed.slots()[1].wildcard)
            throw std::runtime_error("Query insertion changed text or wildcard identity.");
        gf::KeyEvent undo{};
        undo.physical_key = gf::PhysicalKey::z;
        undo.modifiers = gf::Modifier::control;
        const std::chrono::steady_clock::time_point undo_start = std::chrono::steady_clock::now();
        const bool undone = window.dispatch_key(undo);
        if (paint)
            (*query).on_paint(painter, {0, 0, 640, 40});
        const std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
        const swiftedit::SearchPattern restored = (*query).pattern();
        if (!undone || (*query).text() != source || restored.slots().size() != count)
            throw std::runtime_error("Query undo changed source.");
        for (const swiftedit::SearchSlot &slot : restored.slots())
            if (!slot.wildcard)
                throw std::runtime_error("Query undo lost a wildcard flag.");
        if (sample >= 20) {
            const std::chrono::duration<double, std::milli> insertion = middle - start;
            const std::chrono::duration<double, std::milli> reversal = finish - undo_start;
            insertions.push_back(insertion.count());
            undos.push_back(reversal.count());
            output << name << ',' << source.size() << ',' << count << ',' << sample - 20
                   << (paint ? ",insert-paint," : ",insert,") << insertion.count() << '\n';
            output << name << ',' << source.size() << ',' << count << ',' << sample - 20
                   << (paint ? ",undo-paint," : ",undo,") << reversal.count() << '\n';
        }
    }
    std::sort(insertions.begin(), insertions.end());
    std::sort(undos.begin(), undos.end());
    std::cout << name << " insert p50/p95/p99/max ms: " << insertions[99] << '/'
              << insertions[189] << '/' << insertions[197] << '/' << insertions.back()
              << "; undo: " << undos[99] << '/' << undos[189] << '/' << undos[197]
              << '/' << undos.back() << '\n';
}
int main(int argc, char **argv) {
    try {
        if (argc != 2 && !(argc == 3 && std::string_view(argv[2]) == "--paint"))
            throw std::runtime_error("Usage: swiftedit-query-edit-bench samples.csv [--paint]");
        const bool paint = argc == 3;
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("Cannot create query benchmark samples.");
        output << "fixture,bytes,graphemes,sample,operation,milliseconds\n";
        measure(output, "ascii", std::string(4095, 'a'), paint);
        std::string unicode{}, combining{};
        for (std::size_t index = 0; index < 2047; ++index)
            unicode += "\xc3\xa9";
        for (std::size_t index = 0; index < 1365; ++index)
            combining += "e\xcc\x81";
        measure(output, "unicode", unicode, paint);
        measure(output, "combining", combining, paint);
        output.flush();
        if (!output)
            throw std::runtime_error("Query sample write failed.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
