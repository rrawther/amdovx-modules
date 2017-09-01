/*
Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "kernels.h"

struct FullyConnectedLayerLocalData {
    NeuralNetworkCommonHandle * handle;
    miopenConvolutionDescriptor_t convdesc;
    miopenTensorDescriptor_t input_desc;
    miopenTensorDescriptor_t output_desc;
    miopenTensorDescriptor_t weight_desc;
    miopenTensorDescriptor_t bias_desc;
    cl_mem input_mem;
    cl_mem output_mem;
    cl_mem weight_mem;
    cl_mem bias_mem;
    miopenConvFwdAlgorithm_t algo;
    size_t workspace_size;
    double alpha;
    double beta;
    cl_mem workspace;
};

static vx_status VX_CALLBACK validateFullyConnectedLayer(vx_node node, const vx_reference parameters[], vx_uint32 num, vx_meta_format metas[])
{
    // check scalar type
    vx_enum type;
    ERROR_CHECK_STATUS(vxQueryScalar((vx_scalar)parameters[3], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_ENUM) return VX_ERROR_INVALID_TYPE;
    ERROR_CHECK_STATUS(vxQueryScalar((vx_scalar)parameters[4], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_ENUM) return VX_ERROR_INVALID_TYPE;

    // check tensor dimensions
    vx_size num_dims;
    vx_size input_dims[4], weights_dims[4], output_dims[4];
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    if(num_dims != 4) return VX_ERROR_INVALID_DIMENSION;
    if(type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_DIMS, input_dims, sizeof(input_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    if(num_dims != 4) return VX_ERROR_INVALID_DIMENSION;
    if(type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_DIMS, weights_dims, sizeof(weights_dims)));
    if(parameters[2]) {
        ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
        ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
        if(num_dims != 1) return VX_ERROR_INVALID_DIMENSION;
        if(type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
        vx_size bias_dims[1];
        ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_DIMS, bias_dims, sizeof(bias_dims)));
        if(bias_dims[0] != weights_dims[3]) return VX_ERROR_INVALID_DIMENSION;
    }
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[5], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[5], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    if(num_dims != 4) return VX_ERROR_INVALID_DIMENSION;
    if(type != VX_TYPE_FLOAT32) return VX_ERROR_INVALID_TYPE;
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[5], VX_TENSOR_DIMS, output_dims, sizeof(output_dims)));
    if(output_dims[3] != input_dims[3]) return VX_ERROR_INVALID_DIMENSION;
    if(input_dims[2] != weights_dims[2]) return VX_ERROR_INVALID_DIMENSION;
    if(output_dims[2] != weights_dims[3]) return VX_ERROR_INVALID_DIMENSION;

    // output tensor configuration
    type = VX_TYPE_FLOAT32;
    num_dims = 4;
    ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(metas[5], VX_TENSOR_DATA_TYPE, &type, sizeof(type)));
    ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(metas[5], VX_TENSOR_NUMBER_OF_DIMS, &num_dims, sizeof(num_dims)));
    ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(metas[5], VX_TENSOR_DIMS, output_dims, sizeof(output_dims)));
    return VX_SUCCESS;
}

static vx_status VX_CALLBACK processFullyConnectedLayer(vx_node node, const vx_reference * parameters, vx_uint32 num)
{
    FullyConnectedLayerLocalData * data = NULL;
    ERROR_CHECK_STATUS(vxQueryNode(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));
    miopenHandle_t miopen_handle = data->handle->miopen_handle;

    //ConvolutionForward.
    ERROR_CHECK_MIOPEN_STATUS(miopenConvolutionForward(data->handle->miopen_handle, &data->alpha, data->input_desc, data->input_mem,
                                                       data->weight_desc, data->weight_mem, data->convdesc, data->algo, &data->beta, data->output_desc, data->output_mem, data->workspace, data->workspace_size));

    //Convolution Forward Bias.
	if(parameters[2]) {
	    ERROR_CHECK_MIOPEN_STATUS(miopenConvolutionForwardBias(data->handle->miopen_handle, &data->alpha, data->bias_desc, data->bias_mem,
                                                           &data->beta, data->output_desc, data->output_mem));
	}
    
    return VX_SUCCESS;
}

static vx_status VX_CALLBACK initializeFullyConnectedLayer(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    FullyConnectedLayerLocalData * data = new FullyConnectedLayerLocalData;
    memset(data, 0, sizeof(*data));
    ERROR_CHECK_STATUS(createGraphHandle(node, &data->handle));

    //input,weight,bias,output descriptors.
    miopenConvolutionMode_t mode = miopenConvolution;
    vx_size input_dims[4], weights_dims[4], output_dims[4], bias_dims[1];;
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_DIMS, input_dims, sizeof(input_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_DIMS, weights_dims, sizeof(weights_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[5], VX_TENSOR_DIMS, output_dims, sizeof(output_dims)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_DIMS, bias_dims, sizeof(bias_dims)));

    ERROR_CHECK_MIOPEN_STATUS(miopenCreateTensorDescriptor(&data->input_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenCreateTensorDescriptor(&data->weight_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenCreateTensorDescriptor(&data->output_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenCreateTensorDescriptor(&data->bias_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenSet4dTensorDescriptor(data->input_desc, miopenFloat, input_dims[3], input_dims[2], input_dims[1], input_dims[0]));
    ERROR_CHECK_MIOPEN_STATUS(miopenSet4dTensorDescriptor(data->output_desc, miopenFloat, output_dims[3], output_dims[2], output_dims[1], output_dims[0]));
    ERROR_CHECK_MIOPEN_STATUS(miopenSet4dTensorDescriptor(data->weight_desc, miopenFloat, weights_dims[3], weights_dims[2], weights_dims[1], weights_dims[0]));
    ERROR_CHECK_MIOPEN_STATUS(miopenSet4dTensorDescriptor(data->bias_desc, miopenFloat,1, bias_dims[0], 1, 1));

    //fully connected to convolution conversion.
    ERROR_CHECK_MIOPEN_STATUS(miopenCreateConvolutionDescriptor(&data->convdesc));
    ERROR_CHECK_MIOPEN_STATUS(miopenInitConvolutionDescriptor(data->convdesc, mode, 0, 0, 1, 1, 1, 1));

    //Memory Declaration.
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[0], VX_TENSOR_BUFFER_OPENCL, &data->input_mem, sizeof(data->input_mem)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[5], VX_TENSOR_BUFFER_OPENCL, &data->output_mem, sizeof(data->output_mem)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[1], VX_TENSOR_BUFFER_OPENCL, &data->weight_mem, sizeof(data->weight_mem)));
    ERROR_CHECK_STATUS(vxQueryTensor((vx_tensor)parameters[2], VX_TENSOR_BUFFER_OPENCL, &data->bias_mem, sizeof(data->bias_mem)));

    //Workspace Size.
    ERROR_CHECK_MIOPEN_STATUS(miopenConvolutionForwardGetWorkSpaceSize(data->handle->miopen_handle, data->weight_desc, data->input_desc, data->convdesc, data->output_desc, &data->workspace_size));
    if (data->workspace_size > 0) {
        vx_context   vxContext = vxGetContext((vx_reference)node);
        cl_context context;
        ERROR_CHECK_STATUS(vxQueryContext(vxContext, VX_CONTEXT_ATTRIBUTE_AMD_OPENCL_CONTEXT, &context, sizeof(context)));
        data->workspace_size = (data->workspace_size + 3) & ~3;
        data->workspace = clCreateBuffer(context, CL_MEM_READ_WRITE, data->workspace_size, NULL, NULL);
        if (!data->workspace) {
            return VX_FAILURE;
        }
        cl_float pattern= 0;
        cl_int err = clEnqueueFillBuffer(data->handle->cmdq, data->workspace, &pattern, sizeof(cl_float), 0, data->workspace_size, 0, NULL, NULL);
        if(err) return VX_FAILURE;
    }

    //Algorithm.
    data->alpha = 1;
    data->beta = 0;
    miopenConvAlgoPerf_t perf;
    int algo_count;
    ERROR_CHECK_MIOPEN_STATUS(miopenFindConvolutionForwardAlgorithm(data->handle->miopen_handle, data->input_desc, data->input_mem, data->weight_desc, data->weight_mem, data->convdesc,
                                                                    data->output_desc, data->output_mem, 1, &algo_count, &perf, data->workspace, data->workspace_size, data->handle->exhaustiveSearch));
    data->algo = perf.fwd_algo;

#if ENABLE_DEBUG_PRINT_DIMS
    std::cout << "fullyconnected input " << input_dims[3] << " " << input_dims[2] << " " << input_dims[1] << " " << input_dims[0] << " ";
    std::cout << "weights " << weights_dims[3] << weights_dims[2] << weights_dims[1] << weights_dims[0] << " ";
    std::cout << "bias " << bias_dims[0] << " ";
    std::cout << "output " << output_dims[3] << " " << output_dims[2] << " " << output_dims[1] << " " << output_dims[0] << std::endl;
#endif

    //add to node attribute.
    ERROR_CHECK_STATUS(vxSetNodeAttribute(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));
    return VX_SUCCESS;
}

static vx_status VX_CALLBACK uninitializeFullyConnectedLayer(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    FullyConnectedLayerLocalData * data = NULL;
    ERROR_CHECK_STATUS(vxQueryNode(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));
    if(data->workspace && clReleaseMemObject(data->workspace) != 0 ) return VX_FAILURE;
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyConvolutionDescriptor(data->convdesc));
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyTensorDescriptor(data->input_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyTensorDescriptor(data->output_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyTensorDescriptor(data->weight_desc));
    ERROR_CHECK_MIOPEN_STATUS(miopenDestroyTensorDescriptor(data->bias_desc));
    if (data) {
        ERROR_CHECK_STATUS(releaseGraphHandle(node, data->handle));
        delete data;
    }
    return VX_SUCCESS;
}

vx_status publishFullyConnectedLayer(vx_context context)
{
    // add kernel to the context with callbacks
    vx_kernel kernel = vxAddUserKernel(context, "org.khronos.nn_extension.fully_connected_layer", VX_KERNEL_FULLYCONNECTED_LAYER, processFullyConnectedLayer, 6, validateFullyConnectedLayer, initializeFullyConnectedLayer, uninitializeFullyConnectedLayer);
    ERROR_CHECK_OBJECT(kernel);

    // enable OpenCL buffer access since the kernel_f callback uses OpenCL buffers instead of host accessible buffers
    vx_bool enableBufferAccess = vx_true_e;
    ERROR_CHECK_STATUS(vxSetKernelAttribute(kernel, VX_KERNEL_ATTRIBUTE_AMD_OPENCL_BUFFER_ACCESS_ENABLE, &enableBufferAccess, sizeof(enableBufferAccess)));

    // set kernel parameters
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 0, VX_INPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_REQUIRED));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 1, VX_INPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_REQUIRED));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 2, VX_INPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_OPTIONAL));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 3, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 4, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
    ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 5, VX_OUTPUT, VX_TYPE_TENSOR, VX_PARAMETER_STATE_REQUIRED));

    // finalize and release kernel object
    ERROR_CHECK_STATUS(vxFinalizeKernel(kernel));
    ERROR_CHECK_STATUS(vxReleaseKernel(&kernel));

    return VX_SUCCESS;
}

VX_API_ENTRY vx_node VX_API_CALL vxFullyConnectedLayer(vx_graph graph, vx_tensor inputs, vx_tensor weights,
                                                       vx_tensor biases, vx_enum overflow_policy, vx_enum rounding_policy, vx_tensor outputs)
{
    vx_node node = NULL;
    vx_context context = vxGetContext((vx_reference)graph);
    if(vxGetStatus((vx_reference)context) == VX_SUCCESS) {
        vx_scalar overflow = vxCreateScalarWithSize(context, VX_TYPE_ENUM, &overflow_policy, sizeof(overflow_policy));
        vx_scalar rounding = vxCreateScalarWithSize(context, VX_TYPE_ENUM, &rounding_policy, sizeof(rounding_policy));
        if(vxGetStatus((vx_reference)overflow) == VX_SUCCESS && vxGetStatus((vx_reference)rounding) == VX_SUCCESS) {
            vx_reference params[] = {
                (vx_reference)inputs,
                (vx_reference)weights,
                (vx_reference)biases,
                (vx_reference)overflow,
                (vx_reference)rounding,
                (vx_reference)outputs
            };
            node = createNode(graph, VX_KERNEL_FULLYCONNECTED_LAYER, params, sizeof(params)/sizeof(params[0]));
        }
    }
    return node;
}