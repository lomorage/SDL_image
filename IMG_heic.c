/*
  SDL_image:  An example image loading library for use with SDL
  Copyright (C) 1997-2012 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#if !defined(__APPLE__) || defined(SDL_IMAGE_USE_COMMON_BACKEND)

/* This is a JPEG image file loading framework */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "SDL_image.h"

#ifdef LOAD_HEIC

#include <libheif/heif.h>

static struct {
    int loaded;
    void *handle;
} lib;

#ifdef LOAD_HEIC_DYNAMIC
int IMG_InitHEIC()
{
    if ( lib.loaded == 0 ) {
        lib.handle = SDL_LoadObject(LOAD_HEIC_DYNAMIC);
        if ( lib.handle == NULL ) {
            return -1;
        }
    }
    ++lib.loaded;

    return 0;
}
void IMG_QuitHEIC()
{
    if ( lib.loaded == 0 ) {
        return;
    }
    if ( lib.loaded == 1 ) {
        SDL_UnloadObject(lib.handle);
    }
    --lib.loaded;
}
#else
int IMG_InitHEIC()
{
    if ( lib.loaded == 0 ) {
    }
    ++lib.loaded;

    return 0;
}
void IMG_QuitHEIC()
{
    if ( lib.loaded == 0 ) {
        return;
    }
    if ( lib.loaded == 1 ) {
    }
    --lib.loaded;
}
#endif /* LOAD_HEIC_DYNAMIC */

/* See if an image is contained in a data source */
int IMG_isHEIC(SDL_RWops *src)
{
    int start;
    int is_HEIC;
    Uint8 magic[16];

    if ( !src )
        return 0;
    start = SDL_RWtell(src);
    is_HEIC = 0;
    if ( SDL_RWread(src, magic, 1, sizeof(magic)) == sizeof(magic) ) {
        enum heif_filetype_result ret = heif_check_filetype(magic, sizeof(magic));
        if (heif_filetype_yes_supported == ret || heif_filetype_maybe == ret) {
            is_HEIC = 1;
        }
    }
    SDL_RWseek(src, start, RW_SEEK_SET);
    return is_HEIC;
}

static int64_t heic_get_position(void* userdata)
{
    SDL_RWops *src = (SDL_RWops *)(userdata);
    return SDL_RWtell(src);
}

static int heic_read(void* data, size_t size, void* userdata)
{
    SDL_RWops *src = (SDL_RWops *)(userdata);
    if ( SDL_RWread(src, data, size, 1) > 0) {
        return 0;
    } else {
        return -1;
    }
}

static int heic_seek(int64_t position, void* userdata)
{
    SDL_RWops *src = (SDL_RWops *)(userdata);
    int64_t pos = SDL_RWseek(src, position, RW_SEEK_SET);
    if (pos > 0) {
        return 0;
    } else {
        return -1;
    }
}

static enum heif_reader_grow_status heic_wait_for_file_size(int64_t target_size, void* userdata)
{
    int start;
    int len;
    SDL_RWops *src = (SDL_RWops *)(userdata);
    start = SDL_RWtell(src);
    SDL_RWseek(src, 0, RW_SEEK_END);
    len = SDL_RWtell(src);
    SDL_RWseek(src, start, RW_SEEK_SET);
    if ( target_size > len ) {
        return heif_reader_grow_status_size_beyond_eof;
    } else {
        return heif_reader_grow_status_size_reached;
    }
}

static struct heif_reader heif_reader_sdl =
{
  1,
  heic_get_position,
  heic_read,
  heic_seek,
  heic_wait_for_file_size
};

/* Load a HEIC type image from an SDL datasource */
SDL_Surface *IMG_LoadHEIC_RW(SDL_RWops *src)
{
    int start;
    const char *error = NULL;
    SDL_Surface *volatile surface = NULL;
    uint32_t width, height;

    struct heif_context* ctx;
    struct heif_image_handle* handle;
    struct heif_error heif_err;
    struct heif_image* img = NULL;
    int i, stride;

    if ( !src ) {
        /* The error message has been set in SDL_RWFromFile */
        return NULL;
    }
    start = SDL_RWtell(src);

    if ( !IMG_Init(IMG_INIT_HEIC) ) {
        return NULL;
    }

    ctx = heif_context_alloc();
    if ( ctx == NULL ) {
        error = "Out of memory";
        goto done;
    }

    //heif_err = heif_context_read_from_file(ctx, "/Users/jeromy/work/playground/test-assets/assets/autumn_1440x960.heic", NULL);
    heif_err = heif_context_read_from_reader(ctx, &heif_reader_sdl, src, NULL);
    if (heif_error_Ok != heif_err.code) {
        error = heif_err.message;
        goto done;
    }

    /* get a handle to the primary image */
    heif_err = heif_context_get_primary_image_handle(ctx, &handle);
    if (heif_error_Ok != heif_err.code) {
        error = heif_err.message;
        goto done;
    }

    /* decode the image and convert colorspace to RGB, saved as 24bit interleaved */
    heif_err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, NULL);
    if (heif_error_Ok != heif_err.code) {
        error = heif_err.message;
        goto done;
    }

    /* Allocate the SDL surface to hold the image */
    width = heif_image_get_width(img, heif_channel_interleaved);
    height = heif_image_get_height(img, heif_channel_interleaved);
    if (width == -1 || height == -1) {
        error = "Invalid image dimension";
        goto done;
    }

    surface = SDL_AllocSurface(SDL_SWSURFACE, width, height, 24,
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                          0x0000FF, 0x00FF00, 0xFF0000,
#else
                          0xFF0000, 0x00FF00, 0x0000FF,
#endif
            0);
    if ( surface == NULL ) {
        error = "Out of memory";
        goto done;
    }

    /* Create the array of pointers to image data */
    const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
    if ( data == NULL ) {
        error = "Error reading heif data";
        goto done;
    }

    uint8_t* pixels = (uint8_t*)surface->pixels;
    for (i = 0; i < height; ++i) {
        SDL_memcpy(pixels, data, stride);
        pixels += surface->pitch;
        data += stride;
    }

done:    /* Clean up and return */
    if (img) {
        heif_image_release(img);
    }

    if ( error ) {
        SDL_RWseek(src, start, RW_SEEK_SET);
        if ( surface ) {
            SDL_FreeSurface(surface);
            surface = NULL;
        }
        IMG_SetError(error);
    }

    return(surface);
}

#else

int IMG_InitHEIC()
{
    IMG_SetError("HEIC images are not supported");
    return(-1);
}

void IMG_QuitHEIC()
{
}

/* See if an image is contained in a data source */
int IMG_isHEIC(SDL_RWops *src)
{
    return(0);
}

/* Load a JPEG type image from an SDL datasource */
SDL_Surface *IMG_LoadJPG_RW(SDL_RWops *src)
{
    return(NULL);
}

#endif /* LOAD_HEIC */

#endif /* !defined(__APPLE__) || defined(SDL_IMAGE_USE_COMMON_BACKEND) */
