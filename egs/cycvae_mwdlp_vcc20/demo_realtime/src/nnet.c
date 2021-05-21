/* Copyright (c) 2018 Mozilla
                 2008-2011 Octasic Inc.
                 2012-2017 Jean-Marc Valin */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/* Modified by Patrick Lumban Tobing (Nagoya University) on Sept.-Dec. 2020 - Mar. 2021,
   marked by PLT_<Sep20/Dec20/Jan21/Mar21> */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include "opus_types.h"
#include "arch.h"
#include "common.h"
#include "nnet.h"
#include "nnet_data.h"
#include "nnet_cv_data.h"
#include "mwdlp10net_cycvae_private.h"

//#define HALF_RAND_MAX (RAND_MAX / 2)
//#define HALF_RAND_MAX_FLT_MIN (HALF_RAND_MAX + FLT_MIN)
#define RAND_MAX_FLT_MIN_FLT_MIN (RAND_MAX + FLT_MIN + FLT_MIN)
//#define ONE_FLT_MIN (1 - FLT_MIN)

#define SOFTMAX_HACK

#ifdef __AVX__
#include "vec_avx.h"
#elif __ARM_NEON__
#include "vec_neon.h"
#else
#warning Compiling without any vectorization. This code will be very slow
#include "vec.h"
#endif

static OPUS_INLINE float relu(float x)
{
   return x < 0 ? 0 : x;
}


//PLT_Mar21
void sgemv_accum16_(float *out, const float *weights, int rows, int cols, int col_stride, const float *x)
{
 sgemv_accum16(out, weights, rows, cols, col_stride, x);
}

void sgemv_accum(float *out, const float *weights, int rows, int cols, int col_stride, const float *x)
{
 int i, j;
 for (i=0;i<rows;i++)
 {
    for (j=0;j<cols;j++) {
       out[i] += weights[j*col_stride + i]*x[j];
   }
 }
}

void compute_activation(float *output, float *input, int N, int activation)
{
   int i;
   if (activation == ACTIVATION_SIGMOID) {
      vec_sigmoid(output, input, N);
   } else if (activation == ACTIVATION_TANH) {
      vec_tanh(output, input, N);
   } else if (activation == ACTIVATION_TANHSHRINK) { //PLT_Sep20
      vec_tanhshrink(output, input, N);
   } else if (activation == ACTIVATION_EXP) { //PLT_Feb21
      vec_exp(output, input, N);
   } else if (activation == ACTIVATION_SIGMOID_EXP) { //PLT_Feb21
      vec_sigmoid_exp(output, input, N);
   } else if (activation == ACTIVATION_TANH_EXP) { //PLT_Feb21
      vec_tanh_exp(output, input, N);
   } else if (activation == ACTIVATION_TANH_EXP_NOCLAMP) { //PLT_Apr21
      //vec_tanh_exp_noclamp(output, input, N);
      vec_tanh_exp(output, input, N);
   } else if (activation == ACTIVATION_RELU) {
      for (i=0;i<N;i++)
         output[i] = relu(input[i]);
   } else if (activation == ACTIVATION_SOFTMAX) {
#ifdef SOFTMAX_HACK
      for (i=0;i<N;i++)
         output[i] = input[i];
#else
      float sum = 0;
      softmax(output, input, N);
      for (i=0;i<N;i++) {
         sum += output[i];
      }
      sum = 1.f/(sum+1e-30);
      for (i=0;i<N;i++)
         output[i] = sum*output[i];
#endif
   } else {
      //celt_assert(activation == ACTIVATION_LINEAR);
      for (i=0;i<N;i++)
         output[i] = input[i];
   }
}

void compute_dense(const DenseLayer *layer, float *output, const float *input)
{
   int i, N;
   //int N, M;
   //int stride;
   //M = layer->nb_inputs;
   N = layer->nb_neurons;
   //stride = N;
   //celt_assert(input != output);
   for (i=0;i<N;i++)
      output[i] = layer->bias[i];
   // sgemv_accum16(output, layer->input_weights, N, M, stride, input);
   sgemv_accum16(output, layer->input_weights, N, layer->nb_inputs, N, input);
   compute_activation(output, output, N, layer->activation);
}

//PLT_Dec20
void compute_dense_linear(const DenseLayer *layer, float *output, const float *input)
{
   int i, N;
   //int N, M;
   //int stride;
   //M = layer->nb_inputs;
   N = layer->nb_neurons;
   //stride = N;
   //celt_assert(input != output);
   for (i=0;i<N;i++)
      output[i] = layer->bias[i];
   // sgemv_accum16(output, layer->input_weights, N, M, stride, input);
   sgemv_accum16(output, layer->input_weights, N, layer->nb_inputs, N, input);
}

