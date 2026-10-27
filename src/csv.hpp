#pragma once
#include <cstdint>
#include <exception>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace swiftedit {
struct Cell {
    std::string value{};
    std::size_t begin{}, end{};
};
struct CellAddress {
    std::size_t row{}, column{};
    bool operator==(const CellAddress &other) const {
        const bool equal = row == other.row && column == other.column;
        return equal;
    }
};
[[nodiscard]] CellAddress cell_address(std::string_view);
[[nodiscard]] std::string cell_name(CellAddress);
class Csv {
public:
    explicit Csv(std::string_view source);
    const Cell &cell(CellAddress) const;
    const std::vector<std::vector<Cell>> &rows() const { return rows_; }
    [[nodiscard]] std::string set(CellAddress, std::string_view) const;
    [[nodiscard]] std::string clear(CellAddress first, CellAddress last) const;
    [[nodiscard]] std::string clear_all() const;

private:
    std::string source_{};
    std::vector<std::vector<Cell>> rows_{};
};
struct Calculation {
    std::string result{};
    std::vector<CellAddress> references{};
};
class CalculationCancelled final : public std::exception {
public:
    const char *what() const noexcept override { return "Formula calculation cancelled."; }
};
// Exact bounded rational arithmetic: never binary floating point. Intermediate
// overflow and non-terminating decimals are errors, not approximate results.
// Optional cancellation is checked within range traversal, numeric parsing and
// aggregation. Cancellation throws CalculationCancelled without a partial result;
// allocation and cleanup are not hard-latency bounded. The caller keeps Csv alive
// and immutable throughout a calculation, including any worker execution.
[[nodiscard]] Calculation calculate(const Csv &, std::string_view expression,
                                    std::stop_token cancellation = {});
// Formula text stays in Csv source. Each evaluation owns its dependency cache;
// reopening/evaluating a changed table cannot reuse stale results.
// Cached dependencies retain path depth, so reference order cannot bypass the
// 64-cell dependency bound.
[[nodiscard]] Calculation calculate_cell(const Csv &, CellAddress,
                                         std::stop_token cancellation = {});
[[nodiscard]] std::string convert_to_value(const Csv &, CellAddress);
} // namespace swiftedit
