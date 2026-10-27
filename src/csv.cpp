#include "csv.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>

namespace swiftedit {
CellAddress cell_address(std::string_view name) {
    std::size_t i = 0, col = 0, row = 0;
    while (i < name.size() && name[i] >= 'A' && name[i] <= 'Z') {
        if (col > 100000)
            throw std::runtime_error("Column address is too large.");
        col = col * 26 + name[i++] - 'A' + 1;
    }
    if (!col || i == name.size() || name[i] == '0')
        throw std::runtime_error("Expected a cell address such as A1.");
    while (i < name.size() && name[i] >= '0' && name[i] <= '9') {
        if (row > 100000)
            throw std::runtime_error("Row address is too large.");
        row = row * 10 + name[i++] - '0';
    }
    if (i != name.size() || !row)
        throw std::runtime_error("Invalid cell address.");
    const CellAddress result{row - 1, col - 1};
    return result;
}
std::string cell_name(CellAddress p) {
    std::string s{};
    std::size_t n = p.column + 1;
    while (n) {
        s.insert(s.begin(), static_cast<char>('A' + (n - 1) % 26));
        n = (n - 1) / 26;
    }
    s += std::to_string(p.row + 1);
    return s;
}
Csv::Csv(std::string_view source) : source_(source) {
    std::size_t i = 0, count = 0;
    std::vector<Cell> row{};
    for (;;) {
        ++count;
        if (count > 100000)
            throw std::runtime_error("CSV table view is limited to 100000 cells.");
        Cell c{};
        c.begin = i;
        if (i < source.size() && source[i] == '"') {
            ++i;
            bool closed = false;
            while (i < source.size()) {
                if (source[i] == '"') {
                    ++i;
                    if (i < source.size() && source[i] == '"') {
                        c.value += '"';
                        ++i;
                    } else {
                        closed = true;
                        break;
                    }
                } else {
                    c.value += source[i];
                    ++i;
                }
            }
            if (!closed)
                throw std::runtime_error("Unclosed quoted CSV field.");
            if (i < source.size() && source[i] != ',' && source[i] != '\r' && source[i] != '\n')
                throw std::runtime_error("Unexpected text after quoted CSV field.");
        } else {
            while (i < source.size() && source[i] != ',' && source[i] != '\r' &&
                   source[i] != '\n') {
                if (source[i] == '"')
                    throw std::runtime_error("Quote inside an unquoted CSV field.");
                c.value += source[i];
                ++i;
            }
        }
        c.end = i;
        row.push_back(std::move(c));
        if (i == source.size()) {
            rows_.push_back(std::move(row));
            break;
        }
        if (source[i] == ',') {
            ++i;
            continue;
        }
        const char ending = source[i];
        ++i;
        if (ending == '\r' && i < source.size() && source[i] == '\n')
            ++i;
        rows_.push_back(std::move(row));
        row.clear();
        if (i == source.size())
            break;
    }
}
const Cell &Csv::cell(CellAddress p) const {
    if (p.row >= rows_.size() || p.column >= rows_[p.row].size())
        throw std::runtime_error("Cell " + cell_name(p) + " does not exist.");
    return rows_[p.row][p.column];
}
std::string Csv::set(CellAddress p, std::string_view value) const {
    const Cell &c = cell(p);
    std::string encoded{};
    if (value.size() > (encoded.max_size() - 2) / 2)
        throw std::length_error("Encoded CSV field is too large.");
    encoded.reserve(value.size() * 2 + 2);
    if (value.find_first_of(",\"\r\n") != value.npos) {
        encoded += '"';
        for (char ch : value) {
            encoded += ch;
            if (ch == '"')
                encoded += '"';
        }
        encoded += '"';
    } else
        encoded = value;
    std::string result = source_;
    result.replace(c.begin, c.end - c.begin, encoded);
    return result;
}
std::string Csv::clear(CellAddress first, CellAddress last) const {
    if (first.row > last.row || first.column > last.column)
        throw std::runtime_error("Reversed CSV selection.");
    cell(first);
    cell(last);
    // Validate the whole rectangle before constructing a replacement. The
    // immutable source owns field offsets; no decoded field copies are needed.
    for (std::size_t r = first.row; r <= last.row; ++r)
        for (std::size_t c = first.column; c <= last.column; ++c)
            static_cast<void>(cell({r, c}));
    std::string result{};
    result.reserve(source_.size());
    std::size_t copied_through = 0;
    // Retain the gaps between selected fields in source order. Repeated erase
    // would shift an ever-growing suffix once per cell (quadratic copy work).
    for (std::size_t row = first.row; row <= last.row; ++row) {
        for (std::size_t column = first.column; column <= last.column; ++column) {
            const Cell &selected = rows_[row][column];
            result.append(source_, copied_through, selected.begin - copied_through);
            copied_through = selected.end;
        }
    }
    result.append(source_, copied_through, source_.size() - copied_through);
    return result;
}
std::string Csv::clear_all() const {
    std::string result{};
    result.reserve(source_.size());
    std::size_t copied_through = 0;
    for (const std::vector<Cell> &row : rows_) {
        for (const Cell &selected : row) {
            result.append(source_, copied_through, selected.begin - copied_through);
            copied_through = selected.end;
        }
    }
    result.append(source_, copied_through, source_.size() - copied_through);
    return result;
}
namespace {
void check_cancelled(const std::stop_token &cancellation) {
    if (cancellation.stop_requested())
        throw CalculationCancelled();
}
using Integer = std::int64_t;
constexpr Integer limit = std::numeric_limits<Integer>::max() / 10;
Integer magnitude(Integer n) {
    const Integer value = n < 0 ? -n : n;
    return value;
}
Integer mul(Integer a, Integer b) {
    if (b && magnitude(a) > limit / magnitude(b))
        throw std::runtime_error("Exact arithmetic capacity exceeded.");
    const Integer product = a * b;
    return product;
}
Integer add(Integer a, Integer b) {
    if ((b > 0 && a > limit - b) || (b < 0 && a < -limit - b))
        throw std::runtime_error("Exact arithmetic capacity exceeded.");
    const Integer sum = a + b;
    return sum;
}
struct Number {
    Integer n{}, d{1};
    Number(Integer a = 0, Integer b = 1) : n(a), d(b) {
        if (!d)
            throw std::runtime_error("Division by zero.");
        if (d < 0) {
            n = -n;
            d = -d;
        }
        Integer g = std::gcd(magnitude(n), d);
        n /= g;
        d /= g;
    }
};
Number plus(Number a, Number b) {
    Integer g = std::gcd(a.d, b.d);
    const Integer left = mul(a.n, b.d / g);
    const Integer right = mul(b.n, a.d / g);
    const Integer numerator = add(left, right);
    const Integer denominator = mul(a.d, b.d / g);
    const Number result{numerator, denominator};
    return result;
}
Number times(Number a, Number b) {
    Integer g = std::gcd(magnitude(a.n), b.d), h = std::gcd(magnitude(b.n), a.d);
    const Integer numerator = mul(a.n / g, b.n / h);
    const Integer denominator = mul(a.d / h, b.d / g);
    const Number result{numerator, denominator};
    return result;
}
Number divide(Number a, Number b) {
    if (!b.n)
        throw std::runtime_error("Division by zero.");
    const Number reciprocal{b.d, b.n};
    const Number result = times(a, reciprocal);
    return result;
}
bool less(Number a, Number b) {
    const Number negative{-b.n, b.d};
    const Number difference = plus(a, negative);
    const bool result = difference.n < 0;
    return result;
}
struct CancellableLess {
    const std::stop_token &cancellation;
    bool operator()(Number first, Number second) const {
        check_cancelled(cancellation);
        const bool result = less(first, second);
        return result;
    }
};
Number numeric(std::string_view s, const std::stop_token &cancellation) {
    check_cancelled(cancellation);
    if (s.empty())
        throw std::runtime_error("Blank is not numeric.");
    bool negative = false;
    std::size_t i = 0;
    if (s[i] == '-' || s[i] == '+') {
        negative = s[i] == '-';
        ++i;
    }
    Integer n = 0, d = 1;
    bool point = false, digit = false;
    for (; i < s.size(); ++i) {
        if (i % 256 == 0)
            check_cancelled(cancellation);
        const char c = s[i];
        if (c == '.' && !point) {
            point = true;
            continue;
        }
        if (c < '0' || c > '9')
            throw std::runtime_error("Non-numeric content; no coercion is allowed.");
        digit = true;
        n = add(mul(n, 10), c - '0');
        if (point)
            d = mul(d, 10);
    }
    if (!digit)
        throw std::runtime_error("Non-numeric content.");
    const Number result{negative ? -n : n, d};
    return result;
}
std::string decimal(Number a) {
    Integer den = a.d;
    while (den % 2 == 0)
        den /= 2;
    while (den % 5 == 0)
        den /= 5;
    if (den != 1)
        throw std::runtime_error("Rounding required: use ROUND(expression, decimal_places).");
    Integer n = magnitude(a.n);
    std::string out = (a.n < 0 ? "-" : "") + std::to_string(n / a.d);
    n %= a.d;
    if (n)
        out += '.';
    while (n) {
        n *= 10;
        out += static_cast<char>('0' + n / a.d);
        n %= a.d;
    }
    return out;
}
struct ActiveEvaluation {
    CellAddress address{};
    std::size_t depth{1};
};
struct CachedNumber {
    Number value{};
    std::size_t depth{1};
};
struct Evaluation {
    const Csv &table;
    std::stop_token cancellation{};
    std::vector<ActiveEvaluation> active{};
    std::map<std::pair<std::size_t, std::size_t>, CachedNumber> values{};
    std::size_t references{};
};
Number evaluate_cell(Evaluation &, CellAddress);
class Parser {
public:
    Parser(Evaluation &evaluation, std::string_view expression)
        : evaluation_(evaluation), table_(evaluation.table), s_(expression) {
        if (s_.size() > 4096)
            throw std::runtime_error("Expression exceeds 4096 bytes.");
        if (s_.starts_with('='))
            ++at_;
    }
    Calculation run() {
        const Number n = run_number();
        Calculation result{};
        result.result = decimal(n);
        result.references = std::move(references_);
        return result;
    }
    Number run_number() {
        check_cancelled(evaluation_.cancellation);
        const Number n = expression();
        space();
        if (at_ != s_.size())
            throw std::runtime_error("Unexpected formula text.");
        check_cancelled(evaluation_.cancellation);
        return n;
    }

private:
    Evaluation &evaluation_;
    const Csv &table_;
    std::string_view s_{};
    std::size_t at_{}, depth_{};
    std::vector<CellAddress> references_{};
    void space() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t'))
            ++at_;
    }
    bool take(char c) {
        space();
        if (at_ < s_.size() && s_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }
    void need(char c) {
        const bool taken = take(c);
        if (!taken)
            throw std::runtime_error(std::string("Expected '") + c + "'.");
    }
    Number expression() {
        Number n = term();
        while (true) {
            const bool addition = take('+');
            if (addition) {
                const Number operand = term();
                n = plus(n, operand);
                continue;
            }
            const bool subtraction = take('-');
            if (subtraction) {
                const Number rhs = term();
                n = plus(n, {-rhs.n, rhs.d});
            } else
                return n;
        }
    }
    Number term() {
        Number n = atom();
        while (true) {
            const bool multiplication = take('*');
            if (multiplication) {
                const Number operand = atom();
                n = times(n, operand);
                continue;
            }
            const bool division = take('/');
            if (division) {
                const Number operand = atom();
                n = divide(n, operand);
            } else
                return n;
        }
    }
    Number reference(CellAddress p) {
        check_cancelled(evaluation_.cancellation);
        if (evaluation_.references >= 100000)
            throw std::runtime_error("Too many referenced cells.");
        ++evaluation_.references;
        references_.push_back(p);
        try {
            const Number result = evaluate_cell(evaluation_, p);
            return result;
        } catch (const CalculationCancelled &) {
            throw;
        } catch (const std::exception &e) {
            throw std::runtime_error(cell_name(p) + ": " + e.what());
        }
    }
    std::string name() {
        space();
        const std::size_t start = at_;
        while (at_ < s_.size() && s_[at_] >= 'A' && s_[at_] <= 'Z')
            ++at_;
        const std::string result(s_.substr(start, at_ - start));
        return result;
    }
    CellAddress address() {
        space();
        const std::size_t start = at_;
        name();
        while (at_ < s_.size() && s_[at_] >= '0' && s_[at_] <= '9')
            ++at_;
        const std::string_view name = s_.substr(start, at_ - start);
        const CellAddress result = cell_address(name);
        return result;
    }
    std::vector<Number> arguments() {
        std::vector<Number> values{};
        const bool closed = take(')');
        if (closed)
            return values;
        bool more = false;
        do {
            space();
            const std::size_t saved = at_;
            bool range = false;
            if (at_ < s_.size() && s_[at_] >= 'A' && s_[at_] <= 'Z') {
                name();
                const std::size_t digits = at_;
                while (at_ < s_.size() && s_[at_] >= '0' && s_[at_] <= '9')
                    ++at_;
                bool colon = false;
                if (at_ > digits)
                    colon = take(':');
                if (colon) {
                    at_ = saved;
                    const CellAddress first = address();
                    need(':');
                    const CellAddress last = address();
                    if (first.row > last.row || first.column > last.column)
                        throw std::runtime_error("Reversed cell range.");
                    table_.cell(first);
                    table_.cell(last);
                    for (std::size_t r = first.row; r <= last.row; ++r)
                        for (std::size_t c = first.column; c <= last.column; ++c) {
                            const Number value = reference({r, c});
                            values.push_back(value);
                        }
                    range = true;
                }
            }
            if (!range) {
                at_ = saved;
                const Number value = expression();
                values.push_back(value);
            }
            more = take(',');
        } while (more);
        need(')');
        return values;
    }
    static void require_argument_count(const std::string &name, std::size_t actual,
                                       std::size_t expected) {
        if (actual != expected)
            throw std::runtime_error(name + ": wrong argument count.");
    }
    Number function(const std::string &name, std::vector<Number> v) {
        if (name == "ROUND") {
            require_argument_count(name, v.size(), 2);
            if (v[1].d != 1 || v[1].n < 0 || v[1].n > 15)
                throw std::runtime_error("ROUND places must be an integer from 0 to 15.");
            Integer scale = 1;
            for (Integer i = 0; i < v[1].n; ++i)
                scale = mul(scale, 10);
            const Number a = times(v[0], {scale});
            Integer n = magnitude(a.n), q = n / a.d, r = n % a.d;
            if (r >= a.d - r)
                q = add(q, 1);
            const Number result{a.n < 0 ? -q : q, scale};
            return result;
        }
        if (name == "ABS") {
            require_argument_count(name, v.size(), 1);
            const Integer absolute = magnitude(v[0].n);
            const Number result{absolute, v[0].d};
            return result;
        }
        if (name == "FLOOR" || name == "CEILING") {
            require_argument_count(name, v.size(), 1);
            const Number a = v[0];
            Integer n = a.n / a.d;
            if (a.n % a.d) {
                if (name == "FLOOR" && a.n < 0)
                    --n;
                if (name == "CEILING" && a.n > 0)
                    ++n;
            }
            const Number result{n};
            return result;
        }
        if (name == "MOD") {
            require_argument_count(name, v.size(), 2);
            const Number ratio = divide(v[0], v[1]);
            Integer q = ratio.n / ratio.d;
            if (ratio.n < 0 && ratio.n % ratio.d)
                --q;
            const Number p = times({q}, v[1]);
            const Number negative{-p.n, p.d};
            const Number result = plus(v[0], negative);
            return result;
        }
        if (v.empty())
            throw std::runtime_error(name + ": expected numeric operands.");
        if (name == "COUNT") {
            const Integer count = static_cast<Integer>(v.size());
            const Number result{count};
            return result;
        }
        if (name == "SUM" || name == "AVERAGE") {
            Number n{};
            for (const Number a : v) {
                check_cancelled(evaluation_.cancellation);
                n = plus(n, a);
            }
            if (name == "AVERAGE") {
                const Number count{static_cast<Integer>(v.size())};
                n = divide(n, count);
            }
            return n;
        }
        if (name == "MIN") {
            const std::vector<Number>::const_iterator minimum =
                std::min_element(v.begin(), v.end(), CancellableLess{evaluation_.cancellation});
            return *minimum;
        }
        if (name == "MAX") {
            const std::vector<Number>::const_iterator maximum =
                std::max_element(v.begin(), v.end(), CancellableLess{evaluation_.cancellation});
            return *maximum;
        }
        throw std::runtime_error("Unsupported function: " + name);
    }
    Number atom() {
        ++depth_;
        if (depth_ > 32)
            throw std::runtime_error("Formula nesting exceeds 32 levels.");
        struct Depth {
            std::size_t &d;
            ~Depth() { --d; }
        } depth{depth_};
        space();
        const bool positive = take('+');
        if (positive) {
            const Number result = atom();
            return result;
        }
        const bool negative = take('-');
        if (negative) {
            Number n = atom();
            const Number result{-n.n, n.d};
            return result;
        }
        const bool grouped = take('(');
        if (grouped) {
            Number n = expression();
            need(')');
            return n;
        }
        const std::size_t start = at_;
        if (at_ < s_.size() && s_[at_] >= 'A' && s_[at_] <= 'Z') {
            const std::string id = name();
            const bool function_call = take('(');
            if (function_call) {
                std::vector<Number> operands = arguments();
                const Number result = function(id, std::move(operands));
                return result;
            }
            at_ = start;
            const CellAddress location = address();
            const Number result = reference(location);
            return result;
        }
        while (at_ < s_.size() && ((s_[at_] >= '0' && s_[at_] <= '9') || s_[at_] == '.'))
            ++at_;
        const std::string_view digits = s_.substr(start, at_ - start);
        const Number result = numeric(digits, evaluation_.cancellation);
        return result;
    }
};
Number evaluate_cell(Evaluation &evaluation, CellAddress address) {
    check_cancelled(evaluation.cancellation);
    const Cell &cell = evaluation.table.cell(address);
    // Short literals have no expensive subtree to memoize. Avoid one map node
    // per common range operand, but keep caching long numeric strings so many
    // references cannot repeatedly scan a large run of leading zeroes.
    // Both paths retain identical dependency-depth admission.
    if (!cell.value.starts_with('=') && cell.value.size() <= 32) {
        if (evaluation.active.size() >= 64)
            throw std::runtime_error("Formula dependency depth exceeds 64 cells.");
        const Number literal = numeric(cell.value, evaluation.cancellation);
        if (!evaluation.active.empty()) {
            ActiveEvaluation &parent = evaluation.active.back();
            parent.depth = std::max(parent.depth, std::size_t(2));
        }
        return literal;
    }
    const std::pair<std::size_t, std::size_t> key{address.row, address.column};
    const std::map<std::pair<std::size_t, std::size_t>, CachedNumber>::const_iterator cached =
        evaluation.values.find(key);
    if (cached != evaluation.values.end()) {
        const CachedNumber &value = (*cached).second;
        if (value.depth > 64 - evaluation.active.size())
            throw std::runtime_error("Formula dependency depth exceeds 64 cells.");
        if (!evaluation.active.empty()) {
            ActiveEvaluation &parent = evaluation.active.back();
            parent.depth = std::max(parent.depth, value.depth + 1);
        }
        return value.value;
    }
    for (const ActiveEvaluation &active : evaluation.active)
        if (active.address == address)
            throw std::runtime_error("Circular formula reference.");
    if (evaluation.active.size() >= 64)
        throw std::runtime_error("Formula dependency depth exceeds 64 cells.");
    Number result{};
    std::size_t depth = 1;
    if (cell.value.starts_with('=')) {
        evaluation.active.push_back({address, 1});
        struct ActiveCell {
            std::vector<ActiveEvaluation> &stack;
            ~ActiveCell() { stack.pop_back(); }
        } active{evaluation.active};
        Parser parser(evaluation, cell.value);
        result = parser.run_number();
        // A referenced formula must itself have a valid displayable result.
        const std::string validated_decimal = decimal(result);
        static_cast<void>(validated_decimal);
        depth = evaluation.active.back().depth;
    } else
        result = numeric(cell.value, evaluation.cancellation);
    evaluation.values.emplace(key, CachedNumber{result, depth});
    if (!evaluation.active.empty()) {
        ActiveEvaluation &parent = evaluation.active.back();
        parent.depth = std::max(parent.depth, depth + 1);
    }
    return result;
}
} // namespace
Calculation calculate(const Csv &table, std::string_view expression, std::stop_token cancellation) {
    check_cancelled(cancellation);
    Evaluation evaluation{table, cancellation};
    Parser parser(evaluation, expression);
    Calculation result = parser.run();
    check_cancelled(cancellation);
    return result;
}
Calculation calculate_cell(const Csv &table, CellAddress address, std::stop_token cancellation) {
    check_cancelled(cancellation);
    const Cell &cell = table.cell(address);
    if (!cell.value.starts_with('=')) {
        const Calculation literal{cell.value, {}};
        check_cancelled(cancellation);
        return literal;
    }
    Evaluation evaluation{table, cancellation};
    evaluation.active.push_back({address, 1});
    Parser parser(evaluation, cell.value);
    Calculation result = parser.run();
    check_cancelled(cancellation);
    return result;
}
std::string convert_to_value(const Csv &table, CellAddress address) {
    const Cell &cell = table.cell(address);
    if (!cell.value.starts_with('='))
        throw std::runtime_error("The selected cell does not contain a formula.");
    const Calculation value = calculate_cell(table, address);
    std::string result = table.set(address, value.result);
    return result;
}
} // namespace swiftedit