//PLT_Mar21
void compute_mdense_mwdlp10(const MDenseLayerMWDLP10 *layer, const DenseLayer *fc_layer,
    const float *prev_logits, float *output, const float *input, const int *last_output)
{
    //int i, j, k, l, m, c, n, last_idx;
    //int i, j, k, l, m, c, n, n_lpcbands_signs, n_lpcbands_mags, last_idx;
    //int i, j, k, l, m, n, n_lpcbands_signs, n_lpcbands_mags, last_idx;
    int i, j, n, n_lpcbands_signs, n_lpcbands_mags, last_idx;
    //int i, j, n, n_lpcbands_signs, n_lpcbands_mags, last_idx;
    //int i, c, n;
    //int j;
    //int n_lpcbands, n_c_lpcbands2;
    //int n_logitsbands, n_c_logitsbands2;
    //int last_idx;
    //float tmp[MDENSE_OUT];
    //float tmp[MDENSE_OUT], vec_out[MDENSE_OUT], dualfc_out[MDENSE_OUT_DUALFC];
    float vec_out[MDENSE_OUT], dualfc_out[MDENSE_OUT_DUALFC];
    //float signs[LPC_ORDER_MBANDS_2], mags[LPC_ORDER_MBANDS_2], mids[MID_OUT_MBANDS_2];
    //float lpc_signs[LPC_ORDER_MBANDS], lpc_mags[LPC_ORDER_MBANDS], res_mids[MID_OUT_MBANDS];
    //float res_mids[MID_OUT_MBANDS];
    //float *logits;
    //celt_assert(input != output);

    //compute dualfc output vectors
    for (i=0;i<MDENSE_OUT;i++)
       vec_out[i] = layer->bias[i];
       //tmp[i] = layer->bias[i];
    //sgemv_accum16(tmp, layer->input_weights, MDENSE_OUT, RNN_SUB_NEURONS, MDENSE_OUT, input);
    sgemv_accum16(vec_out, layer->input_weights, MDENSE_OUT, RNN_SUB_NEURONS, MDENSE_OUT, input);
    //for (i=0;i<MDENSE_OUT;i++) {
    //    printf("mdense_out [%d] %f\n", i, tmp[i]);
    //}
    //exit(0);
    //output_dim:2*N_MBANDS*(DLPC_ORDER*2+MID_OUT),e.g.,DLPC_ORDER=6,N_MBANDS=5,MID_OUT=32->440
    //signs, 1st 6*5*2
    //for (i=0;i<LPC_ORDER_MBANDS_4;i++)
    //   if (tmp[i] < -32) tmp[i] = -32;
    //   else if (tmp[i] > 32) tmp[i] = 32;
    //RNN_COPY(signs, tmp, LPC_ORDER_MBANDS_2);
    //RNN_COPY(vec_out, tmp, LPC_ORDER_MBANDS);
    //RNN_COPY(&vec_out[LPC_ORDER_MBANDS_2_MID_OUT_MBANDS], &tmp[LPC_ORDER_MBANDS], LPC_ORDER_MBANDS);
    //-1,1
    //compute_activation(signs, signs, LPC_ORDER_MBANDS_2, layer->activation_signs);
    compute_activation(vec_out, vec_out, LPC_ORDER_MBANDS, layer->activation_signs);
    compute_activation(&vec_out[LPC_ORDER_MBANDS_2_MID_OUT_MBANDS], &vec_out[LPC_ORDER_MBANDS_2_MID_OUT_MBANDS],
            LPC_ORDER_MBANDS, layer->activation_signs);
    //for (i=0;i<LPC_ORDER_MBANDS_2;i++) {
    //    printf("signs [%d] %f\n", i, signs[i]);
    //}
    //exit(0);
    //mags, 2nd 6*5*2
    //RNN_COPY(mags, &tmp[LPC_ORDER_MBANDS_2], LPC_ORDER_MBANDS_2);
    //RNN_COPY(&vec_out[LPC_ORDER_MBANDS], &tmp[LPC_ORDER_MBANDS_2], LPC_ORDER_MBANDS);
    //RNN_COPY(&vec_out[LPC_ORDER_MBANDS_3_MID_OUT_MBANDS], &tmp[LPC_ORDER_MBANDS_3], LPC_ORDER_MBANDS);
    //>= 0
    //compute_activation(mags, mags, LPC_ORDER_MBANDS_2, layer->activation_mags);
    compute_activation(&vec_out[LPC_ORDER_MBANDS], &vec_out[LPC_ORDER_MBANDS], LPC_ORDER_MBANDS, layer->activation_mags);
    compute_activation(&vec_out[LPC_ORDER_MBANDS_3_MID_OUT_MBANDS], &vec_out[LPC_ORDER_MBANDS_3_MID_OUT_MBANDS],
            LPC_ORDER_MBANDS, layer->activation_mags);
    //for (i=0;i<LPC_ORDER_MBANDS_2;i++) {
    //    printf("mags [%d] %f\n", i, mags[i]);
    //}
    //exit(0);
    //mids, last 32*5*2
    //RNN_COPY(mids, &tmp[LPC_ORDER_MBANDS_4], MID_OUT_MBANDS_2);
    //RNN_COPY(&vec_out[LPC_ORDER_MBANDS_2], &tmp[LPC_ORDER_MBANDS_4], MID_OUT_MBANDS);
    //RNN_COPY(&vec_out[LPC_ORDER_MBANDS_4_MID_OUT_MBANDS], &tmp[LPC_ORDER_MBANDS_4_MID_OUT_MBANDS], MID_OUT_MBANDS);
    //compute_activation(mids, mids, MID_OUT_MBANDS_2, layer->activation_mids);
    compute_activation(&vec_out[LPC_ORDER_MBANDS_2], &vec_out[LPC_ORDER_MBANDS_2],
            MID_OUT_MBANDS, layer->activation_mids);
    compute_activation(&vec_out[LPC_ORDER_MBANDS_4_MID_OUT_MBANDS], &vec_out[LPC_ORDER_MBANDS_4_MID_OUT_MBANDS],
            MID_OUT_MBANDS, layer->activation_mids);
    //for (i=0;i<MID_OUT_MBANDS_2;i++) {
    //    printf("mids [%d] %f\n", i, mids[i]);
    //}

    ////n_dim = DLPC_ORDER or MID_OUT; n_channel = 2
    ////n_bands x n_channel x n_dim --> n_bands x n_dim x n_channel
    ////FIXME: change structure in training script as above
    //for (n=0;n<N_MBANDS;n++) {
    //    i = n*DLPC_ORDER;
    //    j = n*2*DLPC_ORDER;
    //    k = n*MID_OUT;
    //    l = n*2*MID_OUT;
    //    m = j+DLPC_ORDER;
    //    //1st channel
    //    RNN_COPY(&vec_out[i], &signs[j], DLPC_ORDER);
    //    RNN_COPY(&vec_out[LPC_ORDER_MBANDS+i], &mags[j], DLPC_ORDER);
    //    RNN_COPY(&vec_out[LPC_ORDER_MBANDS_2+k], &mids[l], MID_OUT);
    //    //2nd channel
    //    RNN_COPY(&vec_out[LPC_ORDER_MBANDS_2_MID_OUT_MBANDS+i], &signs[m], DLPC_ORDER);
    //    RNN_COPY(&vec_out[LPC_ORDER_MBANDS_3_MID_OUT_MBANDS+i], &mags[m], DLPC_ORDER);
    //    RNN_COPY(&vec_out[LPC_ORDER_MBANDS_4_MID_OUT_MBANDS+k], &mids[l+MID_OUT], MID_OUT);
    //}
    //combine dualfc channels
    sgev_dualfc8(dualfc_out, layer->factors, MDENSE_OUT, vec_out);

    ////logits by last fc-layer
    for (n=0;n<N_MBANDS;n++)
        for (i=0,j=n*SQRT_QUANTIZE;i<SQRT_QUANTIZE;i++)
            output[j+i] = fc_layer->bias[i];
    //for (n=0;n<N_MBANDS;n++)
    //    for (i=0,j=n*DLPC_ORDER;i<DLPC_ORDER;i++)
    //        printf("signs [%d][%d] %f\n", n, i, dualfc_out[j+i]);
    //for (n=0;n<N_MBANDS;n++)
    //    for (i=0,j=n*DLPC_ORDER+LPC_ORDER_MBANDS;i<DLPC_ORDER;i++)
    //        printf("mags [%d][%d] %f\n", n, i, dualfc_out[j+i]);
    //for (n=0;n<N_MBANDS;n++)
    //    for (i=0,j=n*MID_OUT+LPC_ORDER_MBANDS_2;i<MID_OUT;i++)
    //        printf("mids [%d][%d] %f\n", n, i, dualfc_out[j+i]);
    sgemv_fclogits16(output, fc_layer->input_weights, SQRT_QUANTIZE, MID_OUT, N_MBANDS, &dualfc_out[LPC_ORDER_MBANDS_2]);
    //for (n=0;n<N_MBANDS;n++)
    //    for (i=0,j=n*SQRT_QUANTIZE;i<SQRT_QUANTIZE;i++)
    //        printf("out [%d][%d] %f\n", n, i, output[j+i]);
    compute_activation(output, output, SQRT_QUANTIZE_MBANDS, fc_layer->activation);

    //exit(0);
    //for (i=0;i<LPC_ORDER_MBANDS;i++) {
    //    lpc_signs[i] = 0;
    //    lpc_mags[i] = 0;
    //}
    //for (i=0;i<MID_OUT_MBANDS;i++)
    //    res_mids[i] = 0;

    //refine logits with data-driven linear prediction procedure
    for (n=0;n<N_MBANDS;n++) { //n_bands x 2 x n_lpc or 32, loop order is n_bands --> 2 --> n_lpc/32
        ////lpc: n_b x 2 x n_lpc
        //for (c=0,n_lpcbands=n*DLPC_ORDER,n_c_lpcbands2=n_lpcbands*2;c<2;c++) {
        //    for (i=0;i<DLPC_ORDER;i++,n_c_lpcbands2++) {
        //        //previous code uses shared factors for signs/mags between bands [c*DLPC_ORDER + i]
        //        //changed into band-dependent factors for signs/mags [n*DLPC_ORDER*2 + c*DLPC_ORDER + i]
        //        //lpc_signs[n_lpcbands+i] += signs[n_c_lpcbands2]*layer->factor_signs[n_c_lpcbands2];
        //        //lpc_mags[n_lpcbands+i] += mags[n_c_lpcbands2]*layer->factor_mags[n_c_lpcbands2];
        //        //lpc_signs[n_lpcbands+i] += signs[n_c_lpcbands2]*layer->factors[i+n_lpcbands+c*MDENSE_OUT_DUALFC];
        //        //lpc_mags[n_lpcbands+i] += mags[n_c_lpcbands2]*layer->factors[i+n_lpcbands+LPC_ORDER_MBANDS+c*MDENSE_OUT_DUALFC];
        //        lpc_signs[n_lpcbands+i] += vec_out[i+n_lpcbands+c*MDENSE_OUT_DUALFC]*layer->factors[i+n_lpcbands+c*MDENSE_OUT_DUALFC];
        //        lpc_mags[n_lpcbands+i] += vec_out[i+n_lpcbands+LPC_ORDER_MBANDS+c*MDENSE_OUT_DUALFC]*layer->factors[i+n_lpcbands+LPC_ORDER_MBANDS+c*MDENSE_OUT_DUALFC];
        //    }
        //}
        ////mids: n_b x 32
        //for (c=0,n_logitsbands=n*MID_OUT,n_c_logitsbands2=n_logitsbands*2;c<2;c++) {
        //    //factor of mids also band-dependent, indexing similar as above signs/mags with 32-dim
        //    for (i=0;i<MID_OUT;i++,n_c_logitsbands2++)
        //        //res_mids[n_logitsbands+i] += mids[n_c_logitsbands2]*layer->factor_mids[n_c_logitsbands2];
        //        //res_mids[n_logitsbands+i] += mids[n_c_logitsbands2]*layer->factors[i+n_logitsbands+LPC_ORDER_MBANDS_2+c*MDENSE_OUT_DUALFC];
        //        res_mids[n_logitsbands+i] += vec_out[i+n_logitsbands+LPC_ORDER_MBANDS_2+c*MDENSE_OUT_DUALFC]*layer->factors[i+n_logitsbands+LPC_ORDER_MBANDS_2+c*MDENSE_OUT_DUALFC];
        //}
        ////////logits: 32 x 32 [[o_1,...,o_256]_1,...,[o_1,...,o_256]_N]
        //logits = &output[n*SQRT_QUANTIZE];
        //for (i=0;i<SQRT_QUANTIZE;i++)
        //    logits[i] = fc_layer->bias[i];
        ////sgemv_accum16(logits, fc_layer->input_weights, SQRT_QUANTIZE, MID_OUT, SQRT_QUANTIZE, &dualfc_out[LPC_ORDER_MBANDS_2+n*MID_OUT]);
        //sgemv_accum16(logits, fc_layer->input_weights, SQRT_QUANTIZE, MID_OUT, SQRT_QUANTIZE, &res_mids[n_logitsbands]);
        //////for (i=0;i<SQRT_QUANTIZE;i++)
        //////    if (logits[i] > 32) logits[i] = 32;
        //compute_activation(logits, logits, SQRT_QUANTIZE, fc_layer->activation);
        ////for (i=0;i<MID_OUT;i++) {
        ////    printf("res_mids [%d][%d] %f\n", n, i, res_mids[n_logitsbands+i]);
        ////}
        ////for (i=0;i<SQRT_QUANTIZE;i++) {
        ////    printf("logits_out [%d][%d] %f\n", n, i, logits[i]);
        ////}
        ////refine logits using linear prediction with one-hot basis of previous samples and data-driven lpc
        ////last_output: [[o_1,...,o_N]_1,...,[o_1,...,o_N]_K]; lpc: [[o_1,...,o_K]_1,...,[o_1,...,o_K]_N]
        //for (i=0;i<DLPC_ORDER;i++) {
        for (i=0,j=n*SQRT_QUANTIZE,n_lpcbands_signs=n*DLPC_ORDER,n_lpcbands_mags=LPC_ORDER_MBANDS+n_lpcbands_signs;i<DLPC_ORDER;i++) {
        //for (i=0,n_lpcbands_signs=n*DLPC_ORDER,n_lpcbands_mags=LPC_ORDER_MBANDS+n_lpcbands_signs;i<DLPC_ORDER;i++) {
            last_idx = last_output[i*N_MBANDS+n];
            //logits[last_idx] += lpc_signs[n_lpcbands+i]*lpc_mags[n_lpcbands+i]*prev_logits[last_idx];
            //logits[last_idx] += dualfc_out[n_lpcbands_signs+i]*dualfc_out[n_lpcbands_mags+i]*prev_logits[last_idx];
            output[j+last_idx] += dualfc_out[n_lpcbands_signs+i]*dualfc_out[n_lpcbands_mags+i]*prev_logits[last_idx];
            //j = last_output[i*N_MBANDS+n];
            //logits[j] += lpc_signs[n_lpcbands+i]*lpc_mags[n_lpcbands+i];
            //if (logits[j] < -32) logits[j] = -32;
            //else if (logits[j] > 32) logits[j] = 32;
        }
        //for (i=0;i<DLPC_ORDER;i++) {
        //    j = last_output[i*N_MBANDS+n];
        //    if (logits[j] > 32) logits[j] = 32;
        //}
        //    //logits[last_output[i*N_MBANDS+n]] += lpc[n_lpcbands+i];
   }
   //for (i=0;i<MID_OUT_MBANDS;i++) {
   //    printf("res_mids [%d] %f\n", i, res_mids[i]);
   //}
   //for (i=0;i<MID_OUT_MBANDS;i++) {
   //    printf("logits_outs [%d] %f\n", i, output[i]);
   //}
   //for (i=0;i<LPC_ORDER_MBANDS;i++) {
   //    printf("lpc_signs [%d] %f\n", i, lpc_signs[i]);
   //}
   //for (i=0;i<LPC_ORDER_MBANDS;i++) {
   //    printf("lpc_mags [%d] %f\n", i, lpc_mags[i]);
   //}
   //exit(0);
}

