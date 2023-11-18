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

#include <exception>
#include <algorithm>
#include <array>
#include <span>
#include <sys/random.h>

typedef unsigned char uchar;

extern "C" {
    #include <jpeglib.h>
}

// just make jpeg errors throw an exception
void jpeg_error_throw(j_common_ptr) { throw std::exception(); }

// decompressor lifetime helper
struct jpeg_decompress {
    jpeg_error_mgr err = {};
    jpeg_decompress_struct st = {};
    jpeg_decompress() {
        st.err = jpeg_std_error(&err);
        err.error_exit = jpeg_error_throw;
        jpeg_create_decompress(&st);
    }
    ~jpeg_decompress() {
        jpeg_finish_decompress(&st);
        jpeg_destroy_decompress(&st);
    }
};

// compressor lifetime helper
struct jpeg_compress {
    jpeg_error_mgr err = {};
    jpeg_compress_struct st = {};
    jpeg_compress() {
        st.err = jpeg_std_error(&err);
        err.error_exit = jpeg_error_throw;
        jpeg_create_compress(&st);
    }
    ~jpeg_compress() {
        jpeg_finish_compress(&st);
        jpeg_destroy_compress(&st);
    }
};

inline ushort correlation(const ushort coef) {
    static_assert(-1 == ~0);
    // except [-2, -1, 0, 1, 2] which are too sensitive and 0x7fff which has no neighbour:
    if (coef + 2u <= 4u || coef == 0x7fffu) return 0;
    else if (coef > 0x7fffu) return coef ^ 1u;
    else return ((coef - 1u) ^ 1u) + 1u;
}

// the JPEG conceal implementation
class jpeg_conceal {
    std::array<uint, (1<<16)> total; // number of symbols in the whole image, set by INIT
    std::array<uint, (1<<16)> occur; // number of any symbols seen so far, set by WRITE or READ
    std::array<uint, (1<<16)> count; // number of info symbols seen so far, set by WRITE or READ
    double entropy; // approximate original quota of info bit 0

    jpeg_decompress src_info = jpeg_decompress(); // decompressor
    jvirt_barray_ptr *components; // image components, which hold the coefficients
    std::span<uchar> img; // image binary data

    std::span<uchar> msg; // message binary data
    ulong msg_byte; // current byte index into the message data
    uchar msg_bit; // current bit index into the current byte


    // execute the given pass logic on every coefficient in the image
    enum pass_t { INIT, WRITE, READ };
    void pass(pass_t type) {
        // initialize
        switch (type) {
            case INIT:
                total.fill(0);
            break;
            case WRITE:
            case READ:
                occur.fill(0);
                count.fill(0);
                msg_byte = 0;
                msg_bit = 0;
        }
        // iterate all coefficients
        for (auto c = 0u; c < src_info.st.num_components; c++) {
            for (auto h = 0u; h < src_info.st.comp_info[c].height_in_blocks; h++) {
                const auto rows = src_info.st.comp_info[c].v_samp_factor;
                const auto array = src_info.st.mem->access_virt_barray(
                        reinterpret_cast<j_common_ptr>(&src_info.st), components[c], h, rows, type == WRITE);
                for (auto y = 0u; y < rows; y++) {
                    for (auto x = 0u; x < src_info.st.comp_info[c].width_in_blocks; x++) {
                        for (auto d = 0u; d < DCTSIZE2; d++) {
                            const auto coef_ptr = reinterpret_cast<ushort *>(&array[y][x][d]);

                            switch (type) {
                                case INIT: total[*coef_ptr]++; break;
                                case WRITE: bit_write(coef_ptr); break;
                                case READ: bit_read(*coef_ptr); break;
                            }
        }   }   }   }   }
    }

    #define INVALID 4u
    #define RESTORE 3u
    #define PADDING 2u
    // lookup the interpretation of an upcoming coefficient (or simply return the bit info value)
    uchar bit_test(const ushort coef, const ushort corr) {
        if (coef == 0 || corr == 0 || total[coef] == 0 || total[corr] == 0) return INVALID;
        if (occur[coef] >= total[coef]) return RESTORE; // corr shortage
        if (occur[corr] >= total[corr]) return PADDING; // coef shortage

        const auto seen = occur[coef] != 0 ? (count[coef] << 16u) / occur[coef] : 0;
        const auto quota = (total[coef] << 16u) / total[corr];
        return seen < quota ? coef & 1u : PADDING;
    }

