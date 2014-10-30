//
// Copyright (c) 2013, University of Erlangen-Nuremberg
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

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include <vector>

//#define CPU
#ifdef OpenCV
#include "opencv2/opencv.hpp"
#ifndef CPU
#include "opencv2/gpu/gpu.hpp"
#endif
#endif

#include "hipacc.hpp"

// variables set by Makefile
//#define SIZE_X 5
//#define SIZE_Y 5
//#define WIDTH 4096
//#define HEIGHT 4096
//#define YORDER
#define CONST_MASK
#define USE_LAMBDA
//#define RUN_UNDEF
//#define NO_SEP

using namespace hipacc;


// get time in milliseconds
double time_ms () {
    struct timeval tv;
    gettimeofday (&tv, NULL);

    return ((double)(tv.tv_sec) * 1e+3 + (double)(tv.tv_usec) * 1e-3);
}


// Sobel filter reference
void sobel_filter(uchar4 *in, short4 *out, int *filter, int size_x, int size_y,
        int width, int height) {
    int anchor_x = size_x >> 1;
    int anchor_y = size_y >> 1;
    #ifdef OpenCV
    int upper_x = width-size_x+anchor_x;
    int upper_y = height-size_y+anchor_y;
    #else
    int upper_x = width-anchor_x;
    int upper_y = height-anchor_y;
    #endif

    for (int y=anchor_y; y<upper_y; ++y) {
        for (int x=anchor_x; x<upper_x; ++x) {
            int4 sum = {0, 0, 0, 0};

            for (int yf = -anchor_y; yf<=anchor_y; yf++) {
                for (int xf = -anchor_x; xf<=anchor_x; xf++) {
                    sum += filter[(yf+anchor_y)*size_x + xf+anchor_x] *
                           convert_int4(in[(y+yf)*width + x + xf]);
                }
            }
            out[y*width + x] = convert_short4(sum);
        }
    }
}
void sobel_filter_row(uchar4 *in, short4 *out, int *filter, int size_x,
        int width, int height) {
    int anchor_x = size_x >> 1;
    #ifdef OpenCV
    int upper_x = width-size_x+anchor_x;
    #else
    int upper_x = width-anchor_x;
    #endif

    for (int y=0; y<height; ++y) {
        //for (int x=0; x<anchor_x; x++) out[y*width + x] = in[y*width + x];
        for (int x=anchor_x; x<upper_x; ++x) {
            int4 sum = {0, 0, 0, 0};

            for (int xf = -anchor_x; xf<=anchor_x; xf++) {
                sum += filter[xf+anchor_x] *
                       convert_int4(in[(y)*width + x + xf]);
            }
            out[y*width + x] = convert_short4(sum);
        }
        //for (int x=upper_x; x<width; x++) out[y*width + x] = in[y*width + x];
    }
}
void sobel_filter_column(short4 *in, short4 *out, int *filter, int size_y,
        int width, int height) {
    int anchor_y = size_y >> 1;
    #ifdef OpenCV
    int upper_y = height-size_y+anchor_y;
    #else
    int upper_y = height-anchor_y;
    #endif

    //for (int y=0; y<anchor_y; y++) {
    //    for (int x=0; x<width; ++x) {
    //        out[y*width + x] = (uchar4) in[y*width + x];
    //    }
    //}
    for (int y=anchor_y; y<upper_y; ++y) {
        for (int x=0; x<width; ++x) {
            int4 sum = {0, 0, 0, 0};

            for (int yf = -anchor_y; yf<=anchor_y; yf++) {
                sum += filter[yf + anchor_y] *
                       convert_int4(in[(y + yf)*width + x]);
            }
            out[y*width + x] = convert_short4(sum);
        }
    }
    //for (int y=upper_y; y<height; y++) {
    //    for (int x=0; x<width; ++x) {
    //        out[y*width + x] = (uchar4) in[y*width + x];
    //    }
    //}
}


// Sobel filter in HIPAcc
#ifdef NO_SEP
class SobelFilterMask : public Kernel<short4> {
    private:
        Accessor<uchar4> &input;
        Domain &dom;
        Mask<int> &mask;
        const int size;