//PLT_Mar21
void compute_mdense_mwdlp10_nodlpc(const MDenseLayerMWDLP10 *layer, const DenseLayer *fc_layer, float *output,
    const float *input)
{
    int i, j, n;
    //int i, c, n;
    //int j;
    //int n_logitsbands, n_c_logitsbands2;
    //float mids[MID_OUT_MBANDS_2], vec_out[MID_OUT_MBANDS_2];
    float vec_out[MID_OUT_MBANDS_2];
    //float res_mids[MID_OUT_MBANDS];
    float dualfc_out[MID_OUT_MBANDS];
    //float *logits;
    //celt_assert(input != output);

    //mid-logits by dualfc
    for (i=0;i<MID_OUT_MBANDS_2;i++)
       vec_out[i] = layer->bias[i];
       //mids[i] = layer->bias[i];
    //sgemv_accum16(mids, layer->input_weights, MID_OUT_MBANDS_2, RNN_SUB_NEURONS, MID_OUT_MBANDS_2, input);
    sgemv_accum16(vec_out, layer->input_weights, MID_OUT_MBANDS_2, RNN_SUB_NEURONS, MID_OUT_MBANDS_2, input);
    //exit(0);
    //mids, last 32*5*2
    //compute_activation(mids, mids, MID_OUT_MBANDS_2, layer->activation_mids);
    compute_activation(vec_out, vec_out, MID_OUT_MBANDS_2, layer->activation_mids);
    //for (i=0;i<MID_OUT_MBANDS_2;i++) {
    //    printf("mids [%d] %f\n", i, mids[i]);
    //}
    //exit(0);

    ////n_dim = DLPC_ORDER or MID_OUT; n_channel = 2
    ////n_bands x n_channel x n_dim --> n_bands x n_dim x n_channel
    ////FIXME: change structure in training script as above
    //for (n=0;n<N_MBANDS;n++) {
    //    i = n*MID_OUT;
    //    j = n*2*MID_OUT;
    //    //1st channel
    //    RNN_COPY(&vec_out[i], &mids[j], MID_OUT);
    //    //2nd channel
    //    RNN_COPY(&vec_out[MID_OUT_MBANDS+i], &mids[j+MID_OUT], MID_OUT);
    //}
    //combine dualfc channels
    sgev_dualfc8(dualfc_out, layer->factors, MDENSE_OUT, vec_out);

    //logits by last fc-layer
    for (n=0;n<N_MBANDS;n++)
        for (i=0,j=n*SQRT_QUANTIZE;i<SQRT_QUANTIZE;i++)
            output[j+i] = fc_layer->bias[i];
    sgemv_fclogits16(output, fc_layer->input_weights, SQRT_QUANTIZE, MID_OUT, N_MBANDS, dualfc_out);
    compute_activation(output, output, SQRT_QUANTIZE_MBANDS, fc_layer->activation);

    //for (i=0;i<MID_OUT_MBANDS;i++)
    //    res_mids[i] = 0;
    //for (n=0;n<N_MBANDS;n++) { //n_bands x 2 x n_lpc or 32, loop order is n_bands --> 2 --> n_lpc/32
    //    //mids: n_b x 32
    //    for (c=0,n_logitsbands=n*MID_OUT,n_c_logitsbands2=n_logitsbands*2;c<2;c++) {
    //        //factor of mids also band-dependent, indexing similar as above signs/mags with 32-dim
    //        for (i=0;i<MID_OUT;i++,n_c_logitsbands2++)
    //            res_mids[n_logitsbands+i] += mids[n_c_logitsbands2]*layer->factor_mids[n_c_logitsbands2];
    //    }
    //    //logits: 32 x 32 [[o_1,...,o_256]_1,...,[o_1,...,o_256]_N]
    //    logits = &output[n*SQRT_QUANTIZE];
    //    for (i=0;i<SQRT_QUANTIZE;i++)
    //        logits[i] = fc_layer->bias[i];
    //    sgemv_accum16(logits, fc_layer->input_weights, SQRT_QUANTIZE, MID_OUT, SQRT_QUANTIZE, &res_mids[n_logitsbands]);
    //    compute_activation(logits, logits, SQRT_QUANTIZE, fc_layer->activation);
    //}
    //exit(0);
}

