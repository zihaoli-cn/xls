// Copyright 2022 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A simple implementation of quantized CNNs OPS

// - Supported CNNs OPS
//   - conv2d_layer
//   - depthwise_conv2d_layer
//   - average_pool_2d_layer
//   - tensor_add
//   - activation_relu
//   - reshape

// Utilities functions:
// - Array Builders:
//   - make_1d_array_s32
//   - make_2d_array_s32
//   - make_3d_array
//   - make_4d_array
// - Accumulate Operations:
//   - sum_1d
//   - sum_2d
//   - avg_2d
//   - max_1d
//   - max_2d
//   - max_3d
//   - max_4d
//   - min_1d
//   - min_2d
//   - min_3d
//   - min_4d
// - Elementwise Operations:
//   - elementwise_add_2d
//   - elementwise_mult_2d
//   - convert_u8_to_s32_4d
//   - quantize_s32_to_u8_4d
// - Array Operations:
//   - extract_subarray_2d
//   - replace_subarray_2d
//   - add_padding_2d

const S32_MIN = s32:0x80000000;
const S32_MAX = s32:0x7fffffff;

// Create an 1-d array.
// All elements are set to `init`.
fn make_1d_array_s32<N: u32>(init:s32) -> s32[N] {
    s32[N]:[init, ...]
}

fn make_1d_array_u8<N: u32>(init:u8) -> u8[N] {
    u8[N]:[init, ...]
}

// Create a 2-d array.
// All elements are set to `init`.
fn make_2d_array_s32<H: u32, W: u32>(init:s32) -> s32[H][W] {
    s32[H][W]:[make_1d_array_s32<H>(init), ...]
}

fn make_2d_array_u8<H: u32, W: u32>(init:u8) -> u8[H][W] {
    u8[H][W]:[make_1d_array_u8<H>(init), ...]
}

// Create a 3-d array.
// All elements are set to `init`.
fn make_3d_array<H: u32, W: u32, C:u32>(init:s32) -> s32[H][W][C] {
    s32[H][W][C]:[make_2d_array_s32<H, W>(init), ...]
}

// Create a 4-d array.
// All elements are set to `init`.
fn make_4d_array<H: u32, W: u32, C:u32, N:u32>(init:s32) -> s32[H][W][C][N] {
    s32[H][W][C][N]:[make_3d_array<H, W, C>(init), ...]
}

// 2-d elementwise add.
// input: two 2-d arrays with the same shape
// output: 2-d arrays with the same shape
fn elementwise_add_2d<H: u32, W: u32>(array1:s32[H][W], array2:s32[H][W]) -> s32[H][W] {
    // result_1d = array1[w][:] .+ array2[w][:]
    for(w, acc_2d) in range(u32:0, W) {
        let result_1d = for(h, acc_1d) in range(u32:0, H) {
            update(acc_1d, h, array1[w][h] + array2[w][h])
        }(make_1d_array_s32<H>(s32:0));
        
        update(acc_2d, w, result_1d)
    }(make_2d_array_s32<H, W>(s32:0))
}

// 2-d elementwise multiply.
// input: two 2-d arrays with the same shape
// output: 2-d arrays with the same shape
fn elementwise_mult_2d<H: u32, W: u32>(array1:s32[H][W], array2:s32[H][W]) -> s32[H][W] {
    for(w, acc_2d) in range(u32:0, W) {
        // result_1d = array1[w][:] .* array2[w][:]
        let result_1d = for(h, acc_1d) in range(u32:0, H) {
            update(acc_1d, h, array1[w][h] * array2[w][h])
        }(make_1d_array_s32<H>(s32:0));
        
        update(acc_2d, w, result_1d)
    }(make_2d_array_s32<H, W>(s32:0))
}

// Sum all the elements in the 1-d array.
// input: an 1-d array.
// output: the total
fn sum_1d<N: u32>(input1d:s32[N]) -> s32 {
    for(n, acc_1d) in range(u32:0, N) {
        acc_1d + input1d[n]
    }(s32:0)
}

// Sum all the elements in the 2-d array.
// input: an 2-d array.
// output: the total
fn sum_2d<H: u32, W: u32>(input2d:s32[H][W]) -> s32 {
    for(w, acc_2d) in range(u32:0, W) {
        acc_2d + sum_1d<H>(input2d[w])
    }(s32:0)
}

// The average of the all the elements in the 2-d array.
// input: an 2-d array.
// output: the average
fn avg_2d<H: u32, W: u32>(input2d:s32[H][W]) -> s32 {
    sum_2d<H, W>(input2d) / (H * W) as s32
}

fn max_1d<N : u32>(input : s32[N]) -> s32 {
    for (i, maximum_1d) in range(u32:0, N) {
        if (input[i] > maximum_1d) { input[i] } else { maximum_1d }
    }(S32_MIN)
}

fn max_2d<H : u32, W : u32>(input : s32[H][W]) -> s32 {
    for (w, maximum_2d) in range(u32:0, W) {
        let maximum_1d = max_1d<H>(input[w]);
        if (maximum_1d > maximum_2d) { maximum_1d } else { maximum_2d }
    }(S32_MIN)
}

