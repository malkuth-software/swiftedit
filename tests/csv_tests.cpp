#include "csv.hpp"
#include <iostream>
#include <stdexcept>
void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}

std::string calculate_result(const swiftedit::Csv &table, std::string_view expression) {
    const swiftedit::Calculation result = swiftedit::calculate(table, expression);
    return result.result;
}
std::string dependency_graph(std::size_t cells, bool shallow_first) {
    std::string result = shallow_first ? "=D1+B1,=C1,=D1" : "=B1+D1,=C1,=D1";
    for (std::size_t column = 3; column + 1 < cells; ++column) {
        result += ",=";
        result += swiftedit::cell_name({0, column + 1});
    }
    result += ",1";
    return result;
}
void verify_dependency_depth() {
    for (const bool shallow_first : {true, false}) {
        const swiftedit::Csv allowed(dependency_graph(64, shallow_first));
        const swiftedit::Calculation result = swiftedit::calculate_cell(allowed, {0, 0});
        check(result.result == "2", "A 64-cell dependency path is accepted in either order");
        const swiftedit::Csv excessive(dependency_graph(65, shallow_first));
        bool refused = false;
        try {
            static_cast<void>(swiftedit::convert_to_value(excessive, {0, 0}));
        } catch (const std::runtime_error &failure) {
            const std::string_view message = failure.what();
            refused = message.find("depth exceeds 64") != message.npos;
        }
        check(refused, "Cached dependencies must not bypass the 64-cell path limit");
        check(excessive.cell({0, 0}).value.starts_with('='),
              "Refused excessive-depth conversion preserves the formula");
    }
}
int main() {
    try {
        verify_dependency_depth();
        using namespace swiftedit;
        const Csv cancellable("2,=A1*3,=SUM(A1:B1)");
        std::stop_source stopped{};
        stopped.request_stop();
        for (const CellAddress address : {CellAddress{0, 0}, {0, 1}, {0, 2}}) {
            bool cancelled = false;
            try {
                static_cast<void>(calculate_cell(cancellable, address, stopped.get_token()));
            } catch (const CalculationCancelled &) { cancelled = true; }
            check(cancelled, "Cancellation is distinct from a literal or formula result");
        }
        bool expression_cancelled = false;
        try {
            static_cast<void>(calculate(cancellable, "SUM(A1:B1)", stopped.get_token()));
        } catch (const CalculationCancelled &) { expression_cancelled = true; }
        check(expression_cancelled && cancellable.cell({0, 2}).value == "=SUM(A1:B1)",
              "Cancelled evaluation preserves stored formula source");
        const Calculation after_cancel = calculate_cell(cancellable, {0, 2});
        check(after_cancel.result == "8", "Independent evaluation remains usable after cancellation");
        for (const std::size_t zeros : {0U, 31U, 32U, 10000U}) {
            const Csv literals(std::string(zeros, '0') + "2,=A1+A1");
            const Calculation value = calculate_cell(literals, {0, 1});
            check(value.result == "4" && value.references.size() == 2 &&
                      value.references[0] == CellAddress{0, 0} &&
                      value.references[1] == CellAddress{0, 0},
                  "Short and long literal operands preserve exact repeated references");
        }
        Csv csv("\"a,b\",\"quoted \"\"word\"\"\",\"line\r\nbreak\"\r\n1,2,3\n4,,6");
        check(csv.rows().size() == 3 && csv.cell({0, 2}).value == "line\r\nbreak",
              "embedded newlines");
        check(csv.cell({0, 1}).value == "quoted \"word\"", "escaped quotes");
        const std::string changed = csv.set({1, 1}, "x,y");
        check(changed == "\"a,b\",\"quoted \"\"word\"\"\",\"line\r\nbreak\"\r\n1,\"x,y\",3\n4,,6",
              "localized serialization preserves unaffected source");
        const std::string cleared = Csv("1,2,3\r\n4,5,6\n").clear({0, 1}, {1, 2});
        check(cleared == "1,,\r\n4,,\n", "clear preserves delimiters rows and line endings");
        const std::string quoted_source =
            "\"keep,one\",\"multi\r\nline\",tail\r\n"
            "keep,\"escaped \"\"quote\"\"\",tail\nlast,,\"keep,three\"";
        const Csv quoted_rectangle(quoted_source);
        const std::string quoted_clear = quoted_rectangle.clear({0, 1}, {1, 1});
        check(quoted_clear == "\"keep,one\",,tail\r\nkeep,,tail\nlast,,\"keep,three\"",
              "Clear preserves untouched quoting and mixed record endings exactly");
        const std::string empty_clear = quoted_rectangle.clear({2, 1}, {2, 1});
        check(empty_clear == quoted_source, "Clearing an empty field preserves source bytes");
        const Csv ragged("a,b,c\r\nd\ne,f,g");
        check(ragged.clear_all() == ",,\r\n\n,,",
              "Clear all retains ragged row lengths and mixed endings");
        check(csv.clear_all() == ",,\r\n,,\n,,",
              "Clear all removes quoted multiline contents, retaining record separators");
        check(Csv("").clear_all().empty(), "Clear all accepts an empty table");
        bool ragged_refused = false;
        try {
            static_cast<void>(ragged.clear({0, 1}, {2, 2}));
        } catch (const std::runtime_error &) {
            ragged_refused = true;
        }
        check(ragged_refused && ragged.cell({0, 1}).value == "b" &&
                  ragged.cell({2, 2}).value == "g",
              "Rectangle containing nonexistent cells refuses atomically without inventing fields");
        const Csv trailing_empty("a,b,");
        check(trailing_empty.rows()[0].size() == 3, "trailing empty field");
        const Csv empty_document("");
        check(empty_document.rows()[0].size() == 1, "empty document cell");
        {
            bool refused = false;
            try {
                Csv("\"bad");
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                Csv("\"bad\"tail");
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                Csv("b\"ad");
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        const CellAddress address = cell_address("AA12");
        check(address == CellAddress{11, 26}, "addresses");
        const std::string address_name = cell_name({11, 26});
        check(address_name == "AA12", "addresses");
        {
            bool refused = false;
            try {
                static_cast<void>(cell_address("A0"));
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        Csv nums("0.1,0.2,3\r\n4,5,6\r\ntext,,=SUM(A1:B1)");
        const std::string calculated_1 = calculate_result(nums, "=A1+B1");
        check(calculated_1 == "0.3", "decimal exactness");
        const std::string calculated_2 = calculate_result(nums, "10/4");
        check(calculated_2 == "2.5", "finite division");
        {
            bool refused = false;
            try {
                calculate_result(nums, "10/3");
            } catch (const std::exception &failure) {
                refused = true;
                const std::string_view message = failure.what();
                check(message.find("Rounding required") != message.npos, "Wrong refusal reason");
            }
            check(refused, "Expected refusal");
        }
        const std::string calculated_3 = calculate_result(nums, "ROUND(10/3,2)");
        check(calculated_3 == "3.33", "explicit rounding");
        const std::string calculated_4 = calculate_result(nums, "ROUND(-1.235,2)");
        check(calculated_4 == "-1.24", "half away from zero");
        const std::string calculated_5 = calculate_result(nums, "SUM(A1:B2,C1)");
        check(calculated_5 == "12.3", "rectangle and comma list");
        const std::string calculated_6 = calculate_result(nums, "SUM(A1 : B1)");
        check(calculated_6 == "0.3", "spaced range");
        const std::string calculated_7 = calculate_result(nums, "AVERAGE(A2:C2)");
        check(calculated_7 == "5", "average");
        const std::string calculated_8 = calculate_result(nums, "MIN(A2:C2)");
        check(calculated_8 == "4", "min max");
        const std::string calculated_9 = calculate_result(nums, "MAX(A2:C2)");
        check(calculated_9 == "6", "min max");
        const std::string calculated_10 = calculate_result(nums, "COUNT(A1:C2)");
        check(calculated_10 == "6", "strict numeric count");
        const std::string calculated_11 = calculate_result(nums, "ABS(-3.5)");
        check(calculated_11 == "3.5", "absolute");
        const std::string calculated_12 = calculate_result(nums, "FLOOR(-1.2)");
        check(calculated_12 == "-2", "negative rounding directions");
        const std::string calculated_13 = calculate_result(nums, "CEILING(-1.2)");
        check(calculated_13 == "-1", "negative rounding directions");
        const std::string calculated_14 = calculate_result(nums, "MOD(-5,3)");
        check(calculated_14 == "1", "floor-based remainder");
        {
            bool refused = false;
            try {
                calculate_result(nums, "SUM(A1:A3)");
            } catch (const std::exception &failure) {
                refused = true;
                const std::string_view message = failure.what();
                check(message.find("A3") != message.npos, "Wrong refusal reason");
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                calculate_result(nums, "SUM(B1:B3)");
            } catch (const std::exception &failure) {
                refused = true;
                const std::string_view message = failure.what();
                check(message.find("B3") != message.npos, "Wrong refusal reason");
            }
            check(refused, "Expected refusal");
        }
        const std::string dependent = calculate_result(nums, "C3");
        check(dependent == "0.3", "Stored formula references calculate exactly");
        const Csv formulas("2,=A1*3,=B1+1\r\nunchanged,tail,\"quoted\"");
        const Calculation formula_value = calculate_cell(formulas, {0, 2});
        check(formula_value.result == "7", "Chained formula result");
        const std::string edited_source = formulas.set({0, 0}, "4");
        const Csv edited_formulas(edited_source);
        const Calculation refreshed = calculate_cell(edited_formulas, {0, 2});
        check(refreshed.result == "13", "Changed inputs never reuse stale formula results");
        const std::string converted = convert_to_value(formulas, {0, 2});
        check(converted == "2,=A1*3,7\r\nunchanged,tail,\"quoted\"",
              "Convert to Value changes only the selected formula source span");
        const Csv cyclic("=B1,=A1");
        bool cycle_refused = false;
        try {
            static_cast<void>(convert_to_value(cyclic, {0, 0}));
        } catch (const std::runtime_error &failure) {
            cycle_refused = true;
            const std::string_view message = failure.what();
            check(message.find("Circular") != message.npos, "Cycle diagnostic");
        }
        check(cycle_refused && cyclic.cell({0, 0}).value == "=B1",
              "Failed conversion preserves the formula");
        {
            bool refused = false;
            try {
                calculate_result(nums, "1/0");
            } catch (const std::exception &failure) {
                refused = true;
                const std::string_view message = failure.what();
                check(message.find("zero") != message.npos, "Wrong refusal reason");
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                calculate_result(nums, "999999999999999999*99");
            } catch (const std::exception &failure) {
                refused = true;
                const std::string_view message = failure.what();
                check(message.find("capacity") != message.npos, "Wrong refusal reason");
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                calculate_result(nums, "RAND()");
            } catch (const std::exception &failure) {
                refused = true;
            }
            check(refused, "Expected refusal");
        }
        {
            bool refused = false;
            try {
                calculate_result(nums, "SUM(C2:A1)");
            } catch (const std::exception &failure) {
                refused = true;
                const std::string_view message = failure.what();
                check(message.find("Reversed") != message.npos, "Wrong refusal reason");
            }
            check(refused, "Expected refusal");
        }
        const std::vector<CellAddress> refs = calculate(nums, "SUM(A1:B1,C2)").references;
        check(refs == std::vector<CellAddress>{{0, 0}, {0, 1}, {1, 2}},
              "references available for hover highlighting");
        std::cout << "CSV tests passed: lossless local edits, quoting, clear rectangle, exact "
                     "decimal arithmetic, strict errors, rounding and reference metadata.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
