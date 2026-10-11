// Compiled to assembly by check_tweak_read.cmake: reading a Tweak<float> must be one load.
#include <moira/tweak.hpp>

extern moira::Tweak<float> kBoxWidth;

float read_box_width() {
    return kBoxWidth;
}