fn max_3d<H : u32, W : u32, C : u32>(input : s32[H][W][C]) -> s32 {
    for (c, maximum_3d) in range(u32:0, C) {
        let maximum_2d = max_2d<H, W>(input[c]);
        if (maximum_2d > maximum_3d) { maximum_2d } else { maximum_3d }
    }(S32_MIN)
}

fn max_4d<H : u32, W : u32, C : u32, N : u32>(input : s32[H][W][C][N]) -> s32 {
    for (n, maximum_4d) in range(u32:0, N) {
        let maximum_3d = max_3d<H, W, C>(input[n]);
        if (maximum_3d > maximum_4d) { maximum_3d } else { maximum_4d }
    }(S32_MIN)
}

fn min_1d<N : u32>(input : s32[N]) -> s32 {
    for (i, minimum_1d) in range(u32:0, N) {
        if (input[i] < minimum_1d) { input[i] } else { minimum_1d }
    }(S32_MAX)
}

fn min_2d<H : u32, W : u32>(input : s32[H][W]) -> s32 {
    for (w, minimum_2d) in range(u32:0, W) {
        let minimum_1d = min_1d<H>(input[w]);
        if (minimum_1d < minimum_2d) { minimum_1d } else { minimum_2d }
    }(S32_MAX)
}

fn min_3d<H : u32, W : u32, C : u32>(input : s32[H][W][C]) -> s32 {
    for (c, minimum_3d) in range(u32:0, C) {
        let minimum_2d = min_2d<H, W>(input[c]);
        if (minimum_2d < minimum_3d) { minimum_2d } else { minimum_3d }
    }(S32_MAX)
}

fn min_4d<H : u32, W : u32, C : u32, N : u32>(input : s32[H][W][C][N]) -> s32 {
    for (n, minimum_4d) in range(u32:0, N) {
        let minimum_3d = min_3d<H, W, C>(input[n]);
        if (minimum_3d < minimum_4d) { minimum_3d } else { minimum_4d }
    }(S32_MAX)
}

fn convert_u8_to_s32_4d<H : u32, W : u32, C : u32, N : u32>(tensor : u8[H][W][C][N]) -> s32[H][W][C][N]{
    for (n, acc_4d) in range(u32:0, N) {
        let elementwise_add_hwc_axis = for (c, acc_3d) in range(u32:0, C) {
            let elementwise_add_hw_axis = for (w, acc_2d) in range(u32:0, W) {
                let elementwise_add_h_axis = for (h, acc_1d) in range(u32:0, H) {
                    update(acc_1d, h, tensor[n][c][w][h] as s32)
                }(make_1d_array_s32<H>(s32:0));

                update(acc_2d, w, elementwise_add_h_axis)
            }(make_2d_array_s32<H, W>(s32:0));
            
            update(acc_3d, c, elementwise_add_hw_axis)
        }(make_3d_array<H, W, C>(s32:0));

        update(acc_4d, n, elementwise_add_hwc_axis)
    }(make_4d_array<H, W, C, N>(s32:0))
}

// (x - maximum)/(maximum - minimum) * 2^8 - 1
fn quantize_s32_to_u8_4d<H : u32, W : u32, C : u32, N : u32>(tensor : s32[H][W][C][N]) -> u8[H][W][C][N] {
    let maximum = max_4d(tensor);
    let minimum = min_4d(tensor);
    
    let range = maximum - minimum;

    for (n, acc_4d) in range(u32:0, N) {
        let elementwise_add_hwc_axis = for (c, acc_3d) in range(u32:0, C) {
            let elementwise_add_hw_axis = for (w, acc_2d) in range(u32:0, W) {
                let elementwise_add_h_axis = for (h, acc_1d) in range(u32:0, H) {
                    let value = tensor[n][c][w][h];
                    let quantized = (((value - minimum) << u8:8) as s32 / range) - s32:1;

                    update(acc_1d, h, quantized as u8)
                }(make_1d_array_s32<H>(s32:0));

                update(acc_2d, w, elementwise_add_h_axis)
            }(make_2d_array_s32<H, W>(s32:0));
            
            update(acc_3d, c, elementwise_add_hw_axis)
        }(make_3d_array<H, W, C>(s32:0));

        update(acc_4d, n, elementwise_add_hwc_axis)
    }(make_4d_array<H, W, C, N>(s32:0))
}

// Extract a subarray from the input 2-d array.
// parameters:
//  - (Hbig, Wbig): input 2-d array's shape
//  - (Hsmall, Wsmall): input 2-d array's shape
// input:
//  - input2d: the input 2-d array
//  - start_h: the starting position in the H-axis
//  - start_w: the starting position in the W-axis
// output: the extracted 2-d array
fn extract_subarray_2d<Hbig: u32, Wbig: u32, Hsmall:u32, Wsmall:u32>
    (input2d:s32[Hbig][Wbig], start_h:u32, start_w:u32) -> s32[Hsmall][Wsmall] {
    for(w, result) in range(u32:0, Wsmall) {
        // h_axis_extracted = input2d[w + start_w][h + start_h : h + start_h + Hsmall]
        let h_axis_extracted = for(h, acc_h_axis) in range(u32:0, Hsmall) {
            update(acc_h_axis, h, input2d[w + start_w][h + start_h])
        }(make_1d_array_s32<Hsmall>(s32:0));

        update(result, w, h_axis_extracted)
    }(make_2d_array_s32<Hsmall, Wsmall>(s32:0))
}