    // write a bit from message to image (to change the current original coefficient given as argument)
    void bit_write(ushort *coef_ptr) {
        const auto coef = *coef_ptr;
        const auto corr = correlation(coef);
        const auto bwas = bit_test(coef, corr);

        if (bwas == INVALID) {
            // ignore
        } else if (bwas == RESTORE) {
            // need to restore quota
            *coef_ptr = corr;
            occur[corr]++;
        } else if (bwas == PADDING) {
            // track padding amount
            occur[coef]++;
        } else if (msg_byte < msg.size()) {
            const auto bset = (msg[msg_byte] >> msg_bit) & 1u;
            if (bwas == bset) {
                // is reading the correct bit
                count[coef]++;
                msg_byte += (++msg_bit >> 3u) & 1u;
                msg_bit &= 7u;
                occur[coef]++;
            } else {
                // would have read the wrong bit
                if (bit_test(corr, coef) == bset) {
                    count[corr]++;
                    msg_byte += (++msg_bit >> 3u) & 1u;
                    msg_bit &= 7u;
                }
                *coef_ptr = corr;
                occur[corr]++;
            }
        } else {
            // add distortion to avoid a detectable noise edge
            ushort rnd;
            getentropy(&rnd, sizeof rnd);
            const auto bset = static_cast<double>(rnd) / static_cast<double>(0xffffu) >= entropy ? 1u : 0u;
            if (bwas == bset) {
                count[coef]++;
                occur[coef]++;
            } else {
                if (bit_test(corr, coef) == bset) {
                    count[corr]++;
                }
                *coef_ptr = corr;
                occur[corr]++;
            }
        }
    }

    // read a bit from image (current coefficient given as argument) to message
    void bit_read(const ushort coef) {
        if (msg.data() && msg_byte >= msg.size()) return;

        const auto bit = bit_test(coef, correlation(coef));
        if (bit < PADDING) {
            count[coef]++;
            if (msg.data()) msg[msg_byte] |= bit << msg_bit;
            msg_byte += (++msg_bit >> 3u) & 1u;
            msg_bit &= 7u;
        }
        occur[coef]++;
    }


public:
    // construct a JPEG steganography class for given image (input data)
    jpeg_conceal(std::span<uchar> image) : img(image) {
        jpeg_mem_src(&src_info.st, img.data(), img.size());
        if (jpeg_read_header(&src_info.st, TRUE) != JPEG_HEADER_OK) throw std::exception();
        components = jpeg_read_coefficients(&src_info.st);
        pass(INIT);

        // calculate original info bit entropy
        ulong bit[2] = {0};
        const auto orig = read();
        for (auto c : orig)
            for (auto b = 0u; b < 8u; b++)
                bit[(c >> b) & 1u]++;
        free(orig.data());
        entropy = static_cast<double>(bit[0]) / static_cast<double>(bit[0] + bit[1]);
    }

    // return the size of the currently stored "message" (can be random image data)
    ulong current_size() {
        msg = {};
        pass(READ);
        return msg_byte;
    }

    // read a message from JPEG and return it
    std::span<uchar> read() {
        const auto size = current_size();
        msg = {(uchar *)calloc(1, size), size};
        pass(READ);
        if (msg_byte != msg.size()) throw std::exception();
        return msg;
    }

    // write from messge to JPEG and return the modified image buffer (may have changed pointer)
    std::span<uchar> write(std::span<uchar> message) {
        msg = message;
        pass(WRITE);
        if (msg_byte != msg.size()) throw std::exception();

        auto dst_info = jpeg_compress();
        auto img_buf = img.data();
        auto img_len = img.size();
        jpeg_mem_dest(&dst_info.st, &img_buf, &img_len); // may change img_buf and img_len
        jpeg_copy_critical_parameters(&src_info.st, &dst_info.st);
        dst_info.st.in_color_space = src_info.st.out_color_space;
        jpeg_write_coefficients(&dst_info.st, components);
        return {img_buf, img_len};
    }
};