//PLT_Sep20
void compute_gru3(const GRULayer *gru, float *state, const float *input)
{
   int i;
   //int N;
   //int stride;
   float zrh[RNN_SUB_NEURONS_3]; //reduce memory, set with this GRU's units
   float recur[RNN_SUB_NEURONS_3];
   float *z;
   float *r;
   float *h;
   //N = gru->nb_neurons;
   z = zrh; //swap with r, pytorch rzh, keras zrh
   r = &zrh[RNN_SUB_NEURONS];
   h = &zrh[RNN_SUB_NEURONS_2];
   //celt_assert(gru->nb_neurons <= MAX_RNN_NEURONS);
   //celt_assert(input != state);
   //celt_assert(gru->reset_after);
   //stride = 3*N;
   RNN_COPY(zrh, input, RNN_SUB_NEURONS_3);
   for (i=0;i<RNN_SUB_NEURONS_3;i++)
      recur[i] = gru->bias[i];
   sgemv_accum16(recur, gru->recurrent_weights, RNN_SUB_NEURONS_3, RNN_SUB_NEURONS, RNN_SUB_NEURONS_3, state);
   for (i=0;i<RNN_SUB_NEURONS_2;i++)
      zrh[i] += recur[i];
   compute_activation(zrh, zrh, RNN_SUB_NEURONS_2, ACTIVATION_SIGMOID);
   //compute_activation(zrh, zrh, RNN_SUB_NEURONS_2, ACTIVATION_SIGMOID_EXP);
   for (i=0;i<RNN_SUB_NEURONS;i++)
      h[i] += recur[RNN_SUB_NEURONS_2+i]*z[i];
      //h[i] += recur[RNN_SUB_NEURONS_2+i]*r[i];
   compute_activation(h, h, RNN_SUB_NEURONS, gru->activation);
   for (i=0;i<RNN_SUB_NEURONS;i++)
      state[i] = r[i]*state[i] + (1-r[i])*h[i];
}

