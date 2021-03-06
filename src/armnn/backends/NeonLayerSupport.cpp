//
// Copyright © 2017 Arm Ltd. All rights reserved.
// See LICENSE file in the project root for full license information.
//

#include "NeonLayerSupport.hpp"

#include "LayerSupportCommon.hpp"
#include "InternalTypes.hpp"

#include <armnn/Descriptors.hpp>
#include <armnn/Types.hpp>
#include <armnn/Tensor.hpp>

#include <boost/core/ignore_unused.hpp>

#ifdef ARMCOMPUTENEON_ENABLED
#include "NeonWorkloads/NeonAdditionFloat32Workload.hpp"
#include "NeonWorkloads/NeonActivationFloat32Workload.hpp"
#include "NeonWorkloads/NeonBatchNormalizationFloat32Workload.hpp"
#include "NeonWorkloads/NeonConvolution2dBaseWorkload.hpp"
#include "NeonWorkloads/NeonDepthwiseConvolutionBaseWorkload.hpp"
#include "NeonWorkloads/NeonL2NormalizationFloat32Workload.hpp"
#include "NeonWorkloads/NeonMultiplicationFloat32Workload.hpp"
#include "NeonWorkloads/NeonNormalizationFloat32Workload.hpp"
#include "NeonWorkloads/NeonFullyConnectedFloat32Workload.hpp"
#include "NeonWorkloads/NeonPermuteWorkload.hpp"
#include "NeonWorkloads/NeonPooling2dBaseWorkload.hpp"
#include "NeonWorkloads/NeonSoftmaxBaseWorkload.hpp"
#endif

using namespace boost;

namespace armnn
{

bool IsNeonDirectConvolutionPreferred(const TensorInfo& weightInfo, const Convolution2dDescriptor& desc)
{
    // See arm_compute::NEDirectConvolutionLayer documentation for the supported cases,
    // and complement with NEDirectConvolutionLayerKernel::configure() implementation.

    // Only 1x1 is using direct convolution. Performance results and details are in:
    //    https://jira.arm.com/browse/IVGCVSW-1003
    // Measurements were taken as of clframework: f105ab972135bcd21304883eff040d7e587099bc

    const bool dataTypeSupported = (weightInfo.GetDataType() == armnn::DataType::Float32);

    // Strides: 1|2|3
    const bool strideSupported = (desc.m_StrideX == 1 || desc.m_StrideX == 2 || desc.m_StrideX == 3) &&
                                 (desc.m_StrideY == 1 || desc.m_StrideY == 2 || desc.m_StrideY == 3);

    auto paddingLargerThan = [](const Convolution2dDescriptor& conv2ddesc, unsigned int value)
    {
        return conv2ddesc.m_PadLeft > value || conv2ddesc.m_PadRight > value ||
               conv2ddesc.m_PadTop > value || conv2ddesc.m_PadBottom > value;
    };

    // Supported sizes and padding.
    const bool sizeAndPaddingSupported =
        // Pad > 0 not supported for 1x1 weights.
        (weightInfo.GetShape()[2] == 1 && weightInfo.GetShape()[3] == 1 && !paddingLargerThan(desc, 0u));

    const bool preferDirectConvolution = dataTypeSupported &&
                                         strideSupported &&
                                         sizeAndPaddingSupported &&
                                         // NEDirectConvolutionLayerKernel doesn't support NULL bias.
                                         desc.m_BiasEnabled;
    return preferDirectConvolution;
}

bool IsNeonNormalizationDescParamsSupported(std::string* reasonIfUnsupported, const NormalizationDescriptor& parameters)
{
    if (parameters.m_NormMethodType != NormalizationAlgorithmMethod::LocalBrightness)
    {
        if (reasonIfUnsupported)
        {
            *reasonIfUnsupported = "Unsupported normalisation method type, only LocalBrightness is supported";
        }
        return false;
    }
    if (parameters.m_NormSize % 2 == 0)
    {
        if (reasonIfUnsupported)
        {
            *reasonIfUnsupported = "Normalization size must be an odd number.";
        }
        return false;
    }

    return true;
}

bool IsNeonBackendSupported(std::string* reasonIfUnsupported)
{
#if ARMCOMPUTENEON_ENABLED
    return true;
#else
    if (reasonIfUnsupported != nullptr)
    {
        *reasonIfUnsupported = "The armnn library has been built without NEON support";
    }
    return false;
#endif
}

template<typename FloatFunc, typename Uint8Func, typename ... Params>
bool IsSupportedForDataTypeNeon(std::string* reasonIfUnsupported,
                                DataType dataType,
                                FloatFunc floatFuncPtr,
                                Uint8Func uint8FuncPtr,
                                Params&&... params)
{
    return IsNeonBackendSupported(reasonIfUnsupported) &&
        IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                         dataType,
                                         floatFuncPtr,
                                         floatFuncPtr,
                                         uint8FuncPtr,
                                         std::forward<Params>(params)...);
}

