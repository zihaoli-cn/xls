// MachSuite (BSD-3) License
// 
// Copyright (c) 2014-2015, the President and Fellows of Harvard College.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// 
// * Neither the name of Harvard University nor the names of its contributors may
//   be used to endorse or promote products derived from this software without
//   specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Matrix Multiplication.
// Based on:
// https://github.com/breagen/MachSuite/blob/master/fft/strided/fft.c

import float32
import std
import xls.modules.fpadd_2x32
import xls.modules.fpmul_2x32

type F32 = float32::F32;

// Fixed point multiplication of square matrices with 
// dimensions MATRIX_DIM x MATRIX_DIM.
pub fn gemm_fixed<BITS: u32, MATRIX_DIM: u32, MATRIX_ELEMENTS: u32 = MATRIX_DIM * MATRIX_DIM>(
  m1: sN[BITS][MATRIX_ELEMENTS], m2: sN[BITS][MATRIX_ELEMENTS]) 
  -> sN[BITS][MATRIX_ELEMENTS]{

  for(i, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) in range (u32:0, MATRIX_DIM) {
    for(j, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) in range (u32:0, MATRIX_DIM) {
      let i_col = i * MATRIX_DIM;

      let sum = for(k, sum): (u32, sN[BITS]) in range (u32:0, MATRIX_DIM) {
        let k_col = k * MATRIX_DIM;
        let mult = m1[i_col + k] * m2[k_col + j];
        mult + sum
      } (sN[BITS]:0);

      update(prod, i_col+j, sum)

    } (prod)
  } (sN[BITS][MATRIX_ELEMENTS]:[sN[BITS]:0, ...])
}