//PLT_Sep20
void compute_sparse_gru(const SparseGRULayer *gru, float *state, const float *input)
{
   int i, j, k;
   //int N;
   float zrh[RNN_MAIN_NEURONS_3];
   float recur[RNN_MAIN_NEURONS_3];
   float *z;
   float *r;
   float *h;
   //N = gru->nb_neurons;
   z = zrh; //swap with r, pytorch rzh, keras zrh
   r = &zrh[RNN_MAIN_NEURONS];
   h = &zrh[RNN_MAIN_NEURONS_2];
   //celt_assert(gru->nb_neurons <= MAX_RNN_NEURONS);
   //celt_assert(input != state);
   //celt_assert(gru->reset_after);
   RNN_COPY(zrh, input, RNN_MAIN_NEURONS_3);
   for (i=0;i<RNN_MAIN_NEURONS_3;i++)
      recur[i] = gru->bias[i];
   for (k=0;k<3;k++)
      for (i=0,j=k*RNN_MAIN_NEURONS;i<RNN_MAIN_NEURONS;i++)
         recur[j + i] += gru->diag_weights[j + i]*state[i];
   sparse_sgemv_accum16(recur, gru->recurrent_weights, RNN_MAIN_NEURONS_3, gru->idx, state);
   //for (i=0;i<RNN_MAIN_NEURONS;i++)
   //     printf("is[%d] %f\n", i, state[i]);
   //for (i=0;i<RNN_MAIN_NEURONS_3;i++)
   //     printf("sg[%d] %f\n", i, recur[i]);
   for (i=0;i<RNN_MAIN_NEURONS_2;i++)
      zrh[i] += recur[i];
   compute_activation(zrh, zrh, RNN_MAIN_NEURONS_2, ACTIVATION_SIGMOID);
   //compute_activation(zrh, zrh, RNN_MAIN_NEURONS_2, ACTIVATION_SIGMOID_EXP);
   for (i=0;i<RNN_MAIN_NEURONS;i++)
      h[i] += recur[RNN_MAIN_NEURONS_2+i]*z[i];
      //h[i] += recur[RNN_MAIN_NEURONS_2+i]*r[i];
   compute_activation(h, h, RNN_MAIN_NEURONS, gru->activation);
   for (i=0;i<RNN_MAIN_NEURONS;i++)
      state[i] = r[i]*state[i] + (1-r[i])*h[i];
}

