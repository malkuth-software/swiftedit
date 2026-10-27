#pragma once
#include <gui_forms/gui_forms.hpp>
#include <stdexcept>

// Renderer identity is existing public diagnostic evidence. Reject the known
// fontless host path before claiming native visual or idle results. This checks
// host readiness, not glyph coverage or visual correctness of every character.
// Window exposes metrics through a non-const accessor; this helper only reads.
inline void require_native_fonts(gui_forms::Window &window) {
#if defined(__APPLE__) || defined(_WIN32)
    const gui_forms::MetricsSnapshot metrics = window.metrics().snapshot();
    if (metrics.renderer_name.empty() ||
        metrics.renderer_name.find("incomplete bundled font pack") != std::string::npos)
        throw std::runtime_error("Native fixture requires loaded bundled fonts: " +
                                 metrics.renderer_name);
#else
    static_cast<void>(window);
#endif
}
