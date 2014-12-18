/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


/**
 * @file
 * Unit tests for type conversion.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */


#include "util/u_pointer.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_type.h"
#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_conv.h"
#include "gallivm/lp_bld_debug.h"
#include "lp_test.h"


typedef void (*conv_test_ptr_t)(const void *src, const void *dst);


void
write_tsv_header(FILE *fp)
{
   fprintf(fp,
           "result\t"
           "cycles_per_channel\t"
           "src_type\t"
           "dst_type\n");

   fflush(fp);
}


static void
write_tsv_row(FILE *fp,
              struct lp_type src_type,
              struct lp_type dst_type,
              double cycles,
              boolean success)
{
   fprintf(fp, "%s\t", success ? "pass" : "fail");

   fprintf(fp, "%.1f\t", cycles / MAX2(src_type.length, dst_type.length));

   dump_type(fp, src_type);
   fprintf(fp, "\t");

   dump_type(fp, dst_type);
   fprintf(fp, "\n");

   fflush(fp);
}


static void
dump_conv_types(FILE *fp,
               struct lp_type src_type,
               struct lp_type dst_type)
{
   fprintf(fp, "src_type=");
   dump_type(fp, src_type);

   fprintf(fp, " dst_type=");
   dump_type(fp, dst_type);

   fprintf(fp, " ...\n");
   fflush(fp);
}


static LLVMValueRef
add_conv_test(struct gallivm_state *gallivm,
              struct lp_type src_type, unsigned num_srcs,
              struct lp_type dst_type, unsigned num_dsts)
{
   LLVMModuleRef module = gallivm->module;
   LLVMContextRef context = gallivm->context;
   LLVMBuilderRef builder = gallivm->builder;
   LLVMTypeRef args[2];
   LLVMValueRef func;
   LLVMValueRef src_ptr;
   LLVMValueRef dst_ptr;
   LLVMBasicBlockRef block;
   LLVMValueRef src[LP_MAX_VECTOR_LENGTH];
   LLVMValueRef dst[LP_MAX_VECTOR_LENGTH];
   unsigned i;

   args[0] = LLVMPointerType(lp_build_vec_type(gallivm, src_type), 0);
   args[1] = LLVMPointerType(lp_build_vec_type(gallivm, dst_type), 0);

   func = LLVMAddFunction(module, "test",
                          LLVMFunctionType(LLVMVoidTypeInContext(context),
                                           args, 2, 0));
   LLVMSetFunctionCallConv(func, LLVMCCallConv);
   src_ptr = LLVMGetParam(func, 0);
   dst_ptr = LLVMGetParam(func, 1);

   block = LLVMAppendBasicBlockInContext(context, func, "entry");
   LLVMPositionBuilderAtEnd(builder, block);

   for(i = 0; i < num_srcs; ++i) {
      LLVMValueRef index = LLVMConstInt(LLVMInt32TypeInContext(context), i, 0);
      LLVMValueRef ptr = LLVMBuildGEP(builder, src_ptr, &index, 1, "");
      src[i] = LLVMBuildLoad(builder, ptr, "");
   }

   lp_build_conv(gallivm, src_type, dst_type, src, num_srcs, dst, num_dsts);

   for(i = 0; i < num_dsts; ++i) {
      LLVMValueRef index = LLVMConstInt(LLVMInt32TypeInContext(context), i, 0);
      LLVMValueRef ptr = LLVMBuildGEP(builder, dst_ptr, &index, 1, "");
      LLVMBuildStore(builder, dst[i], ptr);
   }

   LLVMBuildRetVoid(builder);;

   gallivm_verify_function(gallivm, func);

   return func;
}


PIPE_ALIGN_STACK
static boolean
test_one(unsigned verbose,
         FILE *fp,
         struct lp_type src_type,
         struct lp_type dst_type)
{
   struct gallivm_state *gallivm;
   LLVMValueRef func = NULL;
   conv_test_ptr_t conv_test_ptr;
   boolean success;
   const unsigned n = LP_TEST_NUM_SAMPLES;
   int64_t cycles[LP_TEST_NUM_SAMPLES];
   double cycles_avg = 0.0;
   unsigned num_srcs;
   unsigned num_dsts;
   double eps;
   unsigned i, j;

   if ((src_type.width >= dst_type.width && src_type.length > dst_type.length) ||
       (src_type.width <= dst_type.width && src_type.length < dst_type.length)) {
      return TRUE;
   }

   /* Known failures
    * - fixed point 32 -> float 32
    * - float 32 -> signed normalised integer 32
    */
   if ((src_type.floating && !dst_type.floating && dst_type.sign && dst_type.norm && src_type.width == dst_type.width) ||
       (!src_type.floating && dst_type.floating && src_type.fixed && src_type.width == dst_type.width)) {
      return TRUE;
   }

