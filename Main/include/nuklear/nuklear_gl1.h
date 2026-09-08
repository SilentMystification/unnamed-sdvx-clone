/*
 * Fixed-function (GL1_LEGACY) nuklear backend for macOS 10.4/PowerPC, where nuklear_sdl_gl3.h
 * (GL3.3 core: glCreateShader/glGenVertexArrays/glVertexAttribPointer) and nuklear_sdl_gles2.h
 * both fail to compile - no GLSL, no VAOs, no generic vertex attributes exist on this backend
 * any more than they do in the rest of the engine (see Material_GL1_LEGACY.cpp/Mesh_GL1_LEGACY.cpp
 * for the same fixed-function-port pattern applied to the main Graphics module).
 *
 * Same public API as nuklear_sdl_gl3.h/nuklear_sdl_gles2.h (nk_sdl_init/nk_sdl_font_stash_begin/
 * nk_sdl_font_stash_end/nk_sdl_handle_event/nk_sdl_render/nk_sdl_shutdown), selected the same
 * way in Main/stdafx.cpp - see humming-sleeping-stonebraker.md - so GuiUtils.cpp/SettingsScreen.cpp/
 * CalibrationScreen.cpp/ChatOverlay.cpp/SettingsPage.cpp (the actual nk_context consumers) need
 * no changes.
 *
 * nk_sdl_handle_event is copied verbatim from nuklear_sdl_gl3.h - it only reads SDL_Event
 * fields and calls nk_input_*, nothing GL-specific.
 *
 * Rendering: glVertexPointer/glTexCoordPointer/glColorPointer as byte offsets into a real,
 * explicitly-managed VBO/EBO (nk_gl1_device::vertexBuffer/elementBuffer), not raw pointers
 * into CPU memory - see that field's comment for why (a real hardware investigation this
 * session found this exact driver reads stale/reallocated client memory instead of the
 * vertices meant for a draw). nk_convert() still needs a plain CPU buffer to write into
 * (vertex_mem/element_mem), which then gets uploaded (orphaned via glBufferData) into the
 * real buffers every render pass, matching nanovg_gl1.h's GL1NVGcontext::vertexBuffer.
 * Projection uses glOrtho instead of a shader uniform; texture+vertex-color modulation
 * uses GL_MODULATE texture env instead of `Frag_Color * texture(...)` - both produce the exact
 * same result, same as the reasoning already verified for Material_GL1_LEGACY.cpp.
 *
 * UNVERIFIED: no macOS/PPC hardware in the environment this was written in to run-test against.
 */
#ifndef NK_GL1_H_
#define NK_GL1_H_

#include "SDL2/SDL.h"

NK_API struct nk_context*   nk_sdl_init(SDL_Window *win);
NK_API void                 nk_sdl_font_stash_begin(struct nk_font_atlas **atlas);
NK_API void                 nk_sdl_font_stash_end(void);
NK_API int                  nk_sdl_handle_event(SDL_Event *evt);
NK_API void                 nk_sdl_render(enum nk_anti_aliasing, int max_vertex_buffer, int max_element_buffer);
NK_API void                 nk_sdl_shutdown(void);

#endif

#ifdef NK_GL1_IMPLEMENTATION

#include <string.h>
#include <stdlib.h>

struct nk_gl1_device {
    struct nk_buffer cmds;
    struct nk_draw_null_texture null;
    void *vertex_mem, *element_mem;
    int vertex_mem_size, element_mem_size;
    GLuint font_tex;
    // Real VBO/EBO the CPU-side vertex_mem/element_mem above get uploaded (orphaned) into
    // every render pass - see nanovg_gl1.h's GL1NVGcontext::vertexBuffer for why raw
    // glVertexPointer/glDrawElements pointers into malloc'd memory are unsafe on this
    // driver: a real hardware investigation (Process Viewer sample showing
    // gleDrawArraysOrElements_VBO_Exec/gldFreeVertexBuffer on the draw path) confirmed this
    // exact GPU/driver reads stale/reallocated client memory instead of the vertices
    // actually meant for a draw, producing near-black frames with stray single-pixel
    // fragments - vertex_mem/element_mem are freed and realloc'd via plain malloc whenever
    // they grow (see nk_sdl_render below), making them exactly the kind of unstable-address
    // CPU memory that triggers it.
    GLuint vertexBuffer, elementBuffer;
};