//PLT_Sep20
void compute_conv1d_linear(const Conv1DLayer *layer, float *output, float *mem, const float *input)
{
   int i;
   int N, M, state_size;
   //int stride;
   M = layer->nb_inputs*layer->kernel_size;
   N = layer->nb_neurons;
   state_size = layer->nb_inputs*(layer->kernel_size-1);
   float tmp[M]; //set to input_size*kernel_size
   //celt_assert(input != output);
   RNN_COPY(tmp, mem, state_size); //get state_size of last frame (in*(kernel_size-1))
   RNN_COPY(&tmp[state_size], input, layer->nb_inputs); //append current input frame
   //for (int j=0;j<layer->kernel_size;j++) {
   //    for (i=0;i<layer->nb_inputs;i++) {
   //        printf("tmp [%d][%d] %f\n", j, i, tmp[j*layer->nb_inputs+i]);
   //    }
   //}
   // compute conv
   for (i=0;i<N;i++)
      output[i] = layer->bias[i];
   sgemv_accum16(output, layer->input_weights, N, M, N, tmp);
   //no activation (linear)
   RNN_COPY(mem, &tmp[layer->nb_inputs], state_size); //set state size for next frame
}

//PLT_Sep20
int sample_from_pdf_mwdlp(const float *pdf, int N)
{
    int i;
    float r;
    float tmp[SQRT_QUANTIZE], cdf[SQRT_QUANTIZE], sum, norm;
    for (i=0;i<N;i++)
        tmp[i] = pdf[i];
    softmax(tmp, tmp, N);
    for (i=0,sum=0;i<N;i++)
        sum += tmp[i];
    norm = 1.f/sum;
    /* Convert tmp to a CDF (sum of all previous probs., init. with 0) */
    cdf[0] = 0;
    for (i=1;i<N;i++)
        cdf[i] = cdf[i-1] + norm*tmp[i-1];
    /* Do the sampling (from the cdf). */
    r = (float) rand() / RAND_MAX; //r ~ [0,1]
    for (i=N-1;i>0;i--)
        if (r >= cdf[i]) return i; //largest cdf that is less/equal than r
    return 0;
}

//PLT_Dec20
void compute_normalize(const NormStats *norm_stats, float *input_output)
{
  for (int i=0;i<norm_stats->n_dim;i++)
    input_output[i] = (input_output[i] - norm_stats->mean[i]) / norm_stats->std[i];
}

//PLT_Dec20
void compute_denormalize(const NormStats *norm_stats, float *input_output)
{
  for (int i=0;i<norm_stats->n_dim;i++)
    input_output[i] = input_output[i] * norm_stats->std[i] + norm_stats->mean[i];
}

//PLT_Jan21
void compute_sparse_gru_enc_melsp(const SparseFrameGRULayer *gru, float *state, const float *input)
{
   int i, j, k;
   float zrh[RNN_ENC_MELSP_NEURONS_3];
   float recur[RNN_ENC_MELSP_NEURONS_3];
   float *z;
   float *r;
   float *h;

   z = zrh; //swap with r, pytorch rzh, keras zrh
   r = &zrh[RNN_ENC_MELSP_NEURONS];
   h = &zrh[RNN_ENC_MELSP_NEURONS_2];

   for (i=0;i<RNN_ENC_MELSP_NEURONS_3;i++) {
      recur[i] = gru->recurrent_bias[i];
      zrh[i] = gru->input_bias[i];
   }
   for (k=0;k<3;k++)
      for (i=0,j=k*RNN_ENC_MELSP_NEURONS;i<RNN_ENC_MELSP_NEURONS;i++)
         recur[j + i] += gru->diag_weights[j + i]*state[i];
   sparse_sgemv_accum16(recur, gru->recurrent_weights, RNN_ENC_MELSP_NEURONS_3, gru->idx, state);
   sgemv_accum16(zrh, gru->input_weights, RNN_ENC_MELSP_NEURONS_3, FEATURE_CONV_ENC_MELSP_OUT_SIZE, RNN_ENC_MELSP_NEURONS_3, input);

   for (i=0;i<RNN_ENC_MELSP_NEURONS_2;i++)
      zrh[i] += recur[i]; //z_t and r_t computed in a similar way : sigmoid(in_t + W_z*h_{t-1})
   compute_activation(zrh, zrh, RNN_ENC_MELSP_NEURONS_2, ACTIVATION_SIGMOID);
   //compute_activation(zrh, zrh, RNN_ENC_MELSP_NEURONS_2, ACTIVATION_SIGMOID_EXP);

   for (i=0;i<RNN_ENC_MELSP_NEURONS;i++)
      h[i] += recur[RNN_ENC_MELSP_NEURONS_2+i]*z[i]; //n_t = tanh(in_t + r_t o W_n*h_{t-1})
   compute_activation(h, h, RNN_ENC_MELSP_NEURONS, ACTIVATION_TANH);
   //compute_activation(h, h, RNN_ENC_MELSP_NEURONS, gru->activation);

   for (i=0;i<RNN_ENC_MELSP_NEURONS;i++)
      state[i] = r[i]*state[i] + (1-r[i])*h[i]; //h_t = z_t o h_{t-1} + (1-z_t) o n_t
}