// Replace a subarray within the input 2-d array using an another smaller subarray.
// parameters:
//  - (Hbig, Wbig): input 2-d array's shape
//  - (Hsmall, Wsmall): subarray 2-d array's shape
// input:
//  - input2d: the input 2-d array
//  - start_h: the starting position in the H-axis
//  - start_w: the starting position in the W-axis
//  - subarray: use this subarray to do the replacement
// output: the 2-d array after replacement
fn replace_subarray_2d<Hbig: u32, Wbig: u32, Hsmall: u32, Wsmall: u32>
    (input2d:s32[Hbig][Wbig], start_h:u32, start_w:u32, subarray:s32[Hsmall][Wsmall]) -> s32[Hbig][Wbig] {
    for(w, result) in range(u32:0, Wsmall) {
        let h_axis_replaced = for(h, h_acc) in range(u32:0, Hsmall) {
            update(h_acc, h + start_h, subarray[w][h])
        }(input2d[w]);
        update(result, w + start_w, h_axis_replaced)
    }(input2d)
}

// Add paddings to the 2-d array.
// parameters:
//  - (Hin, Win): input 2-d array's shape
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - (Hout, Wout): output 2-d array's shape
// input:
//  - input2d: the input 2-d array
// output: the 2-d array with paddings
fn add_padding_2d<PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32, 
    Hin: u32, Win: u32, // can be deduced
    Hout : u32 = Hin + PaddingHLeft + PaddingHRight, Wout : u32 = Win + PaddingWUp + PaddingWDown>
    (input2d:s32[Hin][Win]) -> s32[Hout][Wout] {
    replace_subarray_2d<Hout, Wout, Hin, Win>(make_2d_array_s32<Hout, Wout>(s32:0), PaddingHLeft, PaddingWUp, input2d)
}

// Compute Hout or Wout based on its `kernel_size`, `padding`, and `stride`.
// Reference:
// `Shape` part in https://pytorch.org/docs/stable/generated/torch.nn.Conv2d.html
fn compute_demension_size(Hin_or_Win:u32, kernel_size : u32, padding1 : u32, padding2: u32, stride : u32) -> u32 {
    (Hin_or_Win + padding1 + padding2 - kernel_size)/stride + u32:1
}

// A single 2D-convolutional operation.
// parameters:
//  - (StrideH, StrideW): strides of the convolution
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - (Hin, Win): input 2-d array's shape
//  - (KH, KW): kernel's shape
//  - (Hout, Wout): output 2-d array's shape
// Reference: 
//  - "Dive Into Deep Learing" book, section 5.1.1, 
fn conv2d_single<StrideH:u32, StrideW:u32,
    PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32,
    Hin : u32, Win: u32, KH:u32, KW: u32, // these parameters can be deduced
    Hout:u32 = compute_demension_size(Hin, KH, PaddingHLeft, PaddingHRight, StrideH),
    Wout:u32 = compute_demension_size(Win, KW, PaddingWUp, PaddingWDown, StrideW),
    Haddp:u32 = Hin + PaddingHLeft + PaddingHRight,
    Waddp:u32 = Win + PaddingWUp + PaddingWDown>
    (input2d:s32[Hin][Win], kernel:s32[KH][KW]) -> s32[Hout][Wout] {
    
    let input2d : s32[Haddp][Waddp] = add_padding_2d<PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown>(input2d);

    for(w, result) in range(u32:0, Wout) {
        let result_h_axis = for(h, acc_h_axis) in range(u32:0, Hout) {
            let extracted = extract_subarray_2d<Haddp, Waddp, KH, KW>(input2d, h * StrideH, w * StrideW);
            let kernel_mult_result = elementwise_mult_2d<KH, KW>(
                extracted, kernel);

            let element_result = sum_2d<KH, KW>(kernel_mult_result);

            update(acc_h_axis, h, element_result)
        }(make_1d_array_s32<Hout>(s32:0));
        update(result, w, result_h_axis)
    }(make_2d_array_s32<Hout, Wout>(s32:0))
}