struct nk_gl1_vertex {
    float position[2];
    float uv[2];
    nk_byte col[4];
};

static struct nk_gl1 {
    SDL_Window *win;
    struct nk_gl1_device ogl;
    struct nk_context ctx;
    struct nk_font_atlas atlas;
} sdl;

NK_API void
nk_sdl_device_create(void)
{
    struct nk_gl1_device *dev = &sdl.ogl;
    nk_buffer_init_default(&dev->cmds);
    glGenBuffers(1, &dev->vertexBuffer);
    glGenBuffers(1, &dev->elementBuffer);
}

NK_INTERN void
nk_sdl_device_upload_atlas(const void *image, int width, int height)
{
    struct nk_gl1_device *dev = &sdl.ogl;
    glGenTextures(1, &dev->font_tex);
    glBindTexture(GL_TEXTURE_2D, dev->font_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, image);
    glBindTexture(GL_TEXTURE_2D, 0);
}

NK_API void
nk_sdl_device_destroy(void)
{
    struct nk_gl1_device *dev = &sdl.ogl;
    glDeleteTextures(1, &dev->font_tex);
    glDeleteBuffers(1, &dev->vertexBuffer);
    glDeleteBuffers(1, &dev->elementBuffer);
    free(dev->vertex_mem);
    free(dev->element_mem);
    dev->vertex_mem = dev->element_mem = NULL;
    nk_buffer_free(&dev->cmds);
}

NK_API void
nk_sdl_render(enum nk_anti_aliasing AA, int max_vertex_buffer, int max_element_buffer)
{
    struct nk_gl1_device *dev = &sdl.ogl;
    int width, height;

    SDL_GetWindowSize(sdl.win, &width, &height);
    if(width <= 0 || height <= 0)
        return;

    /* (re)allocate CPU-side scratch buffers nk_convert() fills - see file header comment
       on why this stays off the GPU rather than using a VBO. */
    if(dev->vertex_mem_size < max_vertex_buffer)
    {
        free(dev->vertex_mem);
        dev->vertex_mem = malloc((size_t)max_vertex_buffer);
        dev->vertex_mem_size = max_vertex_buffer;
    }
    if(dev->element_mem_size < max_element_buffer)
    {
        free(dev->element_mem);
        dev->element_mem = malloc((size_t)max_element_buffer);
        dev->element_mem_size = max_element_buffer;
    }

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)width, (double)height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D);
    /* GL_TEXTURE_ENV_MODE is set once at context Init() (OpenGL_GL1_LEGACY.cpp) - always
       GL_MODULATE, never changes, no need to redundantly set it here every render pass. */

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    {
        const struct nk_draw_command *cmd;
        const nk_draw_index *offset;
        struct nk_buffer vbuf, ebuf;
        struct nk_convert_config config;
        static const struct nk_draw_vertex_layout_element vertex_layout[] = {
            {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_gl1_vertex, position)},
            {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_gl1_vertex, uv)},
            {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct nk_gl1_vertex, col)},
            {NK_VERTEX_LAYOUT_END}
        };
        NK_MEMSET(&config, 0, sizeof(config));
        config.vertex_layout = vertex_layout;
        config.vertex_size = sizeof(struct nk_gl1_vertex);
        config.vertex_alignment = NK_ALIGNOF(struct nk_gl1_vertex);
        config.null = dev->null;
        config.circle_segment_count = 22;
        config.curve_segment_count = 22;
        config.arc_segment_count = 22;
        config.global_alpha = 1.0f;
        config.shape_AA = AA;
        config.line_AA = AA;

        nk_buffer_init_fixed(&vbuf, dev->vertex_mem, (nk_size)max_vertex_buffer);
        nk_buffer_init_fixed(&ebuf, dev->element_mem, (nk_size)max_element_buffer);
        nk_convert(&sdl.ctx, &dev->cmds, &vbuf, &ebuf, &config);

        {
            // Real VBO/EBO upload (orphaned via glBufferData every pass), matching
            // nanovg_gl1.h's proven fix for this exact driver - see nk_gl1_device's
            // vertexBuffer/elementBuffer field comment. vertex_mem/element_mem are still
            // used as nk_convert()'s CPU-side scratch target (that part of the API needs a
            // plain memory buffer to write into); the difference is they no longer get
            // handed to GL directly as draw-time pointers.
            GLsizei vs = (GLsizei)sizeof(struct nk_gl1_vertex);

            glBindBuffer(GL_ARRAY_BUFFER, dev->vertexBuffer);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)max_vertex_buffer, dev->vertex_mem, GL_STREAM_DRAW);
            glVertexPointer(2, GL_FLOAT, vs, (const void*)offsetof(struct nk_gl1_vertex, position));
            glTexCoordPointer(2, GL_FLOAT, vs, (const void*)offsetof(struct nk_gl1_vertex, uv));
            glColorPointer(4, GL_UNSIGNED_BYTE, vs, (const void*)offsetof(struct nk_gl1_vertex, col));

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dev->elementBuffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)max_element_buffer, dev->element_mem, GL_STREAM_DRAW);
            // Now a byte offset into the bound GL_ELEMENT_ARRAY_BUFFER, same as
            // nuklear_sdl_gl3.h's VBO-based backend - starting at 0/NULL is correct here,
            // unlike when this was a real pointer into dev->element_mem with no buffer
            // bound (that NULL was the crash bug fixed earlier this session).
            offset = (const nk_draw_index*)0;
        }

        nk_draw_foreach(cmd, &sdl.ctx, &dev->cmds) {
            if (!cmd->elem_count) continue;
            glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
            glScissor((GLint)cmd->clip_rect.x,
                (GLint)(height - (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)),
                (GLint)cmd->clip_rect.w,
                (GLint)cmd->clip_rect.h);
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count, GL_UNSIGNED_SHORT, offset);
            offset += cmd->elem_count;
        }
        nk_clear(&sdl.ctx);
    }

    /* GL_VERTEX_ARRAY is deliberately NOT disabled here - OpenGL_GL1_LEGACY.cpp's Init()
       enables it once and every other GL1 renderer (Mesh_GL1_LEGACY.cpp, nanovg_gl1.h)
       assumes it stays on for the life of the context; disabling it here previously meant
       everything drawn after the first nuklear render pass silently submitted zero
       vertices, forever. GL_TEXTURE_COORD_ARRAY/GL_COLOR_ARRAY are safe to disable since
       nothing else relies on them staying on (Mesh_GL1_LEGACY.cpp explicitly sets
       GL_TEXTURE_COORD_ARRAY itself per mesh, and nothing else uses GL_COLOR_ARRAY at all -
       also avoids leaving it bound to vertex_mem, which nk_sdl_render's own next call may
       realloc/free). */
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