#if ARMCOMPUTENEON_ENABLED
template<class FuncType, class... Args>
inline bool IsWorkloadSupported(FuncType& func, std::string* reasonIfUnsupported, Args&&... args)
{
    arm_compute::Status aclStatus = func(std::forward<Args>(args)...);
    const bool supported = (aclStatus.error_code() == arm_compute::ErrorCode::OK);
    if (!supported && reasonIfUnsupported)
    {
        *reasonIfUnsupported = aclStatus.error_description();
    }
    return supported;
}

#define FORWARD_WORKLOAD_VALIDATE_FUNC(func, reasonIfUnsupported, ...) \
    return IsWorkloadSupported(func, reasonIfUnsupported, __VA_ARGS__);
#else
#define FORWARD_WORKLOAD_VALIDATE_FUNC(func, reasonIfUnsupported, ...) \
    return IsNeonBackendSupported(reasonIfUnsupported);
#endif

bool IsActivationSupportedNeon(const TensorInfo& input,
                               const TensorInfo& output,
                               const ActivationDescriptor& descriptor,
                               std::string* reasonIfUnsupported)
{
    ignore_unused(descriptor);
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonActivationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor);
}

bool IsAdditionSupportedNeon(const TensorInfo& input0,
                             const TensorInfo& input1,
                             const TensorInfo& output,
                             std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonAdditionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool IsBatchNormalizationSupportedNeon(const TensorInfo& input,
                                       const TensorInfo& output,
                                       const TensorInfo& mean,
                                       const TensorInfo& var,
                                       const TensorInfo& beta,
                                       const TensorInfo& gamma,
                                       const BatchNormalizationDescriptor& descriptor,
                                       std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonBatchNormalizationValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   mean,
                                   var,
                                   beta,
                                   gamma,
                                   descriptor);
}

bool IsConstantSupportedNeon(const TensorInfo& output,
                             std::string* reasonIfUnsupported)
{
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      output.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool IsConvolution2dSupportedNeon(const TensorInfo& input,
                                  const TensorInfo& output,
                                  const Convolution2dDescriptor& descriptor,
                                  const TensorInfo& weights,
                                  const TensorInfo& biases,
                                  std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonConvolution2dWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}

bool IsDepthwiseConvolutionSupportedNeon(const TensorInfo& input,
                                         const TensorInfo& output,
                                         const DepthwiseConvolution2dDescriptor& descriptor,
                                         const TensorInfo& weights,
                                         const TensorInfo& biases,
                                         std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonDepthwiseConvolutionWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   descriptor,
                                   weights,
                                   biases);
}

bool IsFullyConnectedSupportedNeon(const TensorInfo& input,
                                   const TensorInfo& output,
                                   const TensorInfo& weights,
                                   const TensorInfo& biases,
                                   const FullyConnectedDescriptor& descriptor,
                                   std::string* reasonIfUnsupported)
{
    // At the moment U8 is unsupported
    if (input.GetDataType() == DataType::QuantisedAsymm8)
    {
        return false;
    }
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonFullyConnectedWorkloadValidate,
                                   reasonIfUnsupported,
                                   input,
                                   output,
                                   weights,
                                   biases,
                                   descriptor);
}

bool IsInputSupportedNeon(const TensorInfo& input,
                          std::string* reasonIfUnsupported)
{
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      input.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool IsL2NormalizationSupportedNeon(const TensorInfo& input,
                                    const TensorInfo& output,
                                    std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonL2NormalizationWorkloadValidate, reasonIfUnsupported, input, output);
}

bool IsMergerSupportedNeon(const std::vector<const TensorInfo*> inputs,
                           const OriginsDescriptor& descriptor,
                           std::string* reasonIfUnsupported)
{
    ignore_unused(descriptor);
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      inputs[0]->GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool IsMultiplicationSupportedNeon(const TensorInfo& input0,
                                   const TensorInfo& input1,
                                   const TensorInfo& output,
                                   std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonMultiplicationWorkloadValidate,
                                   reasonIfUnsupported,
                                   input0,
                                   input1,
                                   output);
}

