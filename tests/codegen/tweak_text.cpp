// Compiled to assembly by check_tweak_text.cmake: a shipping build carries none of a tweak's
// tool text (D256).
#include <baker/tweak.hpp>

baker::Tweak<float> kBoxWidth{"box.width",
                              2.0f,
                              {.range = {0, 10},
                               .step = 0.5,
                               .desc = "TOOL_TEXT_DESC",
                               .kind = "TOOL_TEXT_KIND",
                               .group = "TOOL_TEXT_GROUP"}};

float read_box_width() {
    return kBoxWidth;
}