// A single 2D-convolutional operation with multiple input channels.
// parameters:
//  - (StrideH, StrideW): strides of the convolution
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - Cin: input channels
//  - (Hin, Win): input 2-d array's shape
//  - (KH, KW): kernel's shape
//  - (Hout, Wout): output 2-d array's shape
fn conv2d_multi_in<StrideH:u32, StrideW:u32,
    PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32,
    Cin:u32, Hin : u32, Win: u32, KH:u32, KW: u32,  // can be duduced
    Hout:u32 = compute_demension_size(Hin, KH, PaddingHLeft, PaddingHRight, StrideH), 
    Wout:u32 = compute_demension_size(Win, KW, PaddingWUp, PaddingWDown, StrideW)>
    (input2d_multi_in:s32[Hin][Win][Cin], kernels_multi_in:s32[KH][KW][Cin], bias : s32)
     -> s32[Hout][Wout] {
    
    for(channel_in, in_channel_acc) in range(u32:0, Cin){
        let in_channel_result : s32[Hout][Wout] = conv2d_single<StrideH, StrideW, PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown>(
            input2d_multi_in[channel_in], 
            kernels_multi_in[channel_in]);
        
        // elementwise-add aross the in-channel dimension
        elementwise_add_2d<Hout, Wout>(in_channel_acc, in_channel_result)
    }(make_2d_array_s32<Hout, Wout>(bias))
}

// A single 2D-convolutional operation with multiple input channels and multiple output channels.
// parameters:
//  - (StrideH, StrideW): strides of the convolution
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - Cin: input channels
//  - Cout: output channels
//  - (Hin, Win): input 2-d array's shape
//  - (KH, KW): kernel's shape
//  - (Hout, Wout): output 2-d array's shape
fn conv2d_multi_in_out<StrideH:u32, StrideW:u32,
    PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32,
    Cin:u32, Cout:u32, Hin : u32, Win: u32, KH:u32, KW: u32,  // can be duduced
    Hout:u32 = compute_demension_size(Hin, KH, PaddingHLeft, PaddingHRight, StrideH), 
    Wout:u32 = compute_demension_size(Win, KW, PaddingWUp, PaddingWDown, StrideW)>
    (input2d_multi_in:s32[Hin][Win][Cin], kernels_multi_in_out:s32[KH][KW][Cin][Cout], bias_multi_out : s32[Cout])
     -> s32[Hout][Wout][Cout] {
    for(channel_out, channel_out_acc) in range(u32:0, Cout){
        update(channel_out_acc, channel_out, 
            conv2d_multi_in<StrideH, StrideW, PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown>(
                input2d_multi_in, 
                kernels_multi_in_out[channel_out],
                bias_multi_out[channel_out]))
    }(make_3d_array<Hout, Wout, Cout>(s32:0))
}

// A 2D-convolutional layer.
// parameters:
//  - (StrideH, StrideW): strides of the convolution
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - N: sample size
//  - Cin: input channels
//  - Cout: output channels
//  - (Hin, Win): input 2-d array's shape
//  - (KH, KW): kernel's shape
//  - (Hout, Wout): output 2-d array's shape
// note:
//  - unsupported configuration:
//    -  `dilation_h_factor`, `dilation_w_factor`
//  - by default:
//    - dilation_h_factor = 1, dilation_w_factor = 1
pub fn conv2d_layer<StrideH:u32, StrideW:u32,
    PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32,
    N:u32, Cin:u32, Cout:u32, Hin : u32, Win: u32, KH:u32, KW: u32,  // can be duduced
    Hout:u32 = compute_demension_size(Hin, KH, PaddingHLeft, PaddingHRight, StrideH), 
    Wout:u32 = compute_demension_size(Win, KW, PaddingWUp, PaddingWDown, StrideW)>
    (input:u8[Hin][Win][Cin][N], filter:u8[KH][KW][Cin][Cout], bias:s32[Cout])
    -> u8[Hout][Wout][Cout][N] {
    
    let input_s32 = convert_u8_to_s32_4d(input);
    let filter_s32 = convert_u8_to_s32_4d(filter);

    let result_s32 = for(n, result_acc) in range(u32:0, N) {
        update(result_acc, n, 
            conv2d_multi_in_out<StrideH, StrideW, PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown>(input_s32[n], filter_s32, bias))
    }(make_4d_array<Hout, Wout, Cout, N>(s32:0));

    quantize_s32_to_u8_4d(result_s32)
}

// A Depthwise 2D-convolutional layer.
// parameters:
//  - (StrideH, StrideW): strides of the convolution
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - N: sample size
//  - Cin: input channels
//  - Cout: output channels
//  - (Hin, Win): input 2-d array's shape
//  - (KH, KW): kernel's shape
//  - (Hout, Wout): output 2-d array's shape
// note:
//  - unsupported configuration:
//    -  `depth_multiplier`, `dilation_h_factor`, `dilation_w_factor`
//  - by default:
//    - depth_multiplier = 1, dilation_h_factor = 1, dilation_w_factor = 1
pub fn depthwise_conv2d_layer<StrideH:u32, StrideW:u32,
    PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32,
    N:u32, Cin:u32, Hin : u32, Win: u32, KH:u32, KW: u32,  // can be duduced
    Hout:u32 = compute_demension_size(Hin, KH, PaddingHLeft, PaddingHRight, StrideH), 
    Wout:u32 = compute_demension_size(Win, KW, PaddingWUp, PaddingWDown, StrideW)>
    (input:s32[Hin][Win][Cin][N], filter:s32[KH][KW][Cin][u32:1], bias:s32[Cin])
    -> s32[Hout][Wout][Cin][N] {
    
    // remove the useless outter demension
    let filter = filter[u32:0];

    for(n, result_acc) in range(u32:0, N) {
        let in_channel_result = for(cin, in_channel_acc) in range(u32:0, Cin) {
            update(in_channel_acc, cin,  
                conv2d_single<StrideH, StrideW, PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown>(
                    input[n][cin], filter[cin]
                ))
        }(make_3d_array<Hout, Wout, Cin>(s32:0));

        update(result_acc, n, in_channel_result)
    }(make_4d_array<Hout, Wout, Cin, N>(s32:0))
}

