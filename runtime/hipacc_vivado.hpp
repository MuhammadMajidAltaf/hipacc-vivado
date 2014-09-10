//
// Copyright (c) 2014, Saarland University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#ifndef __HIPACC_CPU_HPP__
#define __HIPACC_CPU_HPP__

#include <string.h>
#include <iostream>

#include <hls_stream.h>
#include <ap_int.h>

#include "hipacc_base.hpp"

class HipaccContext : public HipaccContextBase {
    public:
        static HipaccContext &getInstance() {
            static HipaccContext instance;

            return instance;
        }
};

long start_time = 0L;
long end_time = 0L;

void hipaccStartTiming() {
    start_time = getMicroTime();
}

void hipaccStopTiming() {
    end_time = getMicroTime();
    last_gpu_timing = (end_time - start_time) * 1.0e-3f;

    std::cerr << "<HIPACC:> Kernel timing: "
              << last_gpu_timing << "(ms)" << std::endl;
}


// Create image to store size information
template<typename T>
HipaccImage hipaccCreateMemory(T *host_mem, int width, int height) {
    HipaccContext &Ctx = HipaccContext::getInstance();

    HipaccImage img = HipaccImage(width, height, width, 0, sizeof(T), (void *)NULL);
    Ctx.add_image(img);

    return img;
}


// Release image
void hipaccReleaseMemory(HipaccImage &img) {
    HipaccContext &Ctx = HipaccContext::getInstance();
    Ctx.del_image(img);
}


// Write to stream
// T1 might be ap_uint<32>
// T2 might be uint (representing uchar4)
template<typename T1, typename T2>
void hipaccWriteMemory(HipaccImage &img, hls::stream<T1> &s, T2 *host_mem) {
    int width = img.width;
    int height = img.height;

    for (size_t i=0; i<width*height; ++i) {
        T1 data = host_mem[i];
        s << data;
    }
}


// Read from stream
// T1 might be ap_uint<32>
// T2 might be uint (representing uchar4)
template<typename T1, typename T2>
void hipaccReadMemory(hls::stream<T1> &s, T2 *host_mem, HipaccImage &img) {
    int width = img.width;
    int height = img.height;

    for (size_t i=0; i<width*height; ++i) {
        T1 data;
        s >> data;
        host_mem[i] = data;
    }
}


// Copy from stream to stream
void hipaccCopyMemory(HipaccImage &src, HipaccImage &dst) {
    assert(false && "Copy stream not implemented yet");
}


// Infer non-const Domain from non-const Mask
template<typename T>
void hipaccWriteDomainFromMask(HipaccImage &dom, T* host_mem) {
    assert(false && "Only const masks and domains are supported yet");
}


#endif  // __HIPACC_CPU_HPP__