//PLT_Jan21
void compute_sparse_gru_enc_excit(const SparseFrameGRULayer *gru, float *state, const float *input)
{
   int i, j, k;
   float zrh[RNN_ENC_EXCIT_NEURONS_3];
   float recur[RNN_ENC_EXCIT_NEURONS_3];
   float *z;
   float *r;
   float *h;

   z = zrh; //swap with r, pytorch rzh, keras zrh
   r = &zrh[RNN_ENC_EXCIT_NEURONS];
   h = &zrh[RNN_ENC_EXCIT_NEURONS_2];

   for (i=0;i<RNN_ENC_EXCIT_NEURONS_3;i++) {
      recur[i] = gru->recurrent_bias[i];
      zrh[i] = gru->input_bias[i];
   }
   for (k=0;k<3;k++)
      for (i=0,j=k*RNN_ENC_EXCIT_NEURONS;i<RNN_ENC_EXCIT_NEURONS;i++)
         recur[j + i] += gru->diag_weights[j + i]*state[i];
   sparse_sgemv_accum16(recur, gru->recurrent_weights, RNN_ENC_EXCIT_NEURONS_3, gru->idx, state);
   sgemv_accum16(zrh, gru->input_weights, RNN_ENC_EXCIT_NEURONS_3, FEATURE_CONV_ENC_EXCIT_OUT_SIZE, RNN_ENC_EXCIT_NEURONS_3, input);

   for (i=0;i<RNN_ENC_EXCIT_NEURONS_2;i++)
      zrh[i] += recur[i]; //z_t and r_t computed in a similar way : sigmoid(in_t + W_z*h_{t-1})
   //compute_activation(zrh, zrh, RNN_ENC_EXCIT_NEURONS_2, ACTIVATION_SIGMOID);
   compute_activation(zrh, zrh, RNN_ENC_EXCIT_NEURONS_2, ACTIVATION_SIGMOID_EXP);

   for (i=0;i<RNN_ENC_EXCIT_NEURONS;i++)
      h[i] += recur[RNN_ENC_EXCIT_NEURONS_2+i]*z[i]; //n_t = tanh(in_t + r_t o W_n*h_{t-1})
   //compute_activation(h, h, RNN_ENC_EXCIT_NEURONS, ACTIVATION_TANH);
   compute_activation(h, h, RNN_ENC_EXCIT_NEURONS, gru->activation);

   for (i=0;i<RNN_ENC_EXCIT_NEURONS;i++)
      state[i] = r[i]*state[i] + (1-r[i])*h[i]; //h_t = z_t o h_{t-1} + (1-z_t) o n_t
}

//PLT_Dec20
void compute_gru_spk(const FrameGRULayer *gru, float *state, const float *input)
{
   int i;
   float zrh[RNN_SPK_NEURONS_3];
   float recur[RNN_SPK_NEURONS_3];
   float *z;
   float *r;
   float *h;

   z = zrh; //swap with r, pytorch rzh, keras zrh
   r = &zrh[RNN_SPK_NEURONS];
   h = &zrh[RNN_SPK_NEURONS_2];

   RNN_COPY(zrh, input, RNN_SPK_NEURONS_3);

   for (i=0;i<RNN_SPK_NEURONS_3;i++) {
      recur[i] = gru->recurrent_bias[i];
      zrh[i] = gru->input_bias[i];
   }
   sgemv_accum16(recur, gru->recurrent_weights, RNN_SPK_NEURONS_3, RNN_SPK_NEURONS, RNN_SPK_NEURONS_3, state);
   sgemv_accum16(zrh, gru->input_weights, RNN_SPK_NEURONS_3, FEATURE_N_SPK_LAT_DIM_EXCIT_MELSP, RNN_SPK_NEURONS_3, input);

   for (i=0;i<RNN_SPK_NEURONS_2;i++)
      zrh[i] += recur[i]; //z_t and r_t computed in a similar way : sigmoid(in_t + W_z*h_{t-1})
   //compute_activation(zrh, zrh, RNN_SPK_NEURONS_2, ACTIVATION_SIGMOID);
   compute_activation(zrh, zrh, RNN_SPK_NEURONS_2, ACTIVATION_SIGMOID_EXP);

   for (i=0;i<RNN_SPK_NEURONS;i++)
      h[i] += recur[RNN_SPK_NEURONS_2+i]*z[i]; //n_t = tanh(in_t + r_t o W_n*h_{t-1})
   //compute_activation(h, h, RNN_SPK_NEURONS, ACTIVATION_TANH);
   compute_activation(h, h, RNN_SPK_NEURONS, gru->activation);

   for (i=0;i<RNN_SPK_NEURONS;i++)
      state[i] = r[i]*state[i] + (1-r[i])*h[i]; //h_t = z_t o h_{t-1} + (1-z_t) o n_t
}

