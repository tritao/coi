#ifndef COI_NATIVE_RUNTIME_SOKOL_INCLUDED
#define COI_NATIVE_RUNTIME_SOKOL_INCLUDED
    #define SOKOL_NO_ENTRY
    #define SOKOL_GLCORE
    #define SOKOL_IMPL
    #define SOKOL_GL_IMPL
    #define SOKOL_DEBUGTEXT_IMPL
    #if defined(COI_NATIVE_FONTSTASH)
        #define FONTSTASH_IMPLEMENTATION
    #endif
    #if defined(COI_NATIVE_CAPTURE)
        #define STB_IMAGE_WRITE_STATIC
        #define STB_IMAGE_WRITE_IMPLEMENTATION
    #endif
    #include "sokol_app.h"
    #include "sokol_gfx.h"
    #include "sokol_gl.h"
    #include "sokol_glue.h"
    #include "sokol_time.h"
    #include "sokol_debugtext.h"
	    #if defined(COI_NATIVE_FONTSTASH)
	        #include "fontstash.h"
	        #include "sokol_fontstash.h"
	    #endif
	    #ifndef COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED
	    #define COI_NATIVE_RUNTIME_STB_IMAGE_INCLUDED
	        #define STB_IMAGE_STATIC
	        #define STB_IMAGE_IMPLEMENTATION
	        #include "stb_image.h"
	    #endif
	    #if defined(COI_NATIVE_CAPTURE)
	        #include "stb_image_write.h"
	    #endif
#endif

#if defined(COI_NATIVE_CLAY)
#ifndef COI_NATIVE_RUNTIME_CLAY_INCLUDED
#define COI_NATIVE_RUNTIME_CLAY_INCLUDED
    #define CLAY_IMPLEMENTATION
    #include "clay.h"
#endif
#endif

#if defined(COI_NATIVE_CLAY) && defined(COI_NATIVE_FONTSTASH)
#ifndef COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED
#define COI_NATIVE_RUNTIME_SOKOL_CLAY_INCLUDED
    // Clay's reference sokol renderer + fontstash-based text measurement.
    #define SOKOL_CLAY_IMPL
    #include "renderers/sokol/sokol_clay.h"
#endif
#endif
