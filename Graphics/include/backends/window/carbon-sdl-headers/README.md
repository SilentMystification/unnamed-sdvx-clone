# Vendored SDL2 headers (types only, no library)

`Window.hpp`'s public interface uses `SDL_Scancode` and `SDL_Event` (e.g.
`IsKeyPressed(SDL_Scancode)`, `Delegate<SDL_Scancode, int32> OnKeyPressed`), and ~30
files under `Main/` consume those types directly. Rewriting all of that to a
custom key-code type is out of scope for the Carbon/AGL window backend, so instead
this directory vendors SDL2's real public headers (copied verbatim from an
`libsdl2-dev` install, zlib licensed - see the header text) to provide those types
with byte-exact ABI values, without requiring SDL2 itself to exist on macOS
10.4/PowerPC (it doesn't - SDL2 needs 10.6+/Cocoa).

Only used for `USC_WINDOW_BACKEND=CARBON` - see `Graphics/CMakeLists.txt`. The
Carbon window backend (`WindowImpl_Carbon.cpp`) never calls a real `SDL_*`
function; it only constructs `SDL_Event`/`SDL_Scancode` values by hand from native
Carbon Event Manager input, so no SDL2 library is linked on this backend.

The `SDL2` (desktop) window backend does not use this directory - it links the
real, installed SDL2 headers and library as before.

Note on `SDL_config.h`: the file here is SDL2's own official
`SDL_config_minimal.h` (from libsdl-org/SDL, `include/SDL_config_minimal.h`) -
the template SDL2 itself provides for porting to a new platform - not the one
originally copied verbatim from a Debian package's `/usr/include/SDL2/`. That
copy turned out to be a Debian-specific indirection to an
architecture-generated `_real_SDL_config.h` in a multiarch path, which doesn't
exist for a PowerPC target and can't be blindly substituted with the
x86_64/Linux-generated one either (wrong endianness/feature-detection
assumptions for our target). The minimal template avoids depending on any of
that - it's exactly meant for a backend, like this one, that only needs
straightforward struct/enum type declarations, not real subsystem detection.
