//
// Copyright (c) 2012, University of Erlangen-Nuremberg
// Copyright (c) 2012, Siemens AG
// Copyright (c) 2010, ARM Limited
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

#include <iostream>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "hipacc.hpp"

// variables set by Makefile
//#define SIGMA_D 3
//#define SIGMA_R 5
//#define WIDTH 4096
//#define HEIGHT 4096
#define SIGMA_D SIZE_X
#define SIGMA_R SIZE_Y
#define CONVOLUTION_MASK
#define EPS 0.02f

using namespace hipacc;


// get time in milliseconds
double time_ms () {
    struct timeval tv;
    gettimeofday (&tv, NULL);

    return ((double)(tv.tv_sec) * 1e+3 + (double)(tv.tv_usec) * 1e-3);
}


// bilateral filter reference
void bilateral_filter(float *in, float *out, int sigma_d, int sigma_r, int
        width, int height) {
    float c_r = 1.0f/(2.0f*sigma_r*sigma_r);
    float c_d = 1.0f/(2.0f*sigma_d*sigma_d);
    float s = 0.0f;

    for (int y=2*sigma_d; y<(height-2*sigma_d); ++y) {
        for (int x=2*sigma_d; x<width-2*sigma_d; ++x) {
            float d = 0.0f;
            float p = 0.0f;

            for (int yf = -2*sigma_d; yf<=2*sigma_d; yf++) {
                for (int xf = -2*sigma_d; xf<=2*sigma_d; xf++) {
                    float diff = in[(y + yf)*width + x + xf] - in[y*width + x];

                    s = expf(-c_r * diff*diff) * expf(-c_d * xf*xf) * expf(-c_d
                            * yf*yf);
                    d += s;
                    p += s * in[(y + yf)*width + x + xf];
                }
            }
            out[y*width + x] = (float) (p / d);
        }
    }
}


namespace hipacc {
#ifndef CONVOLUTION_MASK
class BilateralFilter : public Kernel<float> {
    private:
        Accessor<float> &Input;
        int sigma_d;
        int sigma_r;

    public:
        BilateralFilter(IterationSpace<float> &IS, Accessor<float> &Input, int
                sigma_d, int sigma_r) :
            Kernel(IS),
            Input(Input),
            sigma_d(sigma_d),
            sigma_r(sigma_r)
        {
            addAccessor(&Input);
        }

        void kernel() {
            float c_r = 1.0f/(2.0f*sigma_r*sigma_r);
            float c_d = 1.0f/(2.0f*sigma_d*sigma_d);
            float d = 0.0f;
            float p = 0.0f;
            float s = 0.0f;

            for (int yf = -2*sigma_d; yf<=2*sigma_d; yf++) {
                for (int xf = -2*sigma_d; xf<=2*sigma_d; xf++) {
                    float diff = Input(xf, yf) - Input();

                    s = expf(-c_r * diff*diff) * expf(-c_d * xf*xf) * expf(-c_d
                            * yf*yf);
                    d += s;
                    p += s * Input(xf, yf);
                }
            }
            output() = (float) (p / d);
        }
};
#else
class BilateralFilterMask : public Kernel<float> {
    private:
        Accessor<float> &Input;
        Mask<float> &sMask;
        int sigma_d, sigma_r;

    public:
        BilateralFilterMask(IterationSpace<float> &IS, Accessor<float> &Input,
                Mask<float> &sMask, int sigma_d, int sigma_r) :
            Kernel(IS),
            Input(Input),
            sMask(sMask),
            sigma_d(sigma_d),
            sigma_r(sigma_r)
        {
            addAccessor(&Input);
        }

        void kernel() {
            float c_r = 1.0f/(2.0f*sigma_r*sigma_r);
            float d = 0.0f;
            float p = 0.0f;

            #if 0
            d = convolve(sMask, HipaccSUM, [&] () {
                    float diff = Input(sMask) - Input();
                    return expf(-c_r * diff*diff) * sMask();
                    });
            p = convolve(sMask, HipaccSUM, [&] () {
                    float diff = Input(sMask) - Input();
                    return expf(-c_r * diff*diff) * sMask() * Input(sMask);
                    });
            #else
            float s = 0.0f;

            for (int yf = -2*sigma_d; yf<=2*sigma_d; yf++) {
                for (int xf = -2*sigma_d; xf<=2*sigma_d; xf++) {
                    float diff = Input(xf, yf) - Input();

                    s = expf(-c_r * diff*diff) * sMask(xf, yf);
                    d += s;
                    p += s * Input(xf, yf);
                }
            }
            #endif

            output() = (float) (p / d);
        }
};
#endif
}