static void
nk_sdl_clipboard_paste(nk_handle usr, struct nk_text_edit *edit)
{
    const char *text = SDL_GetClipboardText();
    if (text) nk_textedit_paste(edit, text, nk_strlen(text));
    (void)usr;
}

static void
nk_sdl_clipboard_copy(nk_handle usr, const char *text, int len)
{
    char *str = 0;
    (void)usr;
    if (!len) return;
    str = (char*)malloc((size_t)len+1);
    if (!str) return;
    memcpy(str, text, (size_t)len);
    str[len] = '\0';
    SDL_SetClipboardText(str);
    free(str);
}

NK_API struct nk_context*
nk_sdl_init(SDL_Window *win)
{
    sdl.win = win;
    nk_init_default(&sdl.ctx, 0);
    sdl.ctx.clip.copy = nk_sdl_clipboard_copy;
    sdl.ctx.clip.paste = nk_sdl_clipboard_paste;
    sdl.ctx.clip.userdata = nk_handle_ptr(0);
    nk_sdl_device_create();
    return &sdl.ctx;
}

NK_API void
nk_sdl_font_stash_begin(struct nk_font_atlas **atlas)
{
    nk_font_atlas_init_default(&sdl.atlas);
    nk_font_atlas_begin(&sdl.atlas);
    *atlas = &sdl.atlas;
}