    public:
        SobelFilterMask(IterationSpace<short4> &iter, Accessor<uchar4> &input,
                Domain &dom, Mask<int> &mask, const int size) :
            Kernel(iter),
            input(input),
            dom(dom),
            mask(mask),
            size(size)
        { add_accessor(&input); }

        #ifdef USE_LAMBDA
        void kernel() {
            int4 sum = reduce(dom, Reduce::SUM, [&] () -> int4 {
                    return mask(dom) * convert_int4(input(dom));
                    });
            output() = convert_short4(sum);
        }
        #else
        void kernel() {
            const int anchor = size >> 1;
            int4 sum = {0, 0, 0, 0};

            for (int yf = -anchor; yf<=anchor; yf++) {
                for (int xf = -anchor; xf<=anchor; xf++) {
                    sum += mask(xf, yf)*convert_int4(input(xf, yf));
                }
            }

            output() = convert_short4(sum);
        }
        #endif
};
#else
class SobelFilterMaskRow : public Kernel<short4> {
    private:
        Accessor<uchar4> &input;
        Mask<int> &mask;
        const int size;

    public:
        SobelFilterMaskRow(IterationSpace<short4> &iter, Accessor<uchar4>
                &input, Mask<int> &mask, const int size):
            Kernel(iter),
            input(input),
            mask(mask),
            size(size)
        { add_accessor(&input); }

        #ifdef USE_LAMBDA
        void kernel() {
            int4 sum = convolve(mask, Reduce::SUM, [&] () -> int4 {
                    return mask() * convert_int4(input(mask));
                    });
            output() = convert_short4(sum);
        }
        #else
        void kernel() {
            const int anchor = size >> 1;
            int4 sum = {0, 0, 0, 0};

            for (int xf = -anchor; xf<=anchor; xf++) {
                sum += mask(xf, 0)*convert_int4(input(xf, 0));
            }

            output() = convert_short4(sum);
        }
        #endif
};
class SobelFilterMaskColumn : public Kernel<short4> {
    private:
        Accessor<short4> &input;
        Mask<int> &mask;
        const int size;

    public:
        SobelFilterMaskColumn(IterationSpace<short4> &iter, Accessor<short4>
                &input, Mask<int> &mask, const int size):
            Kernel(iter),
            input(input),
            mask(mask),
            size(size)
        { add_accessor(&input); }

        #ifdef USE_LAMBDA
        void kernel() {
            int4 sum = convolve(mask, Reduce::SUM, [&] () -> int4 {
                    return mask() * convert_int4(input(mask));
                    });
            output() = convert_short4(sum);
        }
        #else
        void kernel() {
            const int anchor = size >> 1;
            int4 sum = {0, 0, 0, 0};

            for (int yf = -anchor; yf<=anchor; yf++) {
                sum += mask(0, yf)*convert_int4(input(0, yf));
            }

            output() = convert_short4(sum);
        }
        #endif
};
#endif


/*************************************************************************
 * Main function                                                         *
 *************************************************************************/
