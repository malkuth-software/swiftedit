#pragma once
#include <filesystem>
#include <memory>
#include <string_view>
namespace notepad {
class NewFileWriterState;
// Bounded synchronous writes to an owned sibling temporary. Destroying an
// unpublished writer cancels it. publish flushes and atomically installs a new
// name, never replacing an existing entry. Publication/flush errors are reported.
class NewFileWriter final {
public:
    explicit NewFileWriter(const std::filesystem::path &);
    ~NewFileWriter();
    NewFileWriter(const NewFileWriter &) = delete;
    NewFileWriter &operator=(const NewFileWriter &) = delete;
    void append(std::string_view);
    void publish();
private:
    std::unique_ptr<NewFileWriterState> state_{};
};
} // namespace notepad
