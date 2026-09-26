//
// The one translation unit that holds stb_image's implementation (M1-16,
// register decision 195), as src/render/VmaImpl.cpp holds VMA's.
//
// stb_image is the tests' PNG decoder: it reads the probe's PNGs back to
// check them pixel by pixel, independently of lodepng, which wrote them. It
// is a system header here, so the implementation it expands is not judged by
// this project's warnings -- measured before the pin: none leak through.
//
// Only PNG is compiled, and nothing that reads a file by name: the tests hand
// it bytes they have already read, through std::filesystem::path.
//
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