int main(int argc, const char **argv) {
    double time0, time1, dt, min_dt;
    const int width = WIDTH;
    const int height = HEIGHT;
    const int size_x = SIZE_X;
    const int size_y = SIZE_Y;
    const int offset_x = size_x >> 1;
    const int offset_y = size_y >> 1;
    std::vector<float> timings;

    // only filter kernel sizes 3x3, 5x5, and 7x7 implemented
    if (size_x != size_y || !(size_x == 3 || size_x == 5 || size_x == 7)) {
        fprintf(stderr, "Wrong filter kernel size. Currently supported values: 3x3, 5x5, and 7x7!\n");
        exit(EXIT_FAILURE);
    }

    // filter coefficients
    #ifdef YORDER
    #ifdef CONST_MASK
    const
    #endif
    int mask[SIZE_Y][SIZE_X] = {
        #if SIZE_X==3
        { -1, -2, -1 },
        {  0,  0,  0 },
        {  1,  2,  1 }
        #endif
        #if SIZE_X==5
        { -1, -4, -6,  -4, -1 },
        { -2, -8, -12, -8, -2 },
        {  0,  0,  0,   0,  0 },
        {  2,  8,  12,  8,  2 },
        {  1,  4,  6,   4,  1 }
        #endif
        #if SIZE_X==7
        { -1, -6,  -15, -20,  -15, -6,  -1 },
        { -4, -24, -60, -80,  -60, -24, -4 },
        { -5, -30, -75, -100, -75, -30, -5 },
        {  0,  0,   0,   0,    0,   0,   0 },
        {  5,  30,  75,  100,  75,  30,  5 },
        {  4,  24,  60,  80,   60,  24,  4 },
        {  1,  6,   15,  20,   15,  6,   1 }
        #endif
    };
    #ifdef CONST_MASK
    const
    #endif
    int mask_x[1][SIZE_X] = {
        #if SIZE_X==3
        { 1, 2, 1 }
        #endif
        #if SIZE_X==5
        { 1, 4, 6, 4, 1 }
        #endif
        #if SIZE_X==7
        { 1, 6, 15, 20, 15, 6, 1 }
        #endif
    };
    #ifdef CONST_MASK
    const
    #endif
    int mask_y[SIZE_Y][1] = {
        #if SIZE_Y==3
        { -1 }, { 0 }, { +1 },
        #endif
        #if SIZE_Y==5
        { -1 }, { -2 }, { 0 }, { +2 }, { +1 },
        #endif
        #if SIZE_Y==7
        { -1 }, { -4 }, { -5 }, { 0 }, { 5 }, { 4 }, { 1 },
        #endif
    };
    #else
    #ifdef CONST_MASK
    const
    #endif
    int mask[SIZE_Y][SIZE_X] = {
        #if SIZE_X==3
        { -1, 0,  1 },
        { -2, 0,  2 },
        { -1, 0,  1 }
        #endif
        #if SIZE_X==5
        { -1,  -2, 0,  2, 1 },
        { -4,  -8, 0,  8, 4 },
        { -6, -12, 0, 12, 6 },
        { -4,  -8, 0,  8, 4 },
        { -1,  -2, 0,  2, 1 }
        #endif
        #if SIZE_X==7
        {  -1,  -4,   -5, 0,   5,  4,  1 },
        {  -6, -24,  -30, 0,  30, 24,  6 },
        { -15, -60,  -75, 0,  75, 60, 15 },
        { -20, -80, -100, 0, 100, 80, 20 },
        { -15, -60,  -75, 0,  75, 60, 15 },
        {  -6, -24,  -30, 0,  30, 24,  6 },
        {  -1,  -4,   -5, 0,   5,  4,  1 }
        #endif
    };
    #ifdef CONST_MASK
    const
    #endif
    int mask_x[1][SIZE_X] = {
        #if SIZE_X==3
        { -1, 0, +1 }
        #endif
        #if SIZE_X==5
        { -1, -2, 0, +2, +1 }
        #endif
        #if SIZE_X==7
        { -1, -4, -5, 0, 5, 4, 1 }
        #endif
    };
    #ifdef CONST_MASK
    const
    #endif
    int mask_y[SIZE_Y][1] = {
        #if SIZE_X==3
        { 1 }, { 2 }, { 1 },
        #endif
        #if SIZE_X==5
        { 1 }, { 4 }, { 6 }, { 4 }, { 1 },
        #endif
        #if SIZE_X==7
        { 1 }, { 6 }, { 15 }, { 20 }, { 15 }, { 6 }, { 1 },
        #endif
    };
    #endif

    // host memory for image of width x height pixels
    uchar4 *input = (uchar4 *)malloc(sizeof(uchar4)*width*height);
    uchar4 *reference_in = (uchar4 *)malloc(sizeof(uchar4)*width*height);
    short4 *reference_out = (short4 *)malloc(sizeof(short4)*width*height);
    short4 *reference_tmp = (short4 *)malloc(sizeof(short4)*width*height);

    // initialize data
    for (int y=0; y<height; ++y) {
        for (int x=0; x<width; ++x) {
            uchar val = rand()%256;
            input[y*width + x] = (uchar4){val, val, val, val};
            reference_in[y*width + x] = input[y*width + x];
            reference_out[y*width + x] = (short4){0, 0, 0, 0};
            reference_tmp[y*width + x] = (short4){0, 0, 0, 0};
        }
    }


    // input and output image of width x height pixels
    Image<uchar4> IN(width, height);
    Image<short4> OUT(width, height);
    Image<short4> TMP(width, height);

    // filter mask
    Mask<int> M(mask);
    Mask<int> MX(mask_x);
    Mask<int> MY(mask_y);

    Domain D(M);

    IterationSpace<short4> IsOut(OUT);
    IterationSpace<short4> IsTmp(TMP);

    IN = input;


    #ifndef OpenCV
    fprintf(stderr, "Calculating Sobel filter ...\n");
    float timing = 0.0f;

    // UNDEFINED
    #ifdef RUN_UNDEF
    #ifdef NO_SEP
    BoundaryCondition<uchar4> BcInUndef2(IN, M, Boundary::UNDEFINED);
    Accessor<uchar4> AccInUndef2(BcInUndef2);
    SobelFilterMask SFU(IsOut, AccInUndef2, D, M, size_x);

    SFU.execute();
    timing = hipacc_last_kernel_timing();
    #else
    BoundaryCondition<uchar4> BcInUndef(IN, MX, Boundary::UNDEFINED);
    Accessor<uchar4> AccInUndef(BcInUndef);
    SobelFilterMaskRow SFRU(IsTmp, AccInUndef, MX, size_x);

    BoundaryCondition<short4> BcTmpUndef(TMP, MY, Boundary::UNDEFINED);
    Accessor<short4> AccTmpUndef(BcTmpUndef);
    SobelFilterMaskColumn SFCU(IsOut, AccTmpUndef, MY, size_y);

    SFRU.execute();
    timing = hipacc_last_kernel_timing();
    SFCU.execute();
    timing += hipacc_last_kernel_timing();
    #endif
    #endif
    timings.push_back(timing);
    fprintf(stderr, "HIPACC (UNDEFINED): %.3f ms, %.3f Mpixel/s\n", timing, (width*height/timing)/1000);


    // CLAMP
    #ifdef NO_SEP
    BoundaryCondition<uchar4> BcInClamp2(IN, M, Boundary::CLAMP);
    Accessor<uchar4> AccInClamp2(BcInClamp2);
    SobelFilterMask SFC(IsOut, AccInClamp2, D, M, size_x);

    SFC.execute();
    timing = hipacc_last_kernel_timing();
    #else
    BoundaryCondition<uchar4> BcInClamp(IN, MX, Boundary::CLAMP);
    Accessor<uchar4> AccInClamp(BcInClamp);
    SobelFilterMaskRow SFRC(IsTmp, AccInClamp, MX, size_x);

    BoundaryCondition<short4> BcTmpClamp(TMP, MY, Boundary::CLAMP);
    Accessor<short4> AccTmpClamp(BcTmpClamp);
    SobelFilterMaskColumn SFCC(IsOut, AccTmpClamp, MY, size_y);

    SFRC.execute();
    timing = hipacc_last_kernel_timing();
    SFCC.execute();
    timing += hipacc_last_kernel_timing();
    #endif
    timings.push_back(timing);
    fprintf(stderr, "HIPACC (CLAMP): %.3f ms, %.3f Mpixel/s\n", timing, (width*height/timing)/1000);


    // REPEAT
    #ifdef NO_SEP
    BoundaryCondition<uchar4> BcInRepeat2(IN, M, Boundary::REPEAT);
    Accessor<uchar4> AccInRepeat2(BcInRepeat2);
    SobelFilterMask SFR(IsOut, AccInRepeat2, D, M, size_x);

    SFR.execute();
    timing = hipacc_last_kernel_timing();
    #else
    BoundaryCondition<uchar4> BcInRepeat(IN, MX, Boundary::REPEAT);
    Accessor<uchar4> AccInRepeat(BcInRepeat);
    SobelFilterMaskRow SFRR(IsTmp, AccInRepeat, MX, size_x);

    BoundaryCondition<short4> BcTmpRepeat(TMP, MY, Boundary::REPEAT);
    Accessor<short4> AccTmpRepeat(BcTmpRepeat);
    SobelFilterMaskColumn SFCR(IsOut, AccTmpRepeat, MY, size_y);

    SFRR.execute();
    timing = hipacc_last_kernel_timing();
    SFCR.execute();
    timing += hipacc_last_kernel_timing();
    #endif
    timings.push_back(timing);
    fprintf(stderr, "HIPACC (REPEAT): %.3f ms, %.3f Mpixel/s\n", timing, (width*height/timing)/1000);


    // MIRROR
    #ifdef NO_SEP
    BoundaryCondition<uchar4> BcInMirror2(IN, M, Boundary::MIRROR);
    Accessor<uchar4> AccInMirror2(BcInMirror2);
    SobelFilterMask SFM(IsOut, AccInMirror2, D, M, size_x);

    SFM.execute();
    timing = hipacc_last_kernel_timing();
    #else
    BoundaryCondition<uchar4> BcInMirror(IN, MX, Boundary::MIRROR);
    Accessor<uchar4> AccInMirror(BcInMirror);
    SobelFilterMaskRow SFRM(IsTmp, AccInMirror, MX, size_x);

    BoundaryCondition<short4> BcTmpMirror(TMP, MY, Boundary::MIRROR);
    Accessor<short4> AccTmpMirror(BcTmpMirror);
    SobelFilterMaskColumn SFCM(IsOut, AccTmpMirror, MY, size_y);

    SFRM.execute();
    timing = hipacc_last_kernel_timing();
    SFCM.execute();
    timing += hipacc_last_kernel_timing();
    #endif
    timings.push_back(timing);
    fprintf(stderr, "HIPACC (MIRROR): %.3f ms, %.3f Mpixel/s\n", timing, (width*height/timing)/1000);


    // CONSTANT
    #ifdef NO_SEP
    BoundaryCondition<uchar4> BcInConst2(IN, M, Boundary::CONSTANT, '1');
    Accessor<uchar4> AccInConst2(BcInConst2);
    SobelFilterMask SFConst(IsOut, AccInConst2, D, M, size_x);

    SFConst.execute();
    timing = hipacc_last_kernel_timing();
    #else
    BoundaryCondition<uchar4> BcInConst(IN, MX, Boundary::CONSTANT, (uchar4){'1','1','1','1'});
    Accessor<uchar4> AccInConst(BcInConst);
    SobelFilterMaskRow SFRConst(IsTmp, AccInConst, MX, size_x);

    BoundaryCondition<short4> BcTmpConst(TMP, MY, Boundary::CONSTANT, (short4){1,1,1,1});
    Accessor<short4> AccTmpConst(BcTmpConst);
    SobelFilterMaskColumn SFCConst(IsOut, AccTmpConst, MY, size_y);

    SFRConst.execute();
    timing = hipacc_last_kernel_timing();
    SFCConst.execute();
    timing += hipacc_last_kernel_timing();
    #endif
    timings.push_back(timing);
    fprintf(stderr, "HIPACC (CONSTANT): %.3f ms, %.3f Mpixel/s\n", timing, (width*height/timing)/1000);


    // get pointer to result data
    short4 *output = OUT.data();
    #endif



    #ifdef OpenCV
    #ifdef CPU
    fprintf(stderr, "\nCalculating OpenCV Sobel filter on the CPU ...\n");
    #else
    fprintf(stderr, "\nCalculating OpenCV Sobel filter on the GPU ...\n");
    #endif


    cv::Mat cv_data_in(height, width, CV_8UC4, input);
    cv::Mat cv_data_out(height, width, CV_16SC4, cv::Scalar(0));
    int ddepth = CV_16S;
    double scale = 1.0f;
    double delta = 0.0f;

    for (int brd_type=0; brd_type<5; brd_type++) {
        #ifdef CPU
        if (brd_type==cv::BORDER_WRAP) {
            // BORDER_WRAP is not supported on the CPU by OpenCV
            timings.push_back(0.0f);
            continue;
        }
        min_dt = DBL_MAX;
        for (int nt=0; nt<10; nt++) {
            time0 = time_ms();

            #ifdef YORDER
            cv::Sobel(cv_data_in, cv_data_out, ddepth, 0, 1, size_y, scale, delta, brd_type);
            #else
            cv::Sobel(cv_data_in, cv_data_out, ddepth, 1, 0, size_x, scale, delta, brd_type);
            #endif

            time1 = time_ms();
            dt = time1 - time0;
            if (dt < min_dt) min_dt = dt;
        }
        #else
        cv::gpu::GpuMat gpu_in, gpu_out;
        gpu_in.upload(cv_data_in);

        min_dt = DBL_MAX;
        for (int nt=0; nt<10; nt++) {
            time0 = time_ms();

            #ifdef YORDER
            cv::gpu::Sobel(gpu_in, gpu_out, -1, 0, 1, size_y, scale, brd_type);
            #else
            cv::gpu::Sobel(gpu_in, gpu_out, -1, 1, 0, size_x, scale, brd_type);
            #endif

            time1 = time_ms();
            dt = time1 - time0;
            if (dt < min_dt) min_dt = dt;
        }

        gpu_out.download(cv_data_out);
        #endif

        fprintf(stderr, "OpenCV(");
        switch (brd_type) {
            case IPL_BORDER_CONSTANT:
                fprintf(stderr, "CONSTANT");
                break;
            case IPL_BORDER_REPLICATE:
                fprintf(stderr, "CLAMP");
                break;
            case IPL_BORDER_REFLECT:
                fprintf(stderr, "MIRROR");
                break;
            case IPL_BORDER_WRAP:
                fprintf(stderr, "REPEAT");
                break;
            case IPL_BORDER_REFLECT_101:
                fprintf(stderr, "MIRROR_101");
                break;
            default:
                break;
        }
        timings.push_back(min_dt);
        fprintf(stderr, "): %.3f ms, %.3f Mpixel/s\n", min_dt, (width*height/min_dt)/1000);
    }

    // get pointer to result data
    short4 *output = (short4 *)cv_data_out.data;
    #endif

    // print statistics
    for (std::vector<float>::const_iterator it = timings.begin();
         it != timings.end(); ++it) {
        fprintf(stderr, "\t%.3f", *it);
    }
    fprintf(stderr, "\n\n");


    fprintf(stderr, "\nCalculating reference ...\n");
    min_dt = DBL_MAX;
    for (int nt=0; nt<3; nt++) {
        time0 = time_ms();

        // calculate reference
        #ifdef NO_SEP
        sobel_filter(reference_in, reference_out, (int *)mask, size_x, size_y, width, height);
        #else
        sobel_filter_row(reference_in, reference_tmp, (int *)mask_x, size_x, width, height);
        sobel_filter_column(reference_tmp, reference_out, (int *)mask_y, size_y, width, height);
        #endif

        time1 = time_ms();
        dt = time1 - time0;
        if (dt < min_dt) min_dt = dt;
    }
    fprintf(stderr, "Reference: %.3f ms, %.3f Mpixel/s\n", min_dt, (width*height/min_dt)/1000);

    fprintf(stderr, "\nComparing results ...\n");
    #ifdef OpenCV
    #ifndef CPU
    fprintf(stderr, "\nWarning: OpenCV implementation on the GPU is currently broken (wrong results) ...\n");
    #endif
    int upper_y = height-size_y+offset_y;
    int upper_x = width-size_x+offset_x;
    #else
    int upper_y = height-offset_y;
    int upper_x = width-offset_x;
    #endif
    // compare results
    for (int y=offset_y; y<upper_y; y++) {
        for (int x=offset_x; x<upper_x; x++) {
            if (reference_out[y*width + x].x != output[y*width + x].x ||
                reference_out[y*width + x].y != output[y*width + x].y ||
                reference_out[y*width + x].z != output[y*width + x].z ||
                reference_out[y*width + x].w != output[y*width + x].w) {
                fprintf(stderr, "Test FAILED, at (%d,%d): %d,%d,%d,%d vs. %d,%d,%d,%d\n", x, y,
                        reference_out[y*width + x].x,
                        reference_out[y*width + x].y,
                        reference_out[y*width + x].z,
                        reference_out[y*width + x].w,
                        output[y*width + x].x,
                        output[y*width + x].y,
                        output[y*width + x].z,
                        output[y*width + x].w);
                exit(EXIT_FAILURE);
            }
        }
    }
    fprintf(stderr, "Test PASSED\n");

    // memory cleanup
    free(input);
    free(reference_in);
    free(reference_tmp);
    free(reference_out);

    return EXIT_SUCCESS;
}