// Average pooling on a single 2-d array.
// parameters:
//  - (StrideH, StrideW): strides of the convolution
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - (FilterHeight, FilterWidth): filter's shape
//  - (Hin, Win): input 2-d array's shape
//  - (Hout, Wout): output 2-d array's shape
pub fn average_pool_2d_single<StrideH:u32, StrideW:u32,
    PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32, FilterHeight:u32, FilterWidth: u32,
    Hin : u32, Win: u32, // these parameters can be deduced
    Hout:u32 = compute_demension_size(Hin, FilterHeight, PaddingHLeft, PaddingHRight, StrideH),
    Wout:u32 = compute_demension_size(Win, FilterWidth, PaddingWUp, PaddingWDown, StrideW),
    Haddp:u32 = Hin + PaddingHLeft + PaddingHRight,
    Waddp:u32 = Win + PaddingWUp + PaddingWDown>
    (input2d:s32[Hin][Win]) -> s32[Hout][Wout] {
    // add padding to the input array
    let input2d : s32[Haddp][Waddp] = add_padding_2d<PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown>(input2d);

    for(w, result) in range(u32:0, Wout) {
        let result_h_axis = for(h, acc_h_axis) in range(u32:0, Hout) {
            let subarray = extract_subarray_2d<Haddp, Waddp, FilterHeight, FilterWidth>(input2d, h * StrideH, w * StrideW);

            update(acc_h_axis, h, avg_2d<FilterHeight, FilterWidth>(subarray))
        }(make_1d_array_s32<Hout>(s32:0));

        update(result, w, result_h_axis)
    }(make_2d_array_s32<Hout, Wout>(s32:0))
}

// A 2D Average Pooling layer.
// parameters:
//  - (StrideH, StrideW): strides of the convolution
//  - PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown: paddings on different sides
//  - (FilterHeight, FilterWidth): filter's shape
//  - (Hin, Win): input 2-d array's shape
//  - Cin: input channels
//  - N: sample size
//  - (Hout, Wout): output 2-d array's shape
pub fn average_pool_2d_layer<StrideH:u32, StrideW:u32,
    PaddingHLeft: u32, PaddingHRight: u32, PaddingWUp : u32, PaddingWDown : u32, FilterHeight:u32, FilterWidth: u32,
    Hin : u32, Win: u32, Cin:u32, N:u32, // can be duduced
    Hout:u32 = compute_demension_size(Hin, FilterHeight, PaddingHLeft, PaddingHRight, StrideH), 
    Wout:u32 = compute_demension_size(Win, FilterWidth, PaddingWUp, PaddingWDown, StrideW)>
    (input:s32[Hin][Win][Cin][N]) -> s32[Hout][Wout][Cin][N] {
    for(n, acc_4d) in range(u32:0, N) {
        let result_3d = for (cin, acc_3d) in range(u32:0, Cin) {
            let pool_result = average_pool_2d_single<StrideH, StrideW, PaddingHLeft, PaddingHRight, PaddingWUp, PaddingWDown, FilterHeight, FilterWidth>(input[n][cin]);

            update(acc_3d, cin, pool_result)    
        }(make_3d_array<Hout, Wout, Cin>(s32:0));

        update(acc_4d, n, result_3d)
    }(make_4d_array<Hout, Wout, Cin, N>(s32:0))
}

// Tensor Add Operations. It is identical to a 4-d elementwise add operaion.
pub fn tensor_add<H:u32, W:u32, C:u32, N:u32>
    (tensor1:s32[H][W][C][N], tensor2:s32[H][W][C][N]) -> s32[H][W][C][N] {
    for (n, acc_4d) in range(u32:0, N) {
        let elementwise_add_hwc_axis = for (c, acc_3d) in range(u32:0, C) {
            let elementwise_add_hw_axis = for (w, acc_2d) in range(u32:0, W) {
                let elementwise_add_h_axis = for (h, acc_1d) in range(u32:0, H) {
                    update(acc_1d, h, tensor1[n][c][w][h] + tensor2[n][c][w][h])
                }(make_1d_array_s32<H>(s32:0));

                update(acc_2d, w, elementwise_add_h_axis)
            }(make_2d_array_s32<H, W>(s32:0));
            
            update(acc_3d, c, elementwise_add_hw_axis)
        }(make_3d_array<H, W, C>(s32:0));

        update(acc_4d, n, elementwise_add_hwc_axis)
    }(make_4d_array<H, W, C, N>(s32:0))
}