int main(int argc, const char **argv) {
    double time0, time1, dt;
    const int width = WIDTH;
    const int height = HEIGHT;
    const int sigma_d = SIGMA_D;
    const int sigma_r = SIGMA_R;

    // host memory for image of of widthxheight pixels
    float *host_in = (float *)malloc(sizeof(float)*width*height);
    float *host_out = (float *)malloc(sizeof(float)*width*height);
    float *reference_in = (float *)malloc(sizeof(float)*width*height);
    float *reference_out = (float *)malloc(sizeof(float)*width*height);

    // input and output image of widthxheight pixels
    Image<float> IN(width, height);
    Image<float> OUT(width, height);

    Accessor<float> AccIn(IN, width-4*sigma_d, height-4*sigma_d, 2*sigma_d, 2*sigma_d);

    // initialize data
    #define DELTA 0.001f
    for (int y=0; y<height; ++y) {
        for (int x=0; x<width; ++x) {
            host_in[y*width + x] = (float) (x*height + y) * DELTA;
            reference_in[y*width + x] = (float) (x*height + y) * DELTA;
            host_out[y*width + x] = (float) (3.12451);
            reference_out[y*width + x] = (float) (3.12451);
        }
    }

#if 0
    float gaussian_d[2*2*sigma_d+1][2*2*sigma_d+1];
    float gaussian[2*2*sigma_d+1];
    for (int xf=-2*sigma_d; xf<=2*sigma_d; xf++) {
        gaussian[xf+2*sigma_d] = expf(-1/(2.0f*sigma_d*sigma_d)*(xf*xf));
    }
    for (int yf=-2*sigma_d; yf<=2*sigma_d; yf++) {
        for (int xf=-2*sigma_d; xf<=2*sigma_d; xf++) {
            gaussian_d[yf+2*sigma_d][xf+2*sigma_d] = gaussian[yf+2*sigma_d] * gaussian[xf+2*SIGMA_D];
            fprintf(stderr, "%f, ", gaussian_d[yf+2*sigma_d][xf+2*sigma_d]);
        }
        fprintf(stderr, "\n");
    }
#endif
#if SIGMA_D==1
    const float mask[] = {
        0.018316f, 0.082085f, 0.135335f, 0.082085f, 0.018316f, 
        0.082085f, 0.367879f, 0.606531f, 0.367879f, 0.082085f, 
        0.135335f, 0.606531f, 1.000000f, 0.606531f, 0.135335f, 
        0.082085f, 0.367879f, 0.606531f, 0.367879f, 0.082085f, 
        0.018316f, 0.082085f, 0.135335f, 0.082085f, 0.018316f, 
    };
#endif
#if SIGMA_D==3
    const float mask[] = {
        0.018316, 0.033746, 0.055638, 0.082085, 0.108368, 0.128022, 0.135335, 0.128022, 0.108368, 0.082085, 0.055638, 0.033746, 0.018316, 
        0.033746, 0.062177, 0.102512, 0.151240, 0.199666, 0.235877, 0.249352, 0.235877, 0.199666, 0.151240, 0.102512, 0.062177, 0.033746, 
        0.055638, 0.102512, 0.169013, 0.249352, 0.329193, 0.388896, 0.411112, 0.388896, 0.329193, 0.249352, 0.169013, 0.102512, 0.055638, 
        0.082085, 0.151240, 0.249352, 0.367879, 0.485672, 0.573753, 0.606531, 0.573753, 0.485672, 0.367879, 0.249352, 0.151240, 0.082085, 
        0.108368, 0.199666, 0.329193, 0.485672, 0.641180, 0.757465, 0.800737, 0.757465, 0.641180, 0.485672, 0.329193, 0.199666, 0.108368, 
        0.128022, 0.235877, 0.388896, 0.573753, 0.757465, 0.894839, 0.945959, 0.894839, 0.757465, 0.573753, 0.388896, 0.235877, 0.128022, 
        0.135335, 0.249352, 0.411112, 0.606531, 0.800737, 0.945959, 1.000000, 0.945959, 0.800737, 0.606531, 0.411112, 0.249352, 0.135335, 
        0.128022, 0.235877, 0.388896, 0.573753, 0.757465, 0.894839, 0.945959, 0.894839, 0.757465, 0.573753, 0.388896, 0.235877, 0.128022, 
        0.108368, 0.199666, 0.329193, 0.485672, 0.641180, 0.757465, 0.800737, 0.757465, 0.641180, 0.485672, 0.329193, 0.199666, 0.108368, 
        0.082085, 0.151240, 0.249352, 0.367879, 0.485672, 0.573753, 0.606531, 0.573753, 0.485672, 0.367879, 0.249352, 0.151240, 0.082085, 
        0.055638, 0.102512, 0.169013, 0.249352, 0.329193, 0.388896, 0.411112, 0.388896, 0.329193, 0.249352, 0.169013, 0.102512, 0.055638, 
        0.033746, 0.062177, 0.102512, 0.151240, 0.199666, 0.235877, 0.249352, 0.235877, 0.199666, 0.151240, 0.102512, 0.062177, 0.033746, 
        0.018316, 0.033746, 0.055638, 0.082085, 0.108368, 0.128022, 0.135335, 0.128022, 0.108368, 0.082085, 0.055638, 0.033746, 0.018316, 
    };
#endif

    IterationSpace<float> BIS(OUT, width-4*sigma_d, height-4*sigma_d, 2*sigma_d, 2*sigma_d);

#ifdef CONVOLUTION_MASK
    Mask<float> M(4*sigma_d+1, 4*sigma_d+1);
    M = mask;
    BilateralFilterMask BF(BIS, AccIn, M, sigma_d, sigma_r);
#else
    BilateralFilter BF(BIS, AccIn, sigma_d, sigma_r);
#endif

    IN = host_in;
    OUT = host_out;

    // warmup
    BF.execute();

    fprintf(stderr, "Calculating bilateral filter ...\n");
    time0 = time_ms();

    BF.execute();

    time1 = time_ms();
    dt = time1 - time0;

    // get results
    host_out = OUT.getData();

    // Mpixel/s = (width*height/1000000) / (dt/1000) = (width*height/dt)/1000
    // NB: actually there are (width-d)*(height) output pixels
    fprintf(stderr, "Hipacc: %.3f ms, %.3f Mpixel/s\n", dt,
            ((width-4*sigma_d)*(height-4*sigma_d)/dt)/1000);


    fprintf(stderr, "\nCalculating reference ...\n");
    time0 = time_ms();

    // calculate reference
    bilateral_filter(reference_in, reference_out, sigma_d, sigma_r, width, height);

    time1 = time_ms();
    dt = time1 - time0;
    fprintf(stderr, "Reference: %.3f ms, %.3f Mpixel/s\n", dt,
            ((width-4*sigma_d)*(height-4*sigma_d)/dt)/1000);

    fprintf(stderr, "\nComparing results ...\n");
    // compare results
    float rms_err = 0.0f;   // RMS error
    for (int y=2*sigma_d; y<height-2*sigma_d; y++) {
        for (int x=2*sigma_d; x<width-2*sigma_d; x++) {
            float derr = reference_out[y*width + x] - host_out[y*width +x];
            rms_err += derr*derr;

            if (abs(derr) > EPS) {
                fprintf(stderr, "Test FAILED, at (%d,%d): %f vs. %f\n", x, y,
                        reference_out[y*width + x], host_out[y*width +x]);
                exit(EXIT_FAILURE);
            }
        }
    }
    rms_err = sqrtf(rms_err / (float((width-4*sigma_d)*(height-4*sigma_d))));
    // check RMS error
    if (rms_err > EPS) {
        fprintf(stderr, "Test FAILED: RMS error in image: %.3f > %.3f, aborting...\n", rms_err, EPS);
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Test PASSED\n");

    // memory cleanup
    free(host_in);
    //free(host_out);
    free(reference_in);
    free(reference_out);

    return EXIT_SUCCESS;
}