NK_API void
nk_sdl_font_stash_end(void)
{
    const void *image; int w, h;
    image = nk_font_atlas_bake(&sdl.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    nk_sdl_device_upload_atlas(image, w, h);
    nk_font_atlas_end(&sdl.atlas, nk_handle_id((int)sdl.ogl.font_tex), &sdl.ogl.null);
    if (sdl.atlas.default_font)
        nk_style_set_font(&sdl.ctx, &sdl.atlas.default_font->handle);
}

/* Copied verbatim from nuklear_sdl_gl3.h - pure SDL_Event -> nk_input_* translation, no
   GL calls at all. */
NK_API int
nk_sdl_handle_event(SDL_Event *evt)
{
    struct nk_context *ctx = &sdl.ctx;
    if (evt->type == SDL_KEYUP || evt->type == SDL_KEYDOWN) {
        int down = evt->type == SDL_KEYDOWN;
        const Uint8* state = SDL_GetKeyboardState(0);
        SDL_Keycode sym = evt->key.keysym.sym;
        if (sym == SDLK_RSHIFT || sym == SDLK_LSHIFT)
            nk_input_key(ctx, NK_KEY_SHIFT, down);
        else if (sym == SDLK_DELETE)
            nk_input_key(ctx, NK_KEY_DEL, down);
        else if (sym == SDLK_RETURN)
            nk_input_key(ctx, NK_KEY_ENTER, down);
        else if (sym == SDLK_TAB)
            nk_input_key(ctx, NK_KEY_TAB, down);
        else if (sym == SDLK_BACKSPACE)
            nk_input_key(ctx, NK_KEY_BACKSPACE, down);
        else if (sym == SDLK_HOME) {
            nk_input_key(ctx, NK_KEY_TEXT_START, down);
            nk_input_key(ctx, NK_KEY_SCROLL_START, down);
        } else if (sym == SDLK_END) {
            nk_input_key(ctx, NK_KEY_TEXT_END, down);
            nk_input_key(ctx, NK_KEY_SCROLL_END, down);
        } else if (sym == SDLK_PAGEDOWN) {
            nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
        } else if (sym == SDLK_PAGEUP) {
            nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
        } else if (sym == SDLK_z)
            nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_r)
            nk_input_key(ctx, NK_KEY_TEXT_REDO, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_c)
            nk_input_key(ctx, NK_KEY_COPY, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_v)
            nk_input_key(ctx, NK_KEY_PASTE, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_x)
            nk_input_key(ctx, NK_KEY_CUT, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_b)
            nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_e)
            nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_a)
            nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down && state[SDL_SCANCODE_LCTRL]);
        else if (sym == SDLK_UP)
            nk_input_key(ctx, NK_KEY_UP, down);
        else if (sym == SDLK_DOWN)
            nk_input_key(ctx, NK_KEY_DOWN, down);
        else if (sym == SDLK_LEFT) {
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
            else nk_input_key(ctx, NK_KEY_LEFT, down);
        } else if (sym == SDLK_RIGHT) {
            if (state[SDL_SCANCODE_LCTRL])
                nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
            else nk_input_key(ctx, NK_KEY_RIGHT, down);
        } else return 0;
        return 1;
    } else if (evt->type == SDL_MOUSEBUTTONDOWN || evt->type == SDL_MOUSEBUTTONUP) {
        int down = evt->type == SDL_MOUSEBUTTONDOWN;
        const int x = evt->button.x, y = evt->button.y;
        if (evt->button.button == SDL_BUTTON_LEFT) {
            if (evt->button.clicks > 1)
                nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
            nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
        } else if (evt->button.button == SDL_BUTTON_MIDDLE)
            nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
        else if (evt->button.button == SDL_BUTTON_RIGHT)
            nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
        return 1;
    } else if (evt->type == SDL_MOUSEMOTION) {
        if (ctx->input.mouse.grabbed) {
            int x = (int)ctx->input.mouse.prev.x, y = (int)ctx->input.mouse.prev.y;
            nk_input_motion(ctx, x + evt->motion.xrel, y + evt->motion.yrel);
        } else nk_input_motion(ctx, evt->motion.x, evt->motion.y);
        return 1;
    } else if (evt->type == SDL_TEXTINPUT) {
        nk_glyph glyph;
        memcpy(glyph, evt->text.text, NK_UTF_SIZE);
        nk_input_glyph(ctx, glyph);
        return 1;
    } else if (evt->type == SDL_MOUSEWHEEL) {
        nk_input_scroll(ctx,nk_vec2((float)evt->wheel.x,(float)evt->wheel.y));
        return 1;
    }
    return 0;
}

NK_API
void nk_sdl_shutdown(void)
{
    nk_font_atlas_clear(&sdl.atlas);
    nk_free(&sdl.ctx);
    nk_sdl_device_destroy();
    memset(&sdl, 0, sizeof(sdl));
}

#endif