//PLT_Jan21
void compute_sparse_gru_dec_melsp(const SparseFrameGRULayer *gru, float *state, const float *input)
{
   int i, j, k;
   float zrh[RNN_DEC_MELSP_NEURONS_3];
   float recur[RNN_DEC_MELSP_NEURONS_3];
   float *z;
   float *r;
   float *h;

   z = zrh; //swap with r, pytorch rzh, keras zrh
   r = &zrh[RNN_DEC_MELSP_NEURONS];
   h = &zrh[RNN_DEC_MELSP_NEURONS_2];

   for (i=0;i<RNN_DEC_MELSP_NEURONS_3;i++) {
      recur[i] = gru->recurrent_bias[i];
      zrh[i] = gru->input_bias[i];
   }
   for (k=0;k<3;k++)
      for (i=0,j=k*RNN_DEC_MELSP_NEURONS;i<RNN_DEC_MELSP_NEURONS;i++)
         recur[j + i] += gru->diag_weights[j + i]*state[i];
   sparse_sgemv_accum16(recur, gru->recurrent_weights, RNN_DEC_MELSP_NEURONS_3, gru->idx, state);
   sgemv_accum16(zrh, gru->input_weights, RNN_DEC_MELSP_NEURONS_3, FEATURE_CONV_DEC_MELSP_OUT_SIZE, RNN_DEC_MELSP_NEURONS_3, input);

   for (i=0;i<RNN_DEC_MELSP_NEURONS_2;i++)
      zrh[i] += recur[i]; //z_t and r_t computed in a similar way : sigmoid(in_t + W_z*h_{t-1})
   //compute_activation(zrh, zrh, RNN_DEC_MELSP_NEURONS_2, ACTIVATION_SIGMOID);
   compute_activation(zrh, zrh, RNN_DEC_MELSP_NEURONS_2, ACTIVATION_SIGMOID_EXP);

   for (i=0;i<RNN_DEC_MELSP_NEURONS;i++)
      h[i] += recur[RNN_DEC_MELSP_NEURONS_2+i]*z[i]; //n_t = tanh(in_t + r_t o W_n*h_{t-1})
   //compute_activation(h, h, RNN_DEC_MELSP_NEURONS, ACTIVATION_TANH);
   compute_activation(h, h, RNN_DEC_MELSP_NEURONS, gru->activation);

   for (i=0;i<RNN_DEC_MELSP_NEURONS;i++)
      state[i] = r[i]*state[i] + (1-r[i])*h[i]; //h_t = z_t o h_{t-1} + (1-z_t) o n_t
}

//PLT_Apr21
void compute_sampling_gauss(float *mu, const float *std, int dim)
{
    //float r;
    float u1, u2 = 0, mag = 0;
    for (int i=0;i<dim;i++) {
        //r = ((float) rand() - HALF_RAND_MAX) / HALF_RAND_MAX_FLT_MIN; //r ~ (-1,1)
        //// loc - sign(r)*scale*log(1-2|r/2|)
        //if (r > 0) loc[i] -= scale[i] * log(1-r);
        //else loc[i] += scale[i] * log(1+r);
        if (i % 2 == 0) {
            u1 = ((float) rand() + FLT_MIN) / RAND_MAX_FLT_MIN_FLT_MIN; //u1 ~ (0,1)
            u2 = ((float) rand() + FLT_MIN) / RAND_MAX_FLT_MIN_FLT_MIN; //u2 ~ (0,1)
            ////u1 ~ (0,1)
            //u1 = ((float) rand()) / RAND_MAX;
            //if (u1 == 0) u1 = FLT_MIN;
            //else if (u1 == 1) u1 = ONE_FLT_MIN;
            ////u2 ~ (0,1)
            //u2 = ((float) rand()) / RAND_MAX;
            //if (u2 == 0) u2 = FLT_MIN;
            //else if (u2 == 1) u2 = ONE_FLT_MIN;
            //z0 = sqrt(-2*ln(u1))*cos(2pi*u2)
            //z1 = sqrt(-2*ln(u1))*sin(2pi*u2)
            mag = sqrt(-2*log(u1));
            u2 *= 6.283185307179586476925286766559;
            //mu[i] += std[i]*mag*cos(u2);
            //temperature sampling: 0.25
            mu[i] += 0.25*std[i]*mag*cos(u2);
        //} else mu[i] += std[i]*mag*sin(u2);
        } else mu[i] += 0.25*std[i]*mag*sin(u2);
    }
}

//PLT_Dec20
void compute_spkidtr(const DenseLayer *in_layer, const DenseLayer *out_layer, float *output, const float *input)
{
   int i, N;
   //transform to 2-dim
   N = in_layer->nb_neurons;
   float tmp[N];
   for (i=0;i<N;i++) {
      tmp[i] = in_layer->bias[i];
   //   printf("[%d] %f ", i, tmp[i]);
   }
   //printf("\n");
   sgemv_accum16(tmp, in_layer->input_weights, N, in_layer->nb_inputs, N, input);
   //printf("2-dim %f %f\n", tmp[0], tmp[1]);
   compute_activation(tmp, tmp, N, in_layer->activation);
   //printf("2-dim tanh %f %f\n", tmp[0], tmp[1]);
   printf("2-dim spk-coord: %f %f\n", tmp[0], tmp[1]);
   //transform to N_SPK-dim
   N = out_layer->nb_neurons;
   for (i=0;i<N;i++) {
      output[i] = out_layer->bias[i];
   //   printf("[%d] %f ", i, output[i]);
   }
   //printf("\n");
   sgemv_accum16(output, out_layer->input_weights, N, out_layer->nb_inputs, N, tmp);
   compute_activation(output, output, N, out_layer->activation);
}

//PLT_Dec20
void compute_spkidtr_coord(const DenseLayer *layer, float *output, const float *input)
{
   int i, N;
   //transform to N_SPK-dim [from input 2-dim]
   N = layer->nb_neurons;
   for (i=0;i<N;i++)
      output[i] = layer->bias[i];
   sgemv_accum16(output, layer->input_weights, N, layer->nb_inputs, N, input);
   compute_activation(output, output, N, layer->activation);
}