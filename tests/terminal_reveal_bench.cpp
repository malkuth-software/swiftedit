#include "terminal_reveal.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(const Clock::time_point start) {
    const std::chrono::duration<double, std::milli> duration = Clock::now() - start;
    return duration.count();
}
void measure(const std::filesystem::path &directory, const std::string &name,
             const std::string &chunk, std::ofstream &raw, std::ofstream &summary,
             const bool reuse) {
    const std::filesystem::path path = directory / (name + ".txt");
    {
        std::ofstream output(path, std::ios::binary);
        std::size_t size = 0;
        while (size < swiftedit::editable_limit) {
            output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            size += chunk.size();
        }
        output.put('\n');
        output.close();
        if (!output)
            throw std::runtime_error("Failed to create reveal fixture.");
    }
    swiftedit::Session session{};
    session.open(path);
    if (!session.read_only())
        throw std::runtime_error("Reveal measurement requires an actual read-only source.");
    for (const std::uint64_t caret : {std::uint64_t{0}, std::uint64_t{8 * 1024 * 1024}}) {
        for (std::size_t trial = 0; trial < 2; ++trial) {
            std::vector<double> samples{};
            samples.reserve(16384);
            const Clock::time_point construction = Clock::now();
            swiftedit::TerminalNoWrapReveal task(session, caret, 0, 80, 20);
            const double construction_ms = elapsed(construction);
            bool complete = false;
            double total_ms = construction_ms;
            while (!complete) {
                const Clock::time_point start = Clock::now();
                complete = task.step(session);
                const double milliseconds = elapsed(start);
                total_ms += milliseconds;
                samples.push_back(milliseconds);
            }
            const swiftedit::TerminalPageCaret visible = task.caret(session);
            if (visible.column >= 80 || visible.row >= 20 || session.dirty())
                throw std::runtime_error("Reveal benchmark produced an invalid caret or changed source.");
            const swiftedit::TerminalHorizontalPage &page = task.viewport(session);
            const std::vector<swiftedit::TerminalHorizontalFrame> &frames = page.result(session);
            const std::vector<swiftedit::TerminalLogicalRow> &rows = page.rows(session);
            bool exact = false;
            for (const swiftedit::TerminalHorizontalCaret boundary : frames[visible.row].carets) {
                if (boundary.source_offset == caret && boundary.column == task.left(session) + visible.column)
                    exact = true;
            }
            if (!exact || rows[visible.row].offset > caret)
                throw std::runtime_error("Revealed caret does not map back to the requested source.");
            for (std::size_t index = 0; index < samples.size(); ++index)
                raw << name << ',' << caret << ',' << trial << ',' << index << ',' << samples[index] << '\n';
            std::sort(samples.begin(), samples.end());
            const std::size_t p95 = (samples.size() * 95 + 99) / 100 - 1;
            summary << name << ',' << caret << ',' << trial << ',' << samples.size() << ','
                    << construction_ms << ',' << total_ms << ',' << samples[p95] << ',' << samples.back() << '\n';
            summary.flush();
            std::cout << name << " caret=" << caret << " trial=" << trial << " steps=" << samples.size()
                      << " total_ms=" << total_ms << " worst_step_ms=" << samples.back() << std::endl;
            samples.clear();
            const Clock::time_point reused_construction = Clock::now();
            swiftedit::TerminalNoWrapReveal next(session, caret + 1, task.left(session), 80, 20,
                                                task.top(session), reuse ? &task : nullptr);
            const double reused_construction_ms = elapsed(reused_construction);
            total_ms = reused_construction_ms;
            complete = false;
            while (!complete) {
                const Clock::time_point start = Clock::now();
                complete = next.step(session);
                const double milliseconds = elapsed(start);
                samples.push_back(milliseconds);
                total_ms += milliseconds;
            }
            const swiftedit::TerminalPageCaret next_caret = next.caret(session);
            const swiftedit::TerminalHorizontalPage &next_page = next.viewport(session);
            const std::vector<swiftedit::TerminalHorizontalFrame> &next_frames = next_page.result(session);
            exact = false;
            for (const swiftedit::TerminalHorizontalCaret boundary : next_frames[next_caret.row].carets) {
                if (boundary.source_offset == caret + 1 &&
                    boundary.column == next.left(session) + next_caret.column)
                    exact = true;
            }
            if (!exact)
                throw std::runtime_error("Reused reveal changed the source caret mapping.");
            for (std::size_t index = 0; index < samples.size(); ++index)
                raw << name << "-next," << caret + 1 << ',' << trial << ',' << index << ',' << samples[index] << '\n';
            std::sort(samples.begin(), samples.end());
            const std::size_t reused_p95 = (samples.size() * 95 + 99) / 100 - 1;
            summary << name << "-next," << caret + 1 << ',' << trial << ',' << samples.size() << ','
                    << reused_construction_ms << ',' << total_ms << ',' << samples[reused_p95] << ',' << samples.back() << '\n';
            std::cout << name << "-next caret=" << caret + 1 << " total_ms=" << total_ms
                      << " worst_step_ms=" << samples.back() << std::endl;
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "--fresh-next"))
            throw std::runtime_error("Usage: swiftedit-terminal-reveal-bench NEW_DIRECTORY [--fresh-next]");
        const bool reuse = argc == 2;
        const std::filesystem::path directory(argv[1]);
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Reveal benchmark requires a new directory.");
        std::ofstream raw(directory / "samples.csv");
        std::ofstream summary(directory / "summary.csv");
        raw << std::fixed << std::setprecision(6) << "fixture,caret,trial,step,milliseconds\n";
        summary << std::fixed << std::setprecision(6)
                << "fixture,caret,trial,steps,construction_ms,total_work_ms,p95_step_ms,worst_step_ms\n";
        measure(directory, "short-lines", std::string(79, 'x') + "\n", raw, summary, reuse);
        measure(directory, "long-ascii", std::string(8192, 'x'), raw, summary, reuse);
        measure(directory, "long-controls", std::string(8192, '\0'), raw, summary, reuse);
        raw.flush();
        summary.flush();
        if (!raw || !summary)
            throw std::runtime_error("Cannot preserve reveal measurements.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