bool IsNormalizationSupportedNeon(const TensorInfo& input,
                                  const TensorInfo& output,
                                  const NormalizationDescriptor& descriptor,
                                  std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonNormalizationWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool IsOutputSupportedNeon(const TensorInfo& output,
                           std::string* reasonIfUnsupported)
{
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      output.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool IsPermuteSupportedNeon(const TensorInfo& input,
                            const TensorInfo& output,
                            const PermuteDescriptor& descriptor,
                            std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonPermuteWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool IsPooling2dSupportedNeon(const TensorInfo& input,
                              const TensorInfo& output,
                              const Pooling2dDescriptor& descriptor,
                              std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonPooling2dWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool IsResizeBilinearSupportedNeon(const TensorInfo& input,
                                   std::string* reasonIfUnsupported)
{
    ignore_unused(input);
    return false;
}

bool IsSoftmaxSupportedNeon(const TensorInfo& input,
                            const TensorInfo& output,
                            const SoftmaxDescriptor& descriptor,
                            std::string* reasonIfUnsupported)
{
    FORWARD_WORKLOAD_VALIDATE_FUNC(NeonSoftmaxWorkloadValidate, reasonIfUnsupported, input, output, descriptor);
}

bool IsSplitterSupportedNeon(const TensorInfo& input,
                             const ViewsDescriptor& descriptor,
                             std::string* reasonIfUnsupported)
{
    ignore_unused(descriptor);
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      input.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool IsFakeQuantizationSupportedNeon(const TensorInfo& input,
                                     const FakeQuantizationDescriptor& descriptor,
                                     std::string* reasonIfUnsupported)
{
    ignore_unused(input);
    ignore_unused(descriptor);
    return false;
}

bool IsReshapeSupportedNeon(const TensorInfo& input,
                            std::string* reasonIfUnsupported)
{
    return IsSupportedForDataTypeNeon(reasonIfUnsupported,
                                      input.GetDataType(),
                                      &TrueFunc<>,
                                      &TrueFunc<>);
}

bool IsFloorSupportedNeon(const TensorInfo& input,
                          const TensorInfo& output,
                          std::string* reasonIfUnsupported)
{
    ignore_unused(output);
    return IsNeonBackendSupported(reasonIfUnsupported) &&
           IsSupportedForDataTypeGeneric(reasonIfUnsupported,
                                         input.GetDataType(),
                                         &FalseFuncF16<>,
                                         &TrueFunc<>,
                                         &FalseFuncU8<>);
}

bool IsLstmSupportedNeon(const TensorInfo& input, const TensorInfo& outputStateIn,
                         const TensorInfo& cellStateIn, const TensorInfo& scratchBuffer,
                         const TensorInfo& outputStateOut, const TensorInfo& cellStateOut,
                         const TensorInfo& output, const LstmDescriptor& descriptor,
                         const TensorInfo& inputToForgetWeights, const TensorInfo& inputToCellWeights,
                         const TensorInfo& inputToOutputWeights, const TensorInfo& recurrentToForgetWeights,
                         const TensorInfo& recurrentToCellWeights, const TensorInfo& recurrentToOutputWeights,
                         const TensorInfo& forgetGateBias, const TensorInfo& cellBias,
                         const TensorInfo& outputGateBias, const TensorInfo* inputToInputWeights,
                         const TensorInfo* recurrentToInputWeights, const TensorInfo* cellToInputWeights,
                         const TensorInfo* inputGateBias, const TensorInfo* projectionWeights,
                         const TensorInfo* projectionBias, const TensorInfo* cellToForgetWeights,
                         const TensorInfo* cellToOutputWeights, std::string* reasonIfUnsupported)
{
    ignore_unused(input);
    ignore_unused(outputStateIn);
    ignore_unused(cellStateIn);
    ignore_unused(scratchBuffer);
    ignore_unused(outputStateOut);
    ignore_unused(cellStateOut);
    ignore_unused(output);
    ignore_unused(descriptor);
    ignore_unused(inputToForgetWeights);
    ignore_unused(inputToCellWeights);
    ignore_unused(inputToOutputWeights);
    ignore_unused(recurrentToForgetWeights);
    ignore_unused(recurrentToCellWeights);
    ignore_unused(recurrentToOutputWeights);
    ignore_unused(forgetGateBias);
    ignore_unused(cellBias);
    ignore_unused(outputGateBias);
    ignore_unused(inputToInputWeights);
    ignore_unused(recurrentToInputWeights);
    ignore_unused(cellToInputWeights);
    ignore_unused(inputGateBias);
    ignore_unused(projectionWeights);
    ignore_unused(projectionBias);
    ignore_unused(cellToForgetWeights);
    ignore_unused(cellToOutputWeights);
    return false;
}

bool IsConvertFp16ToFp32SupportedNeon(const TensorInfo& input,
                                      const TensorInfo& output,
                                      std::string* reasonIfUnsupported)
{
    ignore_unused(input);
    ignore_unused(output);
    return true;
}

bool IsConvertFp32ToFp16SupportedNeon(const TensorInfo& input,
                                      const TensorInfo& output,
                                      std::string* reasonIfUnsupported)
{
    ignore_unused(input);
    ignore_unused(output);
    return true;
}

}
