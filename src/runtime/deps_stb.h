#pragma once

// stb_image / stb_image_write (no implementation macros here; see vendor_impl.cc).

#ifndef COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED
#define COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED (1)
#endif
#include "stb_image.h"

#if defined(COI_NATIVE_CAPTURE)
#include "stb_image_write.h"
#endif

