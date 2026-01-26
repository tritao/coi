#pragma once

#if defined(COI_NATIVE_CLAY)
// Forward declaration; full definition comes from `clay.h` (included via deps.h).
struct Clay_Vector2;

// Returns the scroll offset for the currently-open Clay layout element.
// Implemented in `src/runtime/third_party/impl.cc` (where CLAY_IMPLEMENTATION is defined).
Clay_Vector2 coi_native_clay_scroll_offset_for_open_element(void);
#endif
