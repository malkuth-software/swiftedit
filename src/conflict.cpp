#include "editor.hpp"
#include <stdexcept>

namespace notepad {
namespace {
std::filesystem::path reviewed_path(std::string_view text) {
    if (text.empty() || text.find('\0') != std::string::npos)
        throw std::runtime_error("Enter a nonempty filename without NUL characters.");
    const std::u8string utf8(text.begin(), text.end());
    const std::filesystem::path result = std::filesystem::absolute(std::filesystem::path(utf8));
    return result;
}
} // namespace
swiftedit::DocumentStamp Editor::save_stamp() const {
    swiftedit::DocumentStamp stamp = save_identity_.stamp();
    stamp.revision.value = counted_text_.revision();
    return stamp;
}
void Editor::build_conflict() {
    conflict_.root = gf::make_control<DialogLayout>(gf::StableId("swiftedit.conflict"));
    conflict_status_ = gf::make_control<gf::Label>(gf::StableId("conflict.status"));
    (*conflict_status_).set_use_mnemonic(false);
    (*conflict_status_).set_text_wrapping(gf::TextWrapping::word);
    (*conflict_.root).place(conflict_status_, {16, 16, 648, 96});
    conflict_over_ = gf::make_control<gf::Button>(gf::StableId("conflict.over"), "Save Over");
    conflict_copy_ = gf::make_control<gf::Button>(gf::StableId("conflict.copy"), "Save New Copy");
    (*conflict_.root).place(conflict_over_, {16, 116, 150, 34});
    (*conflict_.root).place(conflict_copy_, {178, 116, 170, 34});
    const std::shared_ptr<gf::Label> filename =
        gf::make_control<gf::Label>(gf::StableId("conflict.filename-label"), "Filename");
    (*conflict_.root).place(filename, {16, 164, 648, 24});
    conflict_path_ = gf::make_control<gf::TextBox>(gf::StableId("conflict.filename"));
    (*conflict_path_).set_accessible_name("Save filename including folder");
    (*conflict_.root).place(conflict_path_, {16, 190, 648, 32});
    const std::shared_ptr<gf::Label> encoding =
        gf::make_control<gf::Label>(gf::StableId("conflict.encoding-label"), "Encoding");
    const std::shared_ptr<gf::Label> endings =
        gf::make_control<gf::Label>(gf::StableId("conflict.endings-label"), "Line endings");
    (*conflict_.root).place(encoding, {16, 236, 310, 24});
    (*conflict_.root).place(endings, {340, 236, 324, 24});
    conflict_encoding_ = gf::make_control<gf::ComboBox>(gf::StableId("conflict.encoding"));
    (*conflict_encoding_).set_items({"UTF-8", "UTF-8 with BOM", "UTF-16 LE", "UTF-16 BE"});
    (*conflict_encoding_).set_accessible_name("Save encoding");
    conflict_endings_ = gf::make_control<gf::ComboBox>(gf::StableId("conflict.endings"));
    (*conflict_endings_).set_items({"Preserve exactly", "LF", "CRLF", "CR"});
    (*conflict_endings_).set_accessible_name("Save line endings");
    (*conflict_.root).place(conflict_encoding_, {16, 262, 310, 32});
    (*conflict_.root).place(conflict_endings_, {340, 262, 324, 32});
    conflict_check_ =
        gf::make_control<gf::Button>(gf::StableId("conflict.review"), "Review Destination");
    conflict_save_ = gf::make_control<gf::Button>(gf::StableId("conflict.save"), "Save");
    conflict_cancel_ = gf::make_control<gf::Button>(gf::StableId("conflict.cancel"), "Cancel");
    (*conflict_.root).place(conflict_check_, {16, 350, 210, 34});
    (*conflict_.root).place(conflict_save_, {410, 350, 120, 34});
    (*conflict_.root).place(conflict_cancel_, {542, 350, 122, 34});
    subscriptions_.push_back(
        (*conflict_over_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::conflict_over}));
    subscriptions_.push_back(
        (*conflict_copy_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::conflict_copy}));
    subscriptions_.push_back(
        (*conflict_check_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::conflict_review}));
    subscriptions_.push_back(
        (*conflict_save_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::conflict_save}));
    subscriptions_.push_back(
        (*conflict_cancel_)
            .clicked()
            .subscribe(*this, ButtonListener{observe(), ButtonAction::conflict_cancel}));
}
void Editor::begin_conflict(const std::filesystem::path &path, Continuation next) {
    const swiftedit::DocumentStamp stamp = save_stamp();
    std::unique_ptr<swiftedit::SaveReview> pending =
        std::make_unique<swiftedit::SaveReview>(path, stamp, document_.encoding);
    (*conflict_status_)
        .set_text("The destination changed outside SwiftEdit. Choose Save Over to replace it, Save "
                  "New Copy to keep it, or Cancel. Nothing has been written.");
    (*conflict_path_).set_text(path_utf8(path));
    (*conflict_encoding_).set_selected_index(static_cast<std::size_t>(document_.encoding));
    (*conflict_endings_).set_selected_index(0);
    (*conflict_over_).set_enabled(true);
    (*conflict_copy_).set_enabled(true);
    (*conflict_path_).set_enabled(false);
    (*conflict_encoding_).set_enabled(false);
    (*conflict_endings_).set_enabled(false);
    (*conflict_check_).set_enabled(false);
    (*conflict_save_).set_enabled(false);
    conflict_review_ = std::move(pending);
    conflict_next_ = next;
    cancel_search();
    set_enabled(false);
    static_cast<void>(find_.handle.hide());
    static_cast<void>(font_.handle.hide());
    static_cast<void>(characters_.handle.hide());
    static_cast<void>(controls_.handle.hide());
    static_cast<void>(conflict_.handle.show());
}
void Editor::cancel_conflict() {
    conflict_review_.reset();
    conflict_next_ = Continuation::none;
    static_cast<void>(conflict_.handle.hide());
    set_enabled(true);
    focus_text();
}
void Editor::conflict_action(ButtonAction action) {
    if (!conflict_review_)
        return;
    if (action == ButtonAction::conflict_cancel) {
        cancel_conflict();
        return;
    }
    bool published = false;
    try {
        swiftedit::SaveReview &review = *conflict_review_;
        if (action == ButtonAction::conflict_over || action == ButtonAction::conflict_copy) {
            const swiftedit::ConflictChoice choice = action == ButtonAction::conflict_over
                                                         ? swiftedit::ConflictChoice::save_over
                                                         : swiftedit::ConflictChoice::new_copy;
            review.choose(choice);
            (*conflict_path_).set_text(path_utf8(review.target()));
            (*conflict_path_).set_enabled(true);
            (*conflict_encoding_).set_enabled(true);
            (*conflict_endings_).set_enabled(true);
            (*conflict_check_).set_enabled(true);
            (*conflict_over_).set_enabled(false);
            (*conflict_copy_).set_enabled(false);
            (*conflict_save_).set_enabled(false);
            (*conflict_status_)
                .set_text(
                    "Review the filename, encoding and line endings below. Review Destination "
                    "checks the chosen file and displays the final warning before Save.");
            return;
        }
        const std::filesystem::path target = reviewed_path((*conflict_path_).text());
        const std::optional<std::size_t> encoding_index = (*conflict_encoding_).selected_index();
        const std::optional<std::size_t> ending_index = (*conflict_endings_).selected_index();
        if (!encoding_index || *encoding_index > 3 || !ending_index || *ending_index > 3)
            throw std::runtime_error("Choose an encoding and line-ending option.");
        const Encoding encoding = static_cast<Encoding>(*encoding_index);
        const swiftedit::SaveEndings endings = static_cast<swiftedit::SaveEndings>(*ending_index);
        if (action == ButtonAction::conflict_review) {
            review.review(target, encoding, endings);
            const std::string warning = review.destructive()
                                            ? "WARNING: Save will replace the existing file. Its "
                                              "current content will be lost."
                                            : "Save will create a new file and keep the original.";
            (*conflict_status_)
                .set_text(warning +
                          " Filename and format are shown below. If you change them, review the "
                          "destination again. Click Save to confirm, or Cancel.");
            (*conflict_save_).set_enabled(true);
            return;
        }
        if (action != ButtonAction::conflict_save)
            return;
        (*conflict_save_).set_enabled(false);
        if (!review.ready() || target != review.target() || encoding != review.encoding() ||
            endings != review.endings())
            throw std::runtime_error("The save details changed or were not reviewed. Review "
                                     "Destination again before Save.");
        std::string prepared((*text_).text());
        if (endings != swiftedit::SaveEndings::preserve) {
            const std::string_view sequence = endings == swiftedit::SaveEndings::lf     ? "\n"
                                              : endings == swiftedit::SaveEndings::crlf ? "\r\n"
                                                                                        : "\r";
            prepared = swiftedit::normalize_newlines(prepared, sequence);
        }
        if (gf::TextBox::validate_multiline_text(prepared) !=
            gf::TextBox::MultilineValidation::valid)
            throw std::runtime_error(
                "Converted text exceeds the current document or line limit. No changes made.");
        review.publish(document_, (*text_).text(), save_stamp());
        published = true;
        const Continuation next = conflict_next_;
        // Retire write authority before touching widgets that can emit callbacks.
        cancel_conflict();
        if ((*text_).text() != prepared)
            (*text_).set_text(prepared);
        (*text_).clear_undo_history();
        refresh();
        if (next != Continuation::none)
            continue_operation(next);
    } catch (const std::exception &failure) {
        if (published)
            error(std::string("The file was saved, but refreshing its display failed. The saved "
                              "snapshot is retained. ") +
                  failure.what());
        else {
            (*conflict_save_).set_enabled(false);
            (*conflict_status_)
                .set_text(std::string(failure.what()) +
                          " No file was written. Cancel or review the destination again.");
        }
    }
}
} // namespace notepad
