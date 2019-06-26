//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "CommonTestUtils.hpp"

#include <backendsCommon/IBackendInternal.hpp>

using namespace armnn;

SubgraphView::InputSlots CreateInputsFrom(const std::vector<Layer*>& layers)
{
    SubgraphView::InputSlots result;
    for (auto&& layer : layers)
    {
        for (auto&& it = layer->BeginInputSlots(); it != layer->EndInputSlots(); ++it)
        {
            result.push_back(&(*it));
        }
    }
    return result;
}

SubgraphView::OutputSlots CreateOutputsFrom(const std::vector<Layer*>& layers)
{
    SubgraphView::OutputSlots result;
    for (auto && layer : layers)
    {
        for (auto&& it = layer->BeginOutputSlots(); it != layer->EndOutputSlots(); ++it)
        {
            result.push_back(&(*it));
        }
    }
    return result;
}

SubgraphView::SubgraphViewPtr CreateSubgraphViewFrom(SubgraphView::InputSlots&& inputs,
                                                     SubgraphView::OutputSlots&& outputs,
                                                     SubgraphView::Layers&& layers)
{
    return std::make_unique<SubgraphView>(std::move(inputs), std::move(outputs), std::move(layers));
}

armnn::IBackendInternalUniquePtr CreateBackendObject(const armnn::BackendId& backendId)
{
    auto& backendRegistry = BackendRegistryInstance();
    auto  backendFactory  = backendRegistry.GetFactory(backendId);
    auto  backendObjPtr   = backendFactory();

    return backendObjPtr;
}

armnn::TensorShape MakeTensorShape(unsigned int batches,
                                   unsigned int channels,
                                   unsigned int height,
                                   unsigned int width,
                                   armnn::DataLayout layout)
{
    using namespace armnn;
    switch (layout)
    {
        case DataLayout::NCHW:
            return TensorShape{ batches, channels, height, width };
        case DataLayout::NHWC:
            return TensorShape{ batches, height, width, channels };
        default:
            throw InvalidArgumentException(std::string("Unsupported data layout: ") + GetDataLayoutName(layout));
    }
}