pub fn reshape<C : u32, N : u32>(input : u8[1][1][C][N]) -> u8[C][N] {
    for (n, acc_2d) in range(u32:0, N) {
        let result_1d = for (c, acc_1d) in range(u32:0, C) {
            update(acc_1d, c, input[n][c][1][1])
        }(make_1d_array_u8<C>(u8:0));

        update(acc_2d, n, result_1d)
    }(make_2d_array_u8<C, N>(u8:0))
}

fn relu(input : s32) -> s32 {
    if input < s32:0 { s32:0 } else { input }
}

// Relu Operations.
pub fn activation_relu<H:u32, W:u32, C:u32, N:u32>
    (tensor:s32[H][W][C][N]) -> s32[H][W][C][N] {
    for (n, acc_4d) in range(u32:0, N) {
        let elementwise_add_hwc_axis = for (c, acc_3d) in range(u32:0, C) {
            let elementwise_add_hw_axis = for (w, acc_2d) in range(u32:0, W) {
                let elementwise_add_h_axis = for (h, acc_1d) in range(u32:0, H) {

                    update(acc_1d, h, relu(tensor[n][c][w][h]))
                }(make_1d_array_s32<H>(s32:0));

                update(acc_2d, w, elementwise_add_h_axis)
            }(make_2d_array_s32<H, W>(s32:0));
            
            update(acc_3d, c, elementwise_add_hw_axis)
        }(make_3d_array<H, W, C>(s32:0));

        update(acc_4d, n, elementwise_add_hwc_axis)
    }(make_4d_array<H, W, C, N>(s32:0))
}

fn make_1d_accending_array_for_test<N: u32>(bias:s32) -> s32[N] {
    for(n, acc_1d) in range(u32:0, N) {
        let value = bias + (n as s32);

        update(acc_1d, n, value)
    }(make_1d_array_s32<N>(s32:0))
}

fn make_2d_accending_array_for_test<H: u32, W:u32>(bias:s32) -> s32[H][W] {
    let step = H;
    for(w, acc_2d) in range(u32:0, W) {
        update(acc_2d, w, make_1d_accending_array_for_test<H>(bias + (w * step) as s32))
    }(make_2d_array_s32<H, W>(s32:0))
}

fn make_3d_accending_array_for_test<H: u32, W:u32, C:u32>(bias:s32) -> s32[H][W][C] {
    let step = W * H;
    for(c, acc_3d) in range(u32:0, C) {
        update(acc_3d, c, make_2d_accending_array_for_test<H, W>(bias + (c * step) as s32))
    }(make_3d_array<H, W, C>(s32:0))
}

fn make_4d_accending_array_for_test<H: u32, W:u32, C:u32, N:u32>(bias:s32) -> s32[H][W][C][N] {
    let step = C * W * H;
    for(n, acc_4d) in range(u32:0, N) {
        update(acc_4d, n, make_3d_accending_array_for_test<H, W, C>(bias + (n * step) as s32))
    }(make_4d_array<H, W, C, N>(s32:0))
}

#![test]
fn test_array_builders() {
    let v4 = s32:4;
    let _ = assert_eq(make_1d_array_s32<u32:5>(v4),  s32[5]:[v4, ...]);
    let _ = assert_eq(make_2d_array_s32<u32:5, u32:4>(v4),  s32[5][4]:[s32[5]:[v4, ...], ...]);
    let _ = assert_eq(make_3d_array<u32:5, u32:4, u32:3>(v4),  s32[5][4][3]:[s32[5][4]:[s32[5]:[v4, ...], ...], ...]);
    let _ = assert_eq(make_4d_array<u32:5, u32:4, u32:3, u32:2>(v4),  s32[5][4][3][2]: [s32[5][4][3]:[s32[5][4]:[s32[5]:[v4, ...], ...], ...], ...]);
    ()
}

#![test]
fn test_accumulate_operations() {
    let _ = assert_eq(sum_1d(make_1d_accending_array_for_test<u32:10>(s32:0)), s32:45);
    let _ = assert_eq(sum_2d(make_2d_accending_array_for_test<u32:2, u32:5>(s32:0)), s32:45);
    let _ = assert_eq(avg_2d(make_2d_accending_array_for_test<u32:2, u32:5>(s32:0)), s32:4);

    let _ = assert_eq(max_4d(make_4d_accending_array_for_test<u32:2, u32:3, u32:4, u32:5>(s32:0)), s32:119);
    let _ = assert_eq(min_4d(make_4d_accending_array_for_test<u32:2, u32:3, u32:4, u32:5>(s32:5)), s32:5);
    ()
}

#![test]
fn test_elementwise_operations() {
    let v4 = s32:4;
    let v8 = s32:8;
    let v16 = s32:16;

    let matrix_of_4 = make_2d_array_s32<u32:5, u32:4>(v4);
    let matrix_of_8 = make_2d_array_s32<u32:5, u32:4>(v8);
    let matrix_of_16 = make_2d_array_s32<u32:5, u32:4>(v16);

    let _ = assert_eq(elementwise_add_2d(matrix_of_4, matrix_of_4),  matrix_of_8);
    let _ = assert_eq(elementwise_mult_2d(matrix_of_4, matrix_of_4),  matrix_of_16);

    ()
}


