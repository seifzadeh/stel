/* SpanDSP's filename API uses UTF-8. The Windows TIFF endpoint uses UTF-16. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <tiffio.h>
TIFF *stel_fax_tiff_open_utf8(const char *path, const char *mode) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide;
    TIFF *result;
    if (length <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length)) {
        free(wide); return NULL;
    }
    result = TIFFOpenW(wide, mode);
    free(wide);
    return result;
}