// Fixed point multiplication of square matrices with 
// dimensions MATRIX_DIM x MATRIX_DIM.  This algorithm
// is a tiled algorithm with tile dimensions BLOCK_DIM x BLOCK_DIM
pub fn bbgemm_fixed<BITS: u32, MATRIX_DIM: u32, BLOCK_DIM:u32, 
  MATRIX_ELEMENTS: u32 = MATRIX_DIM * MATRIX_DIM, 
  BLOCK_ITERATIONS: u32 = MATRIX_DIM / BLOCK_DIM>(
  m1: sN[BITS][MATRIX_ELEMENTS], m2: sN[BITS][MATRIX_ELEMENTS]) 
  -> sN[BITS][MATRIX_ELEMENTS]{

  // Iterate over different blocks.
  for(jj, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
    in range (u32:0, BLOCK_ITERATIONS) {

    let jj = jj*BLOCK_DIM;
    for(kk, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
      in range (u32:0, BLOCK_ITERATIONS) {

      let kk = kk*BLOCK_DIM;
      for(i, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
        in range (u32:0, MATRIX_DIM) {

        let i_row = i * MATRIX_DIM;
        // Accumulate partial products within block.
        for(k, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
          in range (u32:0, BLOCK_DIM) {

          let k_row = (k + kk) * MATRIX_DIM;
          let temp_x = m1[i_row + k + kk];
          for(j, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
            in range (u32:0, BLOCK_DIM) {

            let mult = temp_x * m2[k_row + j + jj];
            let prod_idx = i_row + j + jj;
            let partial_product = prod[prod_idx];
            let partial_product = partial_product + mult;
            update(prod, prod_idx, partial_product)

          } (prod) // j
        } (prod) // k

      } (prod) // i
    } (prod) // kk
  } (sN[BITS][MATRIX_ELEMENTS]:[sN[BITS]:0, ...]) // jj
}

// Fixed point multiplication of square matrices with 
// dimensions MATRIX_DIM x MATRIX_DIM.  This algorithm
// is a tiled algorithm with tile dimensions BLOCK_DIM x BLOCK_DIM.
// The order of the inner loops has been changed compared to bbgemm_fixed
// such that we accumulate a sum of BLOCK_DIM partial products in the inner
// loop before adding this sum to the output matrix.
pub fn bbgemm_acc_fixed<BITS: u32, MATRIX_DIM: u32, BLOCK_DIM:u32, 
  MATRIX_ELEMENTS: u32 = MATRIX_DIM * MATRIX_DIM, 
  BLOCK_ITERATIONS: u32 = MATRIX_DIM / BLOCK_DIM>(
  m1: sN[BITS][MATRIX_ELEMENTS], m2: sN[BITS][MATRIX_ELEMENTS]) 
  -> sN[BITS][MATRIX_ELEMENTS]{

  // Iterate over different blocks.
  for(jj, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
    in range (u32:0, BLOCK_ITERATIONS) {

    let jj = jj*BLOCK_DIM;
    for(kk, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
      in range (u32:0, BLOCK_ITERATIONS) {

      let kk = kk*BLOCK_DIM;
      for(i, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
        in range (u32:0, MATRIX_DIM) {

        let i_row = i * MATRIX_DIM;
        // Accumulate partial products within block.
        for(j, prod): (u32, sN[BITS][MATRIX_ELEMENTS]) 
          in range (u32:0, BLOCK_DIM) {

          // Sum across BLOCK_DIM partial products.
          let sum = for(k, sum): (u32, sN[BITS]) 
            in range (u32:0, BLOCK_DIM) {

            let k_row = (k + kk) * MATRIX_DIM;
            let mult = m1[i_row + k + kk] * m2[k_row + j + jj];
            sum + mult
          } (sN[BITS]:0); // k

          let prod_idx = i_row + j + jj;
          let partial_product = prod[prod_idx];
          let partial_product = partial_product + sum;
          update(prod, prod_idx, partial_product)

        } (prod) // j

      } (prod) // i
    } (prod) // kk
  } (sN[BITS][MATRIX_ELEMENTS]:[sN[BITS]:0, ...]) // jj
}

// Floating point multiplication of square matrices with 
// dimensions MATRIX_DIM x MATRIX_DIM.
pub fn gemm_float32<MATRIX_DIM: u32, MATRIX_ELEMENTS: u32 = MATRIX_DIM * MATRIX_DIM>(
  m1: F32[MATRIX_ELEMENTS], m2: F32[MATRIX_ELEMENTS]) -> F32[MATRIX_ELEMENTS]{

  for(i, prod): (u32, F32[MATRIX_ELEMENTS]) in range (u32:0, MATRIX_DIM) {
    for(j, prod): (u32, F32[MATRIX_ELEMENTS]) in range (u32:0, MATRIX_DIM) {
      let i_col = i * MATRIX_DIM;

      let sum = for(k, sum): (u32, F32) in range (u32:0, MATRIX_DIM) {
        let k_col = k * MATRIX_DIM;
        let mult = fpmul_2x32::fpmul_2x32(
          m1[i_col + k], 
          m2[k_col + j]);
        fpadd_2x32::fpadd_2x32(mult, sum)
      } (float32::zero(u1:0));

      update(prod, i_col+j, sum)

    } (prod)
  } (F32[MATRIX_ELEMENTS]:[float32::zero(u1:0), ...])
}

// Floating point multiplication of square matrices with 
// dimensions MATRIX_DIM x MATRIX_DIM.  This algorithm
// is a tiled algorithm with tile dimensions BLOCK_DIM x BLOCK_DIM
pub fn bbgemm_float32<MATRIX_DIM: u32, BLOCK_DIM:u32, 
  MATRIX_ELEMENTS: u32 = MATRIX_DIM * MATRIX_DIM, 
  BLOCK_ITERATIONS: u32 = MATRIX_DIM / BLOCK_DIM>(
  m1: F32[MATRIX_ELEMENTS], m2: F32[MATRIX_ELEMENTS]) 
  -> F32[MATRIX_ELEMENTS]{

  // Iterate over different blocks.
  for(jj, prod): (u32, F32[MATRIX_ELEMENTS]) 
    in range (u32:0, BLOCK_ITERATIONS) {

    let jj = jj*BLOCK_DIM;
    for(kk, prod): (u32, F32[MATRIX_ELEMENTS]) 
      in range (u32:0, BLOCK_ITERATIONS) {

      let kk = kk*BLOCK_DIM;
      for(i, prod): (u32, F32[MATRIX_ELEMENTS]) 
        in range (u32:0, MATRIX_DIM) {

        let i_row = i * MATRIX_DIM;
        // Accumulate partial products within block.
        for(k, prod): (u32, F32[MATRIX_ELEMENTS]) 
          in range (u32:0, BLOCK_DIM) {

          let k_row = (k + kk) * MATRIX_DIM;
          let temp_x = m1[i_row + k + kk];
          for(j, prod): (u32, F32[MATRIX_ELEMENTS]) 
            in range (u32:0, BLOCK_DIM) {

            let mult = fpmul_2x32::fpmul_2x32(
              temp_x, 
              m2[k_row + j + jj]);
            let prod_idx = i_row + j + jj;
            let partial_product = prod[prod_idx];
            let partial_product = fpadd_2x32::fpadd_2x32(
              partial_product, 
              mult);
            update(prod, prod_idx, partial_product)

          } (prod) // j
        } (prod) // k

      } (prod) // i
    } (prod) // kk
  } (F32[MATRIX_ELEMENTS]:[float32::zero(u1:0), ...]) // jj
}

// Floating point multiplication of square matrices with 
// dimensions MATRIX_DIM x MATRIX_DIM.  This algorithm
// is a tiled algorithm with tile dimensions BLOCK_DIM x BLOCK_DIM.
// The order of the inner loops has been changed compared to bbgemm_float32
// such that we accumulate a sum of BLOCK_DIM partial products in the inner
// loop before adding this sum to the output matrix.
pub fn bbgemm_acc_float32<MATRIX_DIM: u32, BLOCK_DIM:u32, 
  MATRIX_ELEMENTS: u32 = MATRIX_DIM * MATRIX_DIM,
  BLOCK_ITERATIONS: u32 = MATRIX_DIM / BLOCK_DIM>(
  m1: F32[MATRIX_ELEMENTS], m2: F32[MATRIX_ELEMENTS]) 
  -> F32[MATRIX_ELEMENTS]{

  // Iterate over different blocks.
  for(jj, prod): (u32, F32[MATRIX_ELEMENTS]) 
    in range (u32:0, BLOCK_ITERATIONS) {

    let jj = jj*BLOCK_DIM;
    for(kk, prod): (u32, F32[MATRIX_ELEMENTS]) 
      in range (u32:0, BLOCK_ITERATIONS) {

      let kk = kk*BLOCK_DIM;
      for(i, prod): (u32, F32[MATRIX_ELEMENTS]) 
        in range (u32:0, MATRIX_DIM) {

        let i_row = i * MATRIX_DIM;
        // Accumulate partial products within block.
        for(j, prod): (u32, F32[MATRIX_ELEMENTS]) 
          in range (u32:0, BLOCK_DIM) {

          // Sum across BLOCK_DIM partial products.
          let sum = for(k, sum): (u32, F32) 
            in range (u32:0, BLOCK_DIM) {

            let k_row = (k + kk) * MATRIX_DIM;
            let mult = fpmul_2x32::fpmul_2x32(
              m1[i_row + k + kk], 
              m2[k_row + j + jj]);
            fpadd_2x32::fpadd_2x32(sum, mult)
          } (float32::zero(u1:0)); // k

          let prod_idx = i_row + j + jj;
          let partial_product = prod[prod_idx];
          let partial_product = fpadd_2x32::fpadd_2x32(
            partial_product, 
            sum);
          update(prod, prod_idx, partial_product)

        } (prod) // j

      } (prod) // i
    } (prod) // kk
  } (F32[MATRIX_ELEMENTS]:[float32::zero(u1:0), ...]) // jj
}

// Arrays for testing.
pub const A_MATRIX_FIXED_4_4 = s32[16]:[
  s32:0,
  s32:1,
  s32:2,
  s32:3,
  s32:4,
  s32:5,
  s32:6,
  s32:7,
  s32:8,
  s32:9,
  s32:10,
  s32:11,
  s32:12,
  s32:13,
  s32:14,
  s32:15
];
pub const B_MATRIX_FIXED_4_4 = s32[16]:[
  s32:-16,
  s32:17,
  s32:-18,
  s32:19,
  s32:20,
  s32:-21,
  s32:22,
  s32:-23,
  s32:-24,
  s32:25,
  s32:-26,
  s32:27,
  s32:28,
  s32:-29,
  s32:30,
  s32:-31
];
pub const RESULT_MATRIX_FIXED_4_4 = s32[16]:[
  s32:56,
  s32:-58,
  s32:60,
  s32:-62,
  s32:88,
  s32:-90,
  s32:92,
  s32:-94,
  s32:120,
  s32:-122,
  s32:124,
  s32:-126,
  s32:152,
  s32:-154,
  s32:156,
  s32:-158
];

#![test]
fn test_gemm_fixed() {
  let result_observed = gemm_fixed<u32:32, u32:4>(A_MATRIX_FIXED_4_4, 
                                    B_MATRIX_FIXED_4_4);
  let _ = assert_eq(result_observed, RESULT_MATRIX_FIXED_4_4);
  ()
}

#![test]
fn test_bbgemm_fixed() {
  let result_observed = bbgemm_fixed<u32:32, u32:4, u32:2>(A_MATRIX_FIXED_4_4, 
                                    B_MATRIX_FIXED_4_4);
  let _ = assert_eq(result_observed, RESULT_MATRIX_FIXED_4_4);
  ()
}

#![test]
fn test_bbgemm_acc_fixed() {
  let result_observed = bbgemm_acc_fixed<u32:32, u32:4, u32:2>(A_MATRIX_FIXED_4_4, 
                                    B_MATRIX_FIXED_4_4);
  let _ = assert_eq(result_observed, RESULT_MATRIX_FIXED_4_4);
  ()
}


// Helper function for testing. Returns the first 200
// ints >= 0 (0->99) as floats.
fn get_first_200_ints_as_float() -> F32[200] {
  let one = float32::one(u1:0);
  for(idx, acc) : (u32, F32[200]) in range(u32:0, u32:199) {
    let next_num = fpadd_2x32::fpadd_2x32(acc[idx], one);
    update(acc, idx+u32:1, next_num)
  } (F32[200]:[float32::zero(u1:0), ...])
}

// Helper function for testing. Negates a F32 number.
fn negate(input: F32) -> F32 {
  let neg_one = float32::one(u1:1);
  fpmul_2x32::fpmul_2x32(input, neg_one)
}

// Helper function for testing. Returns the first 200
// ints <=0 (0->-99) as floats.
fn get_first_200_negative_ints_as_float() -> F32[200] {
  let fp32_200_ints = get_first_200_ints_as_float();
  map(fp32_200_ints, negate)
}

// Helper function for testing. Converts an array ints
// to an array of floats. Only ints with absolute value
// < 200 supported.
fn convert_fixed_array_to_float<BITS: u32, ENTRIES: u32>
  (input: sN[BITS][ENTRIES]) -> F32[ENTRIES] {

  // Grab floats.
  let fp32_200_ints = get_first_200_ints_as_float();
  let fp32_200_neg_ints = get_first_200_negative_ints_as_float();

  // Could be probably do this as a map,
  // but since this only for testing I prefer
  // simplicity of efficiency.
  for(idx, fp_array): (u32, F32[ENTRIES]) 
    in range(u32:0, ENTRIES) {
    let fixed_value = input[idx];
    let is_postive = fixed_value >= sN[BITS]:0;
    let abs_value = fixed_value if is_postive else 
                    -fixed_value;
    let float_value = fp32_200_ints[abs_value] if is_postive
                      else fp32_200_neg_ints[abs_value];
    update(fp_array, idx, float_value) 
  } (F32[ENTRIES]:[float32::zero(u1:0), ...])
}

#![test]
fn test_gemm_float32() {
  let a_matrix = 
    convert_fixed_array_to_float(A_MATRIX_FIXED_4_4);
  let b_matrix = 
    convert_fixed_array_to_float(B_MATRIX_FIXED_4_4);
  let result_expected = 
    convert_fixed_array_to_float(RESULT_MATRIX_FIXED_4_4);

  let result_observed = gemm_float32<u32:4>(a_matrix, 
                                    b_matrix);
  let _ = assert_eq(result_observed, result_expected);
  ()
}

#![test]
fn test_bbgemm_float32() {
  let a_matrix = 
    convert_fixed_array_to_float(A_MATRIX_FIXED_4_4);
  let b_matrix = 
    convert_fixed_array_to_float(B_MATRIX_FIXED_4_4);
  let result_expected = 
    convert_fixed_array_to_float(RESULT_MATRIX_FIXED_4_4);

  let result_observed = bbgemm_float32<u32:4, u32:2>(a_matrix, 
                                    b_matrix);
  let _ = assert_eq(result_observed, result_expected);
  ()
}

#![test]
fn test_bbgemm_acc_float32() {
  let a_matrix = 
    convert_fixed_array_to_float(A_MATRIX_FIXED_4_4);
  let b_matrix = 
    convert_fixed_array_to_float(B_MATRIX_FIXED_4_4);
  let result_expected = 
    convert_fixed_array_to_float(RESULT_MATRIX_FIXED_4_4);

  let result_observed = bbgemm_acc_float32<u32:4, u32:2>(a_matrix, 
                                    b_matrix);
  let _ = assert_eq(result_observed, result_expected);
  ()
}
