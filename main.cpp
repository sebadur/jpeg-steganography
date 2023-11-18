/*
MIT License

Copyright (c) 2023 Dr. Sebastian Badur

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <cstdio>
#include <cstdlib>
#include "jpeg.cpp"

int main(int argc, char **argv) {
    if (argc < 3) return -1;

    for (auto coef = 0u; coef < (1u << 16u); coef++) {
        const auto corr = correlation(coef);
        if (corr) {
            if (corr != 0 && coef != correlation(corr)) {
                printf("illegal correlation %i : %i\n", (short)coef, (short)corr);
                exit(10);
            }
        } else {
            if ((short)coef != -2 && (short)coef != -1 && coef != 0 && coef != 1 && coef != 2 && coef != 0x7fff) {
                printf("illegal 'invalid' %i\n", (short)coef);
                exit(11);
            }
        }
    }

    const auto in_fp = fopen(argv[1], "rb");
    fseek(in_fp, 0, SEEK_END);
    const ulong in_len = ftell(in_fp);
    if (in_len == (ulong)-1) return -2;
    rewind(in_fp);

    auto in_buf = (unsigned char *)malloc(in_len * sizeof(char));
    fread(in_buf, in_len, 1, in_fp);
    fclose(in_fp);

    auto jpeg = jpeg_conceal({in_buf, in_len});
    printf("size original = %li\n", jpeg.current_size());
    const auto before = jpeg.read();

    uchar msg_buf[2000] = "Hello World!";
    auto modified = jpeg.write({msg_buf, sizeof msg_buf});
    printf("size changed = %li\n", jpeg.current_size());

    auto after = jpeg.read();
    printf("read = %s\n", after.data());

    ulong bc[2] = {0, 0};
    for (auto b : before) bc[b&1]++;
    ulong ac[2] = {0, 0};
    for (auto a : after) ac[a&1]++;
    printf("entropy = { before: %li:%li, after: %li:%li }\n", bc[0], bc[1], ac[0], ac[1]);

    const auto out_fp = fopen(argv[2], "wb");
    fwrite(modified.data(), modified.size(), 1, out_fp);
    fclose(out_fp);

    free(before.data());
    free(after.data());
    if (modified.data() != in_buf) free(modified.data());
    free(in_buf);

    return 0;
}