#![test]
fn test_add_padding_2d (){
    let m4x4 : s32[4][4] = make_2d_accending_array_for_test<u32:4, u32:4>(s32:0);
    
    let h_padding_left = u32:2;
    let h_padding_right = u32:3;
    let w_padding_up = u32:1;
    let w_padding_down = u32:4;

    let m : s32[9][9] = add_padding_2d<h_padding_left, h_padding_right, w_padding_up, w_padding_down>(m4x4);

    let _ = trace!(m4x4);
    let _ = trace!(m);

    let _ = for (w, _) in range(u32:0, u32:4) {
        for (h, _) in range(u32:0, u32:4){
            assert_eq(m4x4[w][h], m[w + w_padding_up][h + h_padding_left])
        }(())
    }(());

    ()
}

#![test]
fn test_array_operations() {
    let m4x4 : s32[4][4] = make_2d_accending_array_for_test<u32:4, u32:4>(s32:0);

    let h_padding = u32:1;
    let w_padding = u32:2;

    let m6x8 : s32[6][8] = add_padding_2d<h_padding, h_padding, w_padding, w_padding>(m4x4);

    let _ = for(h, _) in range(u32:0, u32:4){
        for(w, _) in range(u32:0, u32:4){
            assert_eq(m4x4[w][h], m6x8[w + w_padding][h + h_padding])
        }(())
    }(());

    let _ = assert_eq(extract_subarray_2d<u32:6, u32:8, u32:4, u32:4>(m6x8, h_padding, w_padding), m4x4);

    let m6x8_replace : s32[6][8] = replace_subarray_2d<u32:6, u32:8, u32:4, u32:4>(make_2d_array_s32<u32:6, u32:8>(s32:0), h_padding, w_padding, m4x4);
    let _ = assert_eq(m6x8_replace, m6x8);

    ()
}

#![test]
fn test_tensor_add() {
    let v5 = s32:5;

    let accending_from_0 = make_4d_accending_array_for_test<u32:5, u32:4, u32:3, u32:2>(s32:0);
    let tensor_of_5 = make_4d_array<u32:5, u32:4, u32:3, u32:2>(v5);
    let accending_from_5 = make_4d_accending_array_for_test<u32:5, u32:4, u32:3, u32:2>(s32:5);

    let _ =  assert_eq(tensor_add(accending_from_0, tensor_of_5), accending_from_5);

    ()
}

#![test]
fn test_average_pool_2d_layer() {
    let array3d = for (idx, acc_3d) in range(u32:0, u32:4) {
        update(acc_3d, idx, make_2d_accending_array_for_test<u32:5, u32:3>(idx as s32))
    } (make_3d_array<u32:5, u32:3, u32:4>(s32:0));

    let array4d : s32[5][3][4][2] = [array3d, array3d];

    let StrideH = u32:2;
    let StrideW = u32:2;
    let PaddingH = u32:1;
    let PaddingW = u32:2;
    let FilterHeight = u32:2;
    let FilterWidth = u32:2;
    let result = average_pool_2d_layer<StrideH, StrideW, PaddingH, PaddingH, PaddingW, PaddingW, FilterHeight, FilterWidth>(array4d);

    let expected_result_3d = [
        [
            [s32:0, s32:0, s32:0], 
            [s32:1, s32:4, s32:6], 
            [s32:2, s32:5, s32:6]
        ],
        [
            [s32:0, s32:0, s32:0], 
            [s32:1, s32:5, s32:7], 
            [s32:2, s32:6, s32:7]
        ],
        [
            [s32:0, s32:0, s32:0], 
            [s32:2, s32:6, s32:8], 
            [s32:3, s32:6, s32:7]
        ],
        [
            [s32:0, s32:0, s32:0], 
            [s32:2, s32:7, s32:9], 
            [s32:3, s32:7, s32:8]
        ]
    ];

    
    let _ = assert_eq(result, [expected_result_3d, expected_result_3d]);

    ()
}