   /* Known failures
    * - fixed point 32 -> float 32
    * - float 32 -> signed normalised integer 32
    */
   if ((src_type.floating && !dst_type.floating && dst_type.sign && dst_type.norm && src_type.width == dst_type.width) ||
       (!src_type.floating && dst_type.floating && src_type.fixed && src_type.width == dst_type.width)) {
      return TRUE;
   }

   if(verbose >= 1)
      dump_conv_types(stderr, src_type, dst_type);

   if (src_type.length > dst_type.length) {
      num_srcs = 1;
      num_dsts = src_type.length/dst_type.length;
   }
   else if (src_type.length < dst_type.length) {
      num_dsts = 1;
      num_srcs = dst_type.length/src_type.length;
   }
   else  {
      num_dsts = 1;
      num_srcs = 1;
   }

   /* We must not loose or gain channels. Only precision */
   assert(src_type.length * num_srcs == dst_type.length * num_dsts);

   eps = MAX2(lp_const_eps(src_type), lp_const_eps(dst_type));

   gallivm = gallivm_create("test_module");

   func = add_conv_test(gallivm, src_type, num_srcs, dst_type, num_dsts);

   gallivm_compile_module(gallivm);

   conv_test_ptr = (conv_test_ptr_t)gallivm_jit_function(gallivm, func);

   gallivm_free_ir(gallivm);

   success = TRUE;
   for(i = 0; i < n && success; ++i) {
      unsigned src_stride = src_type.length*src_type.width/8;
      unsigned dst_stride = dst_type.length*dst_type.width/8;
      PIPE_ALIGN_VAR(LP_MIN_VECTOR_ALIGN) uint8_t src[LP_MAX_VECTOR_LENGTH*LP_MAX_VECTOR_LENGTH];
      PIPE_ALIGN_VAR(LP_MIN_VECTOR_ALIGN) uint8_t dst[LP_MAX_VECTOR_LENGTH*LP_MAX_VECTOR_LENGTH];
      double fref[LP_MAX_VECTOR_LENGTH*LP_MAX_VECTOR_LENGTH];
      uint8_t ref[LP_MAX_VECTOR_LENGTH*LP_MAX_VECTOR_LENGTH];
      int64_t start_counter = 0;
      int64_t end_counter = 0;

      for(j = 0; j < num_srcs; ++j) {
         random_vec(src_type, src + j*src_stride);
         read_vec(src_type, src + j*src_stride, fref + j*src_type.length);
      }

      for(j = 0; j < num_dsts; ++j) {
         write_vec(dst_type, ref + j*dst_stride, fref + j*dst_type.length);
      }

      start_counter = rdtsc();
      conv_test_ptr(src, dst);
      end_counter = rdtsc();

      cycles[i] = end_counter - start_counter;

      for(j = 0; j < num_dsts; ++j) {
         if(!compare_vec_with_eps(dst_type, dst + j*dst_stride, ref + j*dst_stride, eps))
            success = FALSE;
      }

      if (!success || verbose >= 3) {
         if(verbose < 1)
            dump_conv_types(stderr, src_type, dst_type);
         if (success) {
            fprintf(stderr, "PASS\n");
         }
         else {
            fprintf(stderr, "MISMATCH\n");
         }

         for(j = 0; j < num_srcs; ++j) {
            fprintf(stderr, "  Src%u: ", j);
            dump_vec(stderr, src_type, src + j*src_stride);
            fprintf(stderr, "\n");
         }

#if 1
         fprintf(stderr, "  Ref: ");
         for(j = 0; j < src_type.length*num_srcs; ++j)
            fprintf(stderr, " %f", fref[j]);
         fprintf(stderr, "\n");
#endif

         for(j = 0; j < num_dsts; ++j) {
            fprintf(stderr, "  Dst%u: ", j);
            dump_vec(stderr, dst_type, dst + j*dst_stride);
            fprintf(stderr, "\n");

            fprintf(stderr, "  Ref%u: ", j);
            dump_vec(stderr, dst_type, ref + j*dst_stride);
            fprintf(stderr, "\n");
         }
      }
   }

   /*
    * Unfortunately the output of cycle counter is not very reliable as it comes
    * -- sometimes we get outliers (due IRQs perhaps?) which are
    * better removed to avoid random or biased data.
    */
   {
      double sum = 0.0, sum2 = 0.0;
      double avg, std;
      unsigned m;

      for(i = 0; i < n; ++i) {
         sum += cycles[i];
         sum2 += cycles[i]*cycles[i];
      }

      avg = sum/n;
      std = sqrtf((sum2 - n*avg*avg)/n);

      m = 0;
      sum = 0.0;
      for(i = 0; i < n; ++i) {
         if(fabs(cycles[i] - avg) <= 4.0*std) {
            sum += cycles[i];
            ++m;
         }
      }

      cycles_avg = sum/m;

   }

   if(fp)
      write_tsv_row(fp, src_type, dst_type, cycles_avg, success);

   gallivm_destroy(gallivm);

   return success;
}


