// Built-in PNG / JPEG codecs (public-domain stb; WebP comes from the bundled libwebp), used only when the Translation Kit has no translator for
// the format: the arm64 images ship nothing but the text translator.
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"
