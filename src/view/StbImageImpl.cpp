//
// The one translation unit that holds stb_image's implementation (M1-16,
// register decision 195), as src/render/VmaImpl.cpp holds VMA's.
//
// **Two users, one copy** (M1-17, register decision 235). orbsim_view reads
// golden images with it, and the tests read the probe's PNGs back with it --
// independently of lodepng, which wrote them. Its implementation compiled into
// both would define every function twice, so it is a library of its own that
// both link. It moved here from tests/ with M1-17 for that reason.
//
// It is a system header here, so the implementation it expands is not judged
// by this project's warnings -- measured before the pin: none leak through.
//
// Only PNG is compiled, and nothing that reads a file by name: both users hand
// it bytes they have already read, through std::filesystem::path.
//
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