#![test]
fn test_conv2d_single() {
    let m6x8 = make_2d_accending_array_for_test<u32:6, u32:8>(s32:0);

    let kernel1 = s32[3][3]:[[s32:1, s32:0, s32:-1], [s32:-1, s32:0, s32:1], [s32:-1, s32:1, s32:0]];
    let kernel2 = s32[3][3]:[[s32:1, s32:0, s32:-1], [s32:-1, s32:0, s32:1], [s32:-1, s32:0, s32:1]];

    // StrideH = StrideW = 1
    let result1 : s32[4][6] = conv2d_single<u32:1, u32:1, u32:0, u32:0, u32:0, u32:0>(m6x8, kernel1);
    let result2 : s32[4][6] = conv2d_single<u32:1, u32:1, u32:0, u32:0, u32:0, u32:0>(m6x8, kernel2);

    let expected_result1 = make_2d_array_s32<u32:4, u32:6>(s32:1);
    let expected_result2 = make_2d_array_s32<u32:4, u32:6>(s32:2);

    let _ = assert_eq(expected_result1, result1);
    let _ = assert_eq(expected_result2, result2);

    // StrideH = StrideW = 2
    let result3 : s32[2][3] = conv2d_single<u32:2, u32:2, u32:0, u32:0, u32:0, u32:0>(m6x8, kernel1);
    let result4 : s32[2][3] = conv2d_single<u32:2, u32:2, u32:0, u32:0, u32:0, u32:0>(m6x8, kernel2);

    let expected_result3 = make_2d_array_s32<u32:2, u32:3>(s32:1);
    let expected_result4 = make_2d_array_s32<u32:2, u32:3>(s32:2);

    let _ = assert_eq(expected_result3, result3);
    let _ = assert_eq(expected_result4, result4);

    // StrideH = StrideW = 2, PaddingH = 2, PaddingW = 1
    let result5 = conv2d_single<u32:2, u32:2, u32:2, u32:2, u32:1, u32:1>(m6x8, kernel2);
    let expected_result5 = [
        [s32:6, s32:4, s32:4, s32:-14],
        [s32:24, s32:2, s32:2, s32:-28],
        [s32:36, s32:2, s32:2, s32:-40],
        [s32:48, s32:2, s32:2, s32:-52],
    ];

    let _ = assert_eq(result5, expected_result5);

    ()
}

#![test]
fn test_conv2d_multi_in(){
    // this test case come from "Dive Into Deep Learing" book, section 5.3.1, figure 5-4
    let m3x3 = make_2d_accending_array_for_test<u32:3, u32:3>(s32:0);
    let m3x3_plus1 = make_2d_accending_array_for_test<u32:3, u32:3>(s32:1);

    let kernel2x2 = make_2d_accending_array_for_test<u32:2, u32:2>(s32:0);
    let kernel2x2_plus1 = make_2d_accending_array_for_test<u32:2, u32:2>(s32:1);

    let input = [m3x3, m3x3_plus1];
    let kernels = [kernel2x2, kernel2x2_plus1];
    let bias = s32:0;
    
    let StrideH = u32:1;
    let StrideW = u32:1;
    let PaddingH = u32:0;
    let PaddingW = u32:0;
    let result = conv2d_multi_in<StrideH, StrideW, PaddingH, PaddingH, PaddingW, PaddingW>(input, kernels, bias);

    let expected_result = [[s32:56, s32:72], [s32:104, s32:120]];

    let _ = assert_eq(result, expected_result);

    ()
}

fn elementwise_add_constant_3d_for_test<H: u32, W: u32, C:u32>(input_array:s32[H][W][C], constant:s32) -> s32[H][W][C] {
    for(c, acc_3d) in range(u32:0, C) {
        let result_2d = for(w, acc_2d) in range(u32:0, W) {
            let result_1d = for(h, acc_1d) in range(u32:0, H) {
                update(acc_1d, h, input_array[c][w][h] + constant)
            }(make_1d_array_s32<H>(s32:0));
            
            update(acc_2d, w, result_1d)
        }(make_2d_array_s32<H, W>(s32:0));
        
        update(acc_3d, c, result_2d)
    }(make_3d_array<H, W, C>(s32:0))
}

#![test]
fn test_conv2d_multi_in_out(){
    // this test case come from "Dive Into Deep Learing" book, section 5.3.2
    let m3x3 = make_2d_accending_array_for_test<u32:3, u32:3>(s32:0);
    let m3x3_plus1 = make_2d_accending_array_for_test<u32:3, u32:3>(s32:1);

    // 3 out-channels, 2 input-channel
    let StrideH = u32:1;
    let StrideW = u32:1;
    let PaddingH = u32:0;
    let PaddingW = u32:0;

    let kernel2x2 = make_2d_accending_array_for_test<u32:2, u32:2>(s32:0);
    let kernel2x2_plus1 = make_2d_accending_array_for_test<u32:2, u32:2>(s32:1);

    let kernels_multi_in : s32[2][2][2] = [kernel2x2, kernel2x2_plus1];

    let kernels_multi_in_out : s32[2][2][2][3] = [
        kernels_multi_in,
        elementwise_add_constant_3d_for_test(kernels_multi_in, s32:1),
        elementwise_add_constant_3d_for_test(kernels_multi_in, s32:2)
        ];
    
    let input : s32[3][3][2] = [m3x3, m3x3_plus1];
    let bias = [s32:0, s32:0, s32:0];
    
    let result = conv2d_multi_in_out<StrideH, StrideW, PaddingH, PaddingH, PaddingW, PaddingW>(input, kernels_multi_in_out, bias);

    let expected_result = [
        [
            [s32:56, s32:72],
            [s32:104, s32:120]
        ], 
        [
            [s32:76, s32:100],
            [s32:148, s32:172]
        ],
        [
            [s32:96, s32:128],
            [s32:192, s32:224]
        ]];

    let _ = assert_eq(result, expected_result);

    let _ = trace!(s32:0x80000000);

    ()
}