const struct lp_type conv_types[] = {
   /* float, fixed,  sign,  norm, width, len */

   /* Float */
   {   TRUE, FALSE,  TRUE,  TRUE,    32,   4 },
   {   TRUE, FALSE,  TRUE, FALSE,    32,   4 },
   {   TRUE, FALSE, FALSE,  TRUE,    32,   4 },
   {   TRUE, FALSE, FALSE, FALSE,    32,   4 },

   {   TRUE, FALSE,  TRUE,  TRUE,    32,   8 },
   {   TRUE, FALSE,  TRUE, FALSE,    32,   8 },
   {   TRUE, FALSE, FALSE,  TRUE,    32,   8 },
   {   TRUE, FALSE, FALSE, FALSE,    32,   8 },

   /* Fixed */
   {  FALSE,  TRUE,  TRUE,  TRUE,    32,   4 },
   {  FALSE,  TRUE,  TRUE, FALSE,    32,   4 },
   {  FALSE,  TRUE, FALSE,  TRUE,    32,   4 },
   {  FALSE,  TRUE, FALSE, FALSE,    32,   4 },

   {  FALSE,  TRUE,  TRUE,  TRUE,    32,   8 },
   {  FALSE,  TRUE,  TRUE, FALSE,    32,   8 },
   {  FALSE,  TRUE, FALSE,  TRUE,    32,   8 },
   {  FALSE,  TRUE, FALSE, FALSE,    32,   8 },

   /* Integer */
   {  FALSE, FALSE,  TRUE,  TRUE,    32,   4 },
   {  FALSE, FALSE,  TRUE, FALSE,    32,   4 },
   {  FALSE, FALSE, FALSE,  TRUE,    32,   4 },
   {  FALSE, FALSE, FALSE, FALSE,    32,   4 },

   {  FALSE, FALSE,  TRUE,  TRUE,    32,   8 },
   {  FALSE, FALSE,  TRUE, FALSE,    32,   8 },
   {  FALSE, FALSE, FALSE,  TRUE,    32,   8 },
   {  FALSE, FALSE, FALSE, FALSE,    32,   8 },

   {  FALSE, FALSE,  TRUE,  TRUE,    16,   8 },
   {  FALSE, FALSE,  TRUE, FALSE,    16,   8 },
   {  FALSE, FALSE, FALSE,  TRUE,    16,   8 },
   {  FALSE, FALSE, FALSE, FALSE,    16,   8 },

   {  FALSE, FALSE,  TRUE,  TRUE,     8,  16 },
   {  FALSE, FALSE,  TRUE, FALSE,     8,  16 },
   {  FALSE, FALSE, FALSE,  TRUE,     8,  16 },
   {  FALSE, FALSE, FALSE, FALSE,     8,  16 },

   {  FALSE, FALSE,  TRUE,  TRUE,     8,   4 },
   {  FALSE, FALSE,  TRUE, FALSE,     8,   4 },
   {  FALSE, FALSE, FALSE,  TRUE,     8,   4 },
   {  FALSE, FALSE, FALSE, FALSE,     8,   4 },

   {  FALSE, FALSE,  FALSE,  TRUE,    8,   8 },
};


const unsigned num_types = sizeof(conv_types)/sizeof(conv_types[0]);


boolean
test_all(unsigned verbose, FILE *fp)
{
   const struct lp_type *src_type;
   const struct lp_type *dst_type;
   boolean success = TRUE;
   int error_count = 0;

   for(src_type = conv_types; src_type < &conv_types[num_types]; ++src_type) {
      for(dst_type = conv_types; dst_type < &conv_types[num_types]; ++dst_type) {

         if(src_type == dst_type)
            continue;

         if(!test_one(verbose, fp, *src_type, *dst_type)){
            success = FALSE;
            ++error_count;
         }
      }
   }

   fprintf(stderr, "%d failures\n", error_count);

   return success;
}


boolean
test_some(unsigned verbose, FILE *fp,
          unsigned long n)
{
   const struct lp_type *src_type;
   const struct lp_type *dst_type;
   unsigned long i;
   boolean success = TRUE;

   for(i = 0; i < n; ++i) {
      src_type = &conv_types[rand() % num_types];
      
      do {
         dst_type = &conv_types[rand() % num_types];
      } while (src_type == dst_type || src_type->norm != dst_type->norm);

      if(!test_one(verbose, fp, *src_type, *dst_type))
        success = FALSE;
   }

   return success;
}


boolean
test_single(unsigned verbose, FILE *fp)
{
   /*    float, fixed,  sign,  norm, width, len */
   struct lp_type f32x4_type =
      {   TRUE, FALSE,  TRUE,  TRUE,    32,   4 };
   struct lp_type ub8x4_type =
      {  FALSE, FALSE, FALSE,  TRUE,     8,  16 };

   boolean success;

   success = test_one(verbose, fp, f32x4_type, ub8x4_type);

   return success;
}
