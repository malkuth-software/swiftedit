#include "document_projection.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

namespace se = swiftedit;
namespace gf = gui_forms;
bool mapping_matches(const gf::DocumentPage &page, const se::DisplayPage &expected) {
    std::size_t index = 0;
    for (const se::DisplayUnit &unit : expected.units()) {
        while (index < page.mapping.size() && page.mapping[index].end.value <= unit.display.offset)
            ++index;
        if (index == page.mapping.size())
            return false;
        const gf::DocumentMapSpan &map = page.mapping[index];
        if (map.begin.value > unit.display.offset || map.end.value < unit.display.offset + unit.display.length)
            return false;
        if (unit.kind == se::DisplayKind::text) {
            if (map.kind != gf::DocumentMapKind::identity_utf8 ||
                map.source.begin.value + unit.display.offset - map.begin.value != unit.source.offset ||
                map.source.end.value - map.source.begin.value != map.end.value - map.begin.value)
                return false;
        } else if (map.kind != gf::DocumentMapKind::atomic_token ||
            map.begin.value != unit.display.offset || map.end.value != unit.display.offset + unit.display.length ||
            map.source.begin.value != unit.source.offset || map.source.end.value != unit.source.offset + unit.source.length)
            return false;
    }
    const bool complete = expected.units().empty() ? page.mapping.empty() : index + 1 == page.mapping.size();
    return complete;
}
void measure(std::ofstream &output, const std::filesystem::path &path,
             const std::string &name, const std::string &bytes) {
    {
        std::ofstream fixture(path, std::ios::binary);
        fixture.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        fixture.close();
        if (!fixture)
            throw std::runtime_error("Cannot write projection benchmark fixture.");
    }
    se::Session session{};
    session.open(path);
    const se::DocumentStamp stamp = session.stamp();
    const se::DisplayPage expected(bytes);
    gf::DocumentPageRequest request{};
    request.revision = {stamp.identity.value, stamp.revision.value};
    request.serial = 1;
    request.permitted.end = gf::SourceByteOffset(bytes.size());
    std::vector<double> samples{};
    for (std::size_t trial = 0; trial < 35; ++trial) {
        const std::chrono::steady_clock::time_point admission_start = std::chrono::steady_clock::now();
        se::DocumentProjection task(session, request);
        const std::chrono::duration<double, std::milli> admission = std::chrono::steady_clock::now() - admission_start;
        if (trial >= 5)
            output << name << ',' << trial - 5 << ",0,construct," << admission.count() << '\n';
        std::size_t step = 0;
        while (task.state() == se::ProjectionState::pending) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            const se::ProjectionState state = task.step(session);
            const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
            if (trial >= 5) {
                output << name << ',' << trial - 5 << ',' << step << ','
                       << (state == se::ProjectionState::complete ? "projection" : "read")
                       << ',' << elapsed.count() << '\n';
                if (state == se::ProjectionState::complete)
                    samples.push_back(elapsed.count());
            }
            if (++step > 16)
                throw std::runtime_error("Projection did not complete within its bounded read count.");
        }
        gf::DocumentPage page{};
        const std::chrono::steady_clock::time_point publication_start = std::chrono::steady_clock::now();
        const bool published = task.publish(session, page);
        const std::chrono::duration<double, std::milli> publication = std::chrono::steady_clock::now() - publication_start;
        if (trial >= 5)
            output << name << ',' << trial - 5 << ',' << step << ",publish," << publication.count() << '\n';
        if (!published || page.display_utf8 != expected.text() ||
            !mapping_matches(page, expected) || session.text() != bytes || session.dirty())
            throw std::runtime_error("Projection benchmark correctness failed.");
    }
    std::sort(samples.begin(), samples.end());
    std::cout << name << " projection p50/p95/p99/max ms " << samples[14] << '/'
              << samples[28] << '/' << samples[29] << '/' << samples.back() << '\n';
}
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: swiftedit-document-projection-bench samples.csv");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("Cannot create projection samples.");
        const std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("swiftedit-projection-bench-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Unique fixture directory required.");
        struct Cleanup {
            std::filesystem::path directory{};
            ~Cleanup() { std::error_code error{}; std::filesystem::remove_all(directory, error); }
        } cleanup{directory};
        output << "fixture,trial,step,stage,milliseconds\n";
        const std::filesystem::path path = directory / "source.txt";
        measure(output, path, "ascii", std::string(65536, 'a'));
        measure(output, path, "controls", std::string(65536, '\0'));
        measure(output, path, "illegal", std::string(65536, '\xff'));
        output.flush();
        if (!output)
            throw std::runtime_error("Projection sample write failed.");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
