//
// Copyright © 2021 Arm Ltd and Contributors. All rights reserved.
// SPDX-License-Identifier: MIT
//

#pragma once

#include <aclCommon/ArmComputeTensorHandle.hpp>
#include <aclCommon/ArmComputeTensorUtils.hpp>

#include <Half.hpp>

#include <armnn/utility/PolymorphicDowncast.hpp>

#include <arm_compute/runtime/CL/CLTensor.h>
#include <arm_compute/runtime/CL/CLSubTensor.h>
#include <arm_compute/runtime/IMemoryGroup.h>
#include <arm_compute/runtime/MemoryGroup.h>
#include <arm_compute/core/TensorShape.h>
#include <arm_compute/core/Coordinates.h>

#include <include/CL/cl_ext.h>
#include <arm_compute/core/CL/CLKernelLibrary.h>

namespace armnn
{

class IClImportTensorHandle : public IAclTensorHandle
{
public:
    virtual arm_compute::ICLTensor& GetTensor() = 0;
    virtual arm_compute::ICLTensor const& GetTensor() const = 0;
    virtual arm_compute::DataType GetDataType() const = 0;
    virtual void SetMemoryGroup(const std::shared_ptr<arm_compute::IMemoryGroup>& memoryGroup) = 0;
};

class ClImportTensorHandle : public IClImportTensorHandle
{
public:
    ClImportTensorHandle(const TensorInfo& tensorInfo, MemorySourceFlags importFlags)
        : m_ImportFlags(importFlags)
    {
        armnn::armcomputetensorutils::BuildArmComputeTensor(m_Tensor, tensorInfo);
    }

    ClImportTensorHandle(const TensorInfo& tensorInfo,
                         DataLayout dataLayout,
                         MemorySourceFlags importFlags)
        : m_ImportFlags(importFlags)
    {
        armnn::armcomputetensorutils::BuildArmComputeTensor(m_Tensor, tensorInfo, dataLayout);
    }

    arm_compute::CLTensor& GetTensor() override { return m_Tensor; }
    arm_compute::CLTensor const& GetTensor() const override { return m_Tensor; }
    virtual void Allocate() override {}
    virtual void Manage() override {}

    virtual const void* Map(bool blocking = true) const override
    {
        IgnoreUnused(blocking);
        return static_cast<const void*>(m_Tensor.buffer() + m_Tensor.info()->offset_first_element_in_bytes());
    }

    virtual void Unmap() const override {}

    virtual ITensorHandle* GetParent() const override { return nullptr; }

    virtual arm_compute::DataType GetDataType() const override
    {
        return m_Tensor.info()->data_type();
    }

    virtual void SetMemoryGroup(const std::shared_ptr<arm_compute::IMemoryGroup>& memoryGroup) override
    {
        IgnoreUnused(memoryGroup);
    }

    TensorShape GetStrides() const override
    {
        return armcomputetensorutils::GetStrides(m_Tensor.info()->strides_in_bytes());
    }

    TensorShape GetShape() const override
    {
        return armcomputetensorutils::GetShape(m_Tensor.info()->tensor_shape());
    }

    void SetImportFlags(MemorySourceFlags importFlags)
    {
        m_ImportFlags = importFlags;
    }

    MemorySourceFlags GetImportFlags() const override
    {
        return m_ImportFlags;
    }

    virtual bool Import(void* memory, MemorySource source) override
    {
        if (m_ImportFlags & static_cast<MemorySourceFlags>(source))
        {
            if (source == MemorySource::Malloc)
            {
                const size_t totalBytes = m_Tensor.info()->total_size();

                const cl_import_properties_arm importProperties[] =
                {
                        CL_IMPORT_TYPE_ARM,
                        CL_IMPORT_TYPE_HOST_ARM,
                        0
                };

                cl_int error = CL_SUCCESS;
                cl_mem buffer = clImportMemoryARM(arm_compute::CLKernelLibrary::get().context().get(),
                                                  CL_MEM_READ_WRITE, importProperties, memory, totalBytes, &error);
                if (error != CL_SUCCESS)
                {
                    throw MemoryImportException(
                        "ClImportTensorHandle::Invalid imported memory:" + std::to_string(error));
                }

                cl::Buffer wrappedBuffer(buffer);
                arm_compute::Status status = m_Tensor.allocator()->import_memory(wrappedBuffer);

                // Use the overloaded bool operator of Status to check if it worked, if not throw an exception
                // with the Status error message
                bool imported = (status.error_code() == arm_compute::ErrorCode::OK);
                if (!imported)
                {
                    throw MemoryImportException(status.error_description());
                }
                ARMNN_ASSERT(!m_Tensor.info()->is_resizable());
                return imported;
            }
            else
            {
                throw MemoryImportException("ClImportTensorHandle::Import flag is not supported");
            }
        }
        else
        {
            throw MemoryImportException("ClImportTensorHandle::Incorrect import flag");
        }
        return false;
    }

private:
    // Only used for testing
    void CopyOutTo(void* memory) const override
    {
        const_cast<armnn::ClImportTensorHandle*>(this)->Map(true);
        switch(this->GetDataType())
        {
            case arm_compute::DataType::F32:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<float*>(memory));
                break;
            case arm_compute::DataType::U8:
            case arm_compute::DataType::QASYMM8:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<uint8_t*>(memory));
                break;
            case arm_compute::DataType::QSYMM8_PER_CHANNEL:
            case arm_compute::DataType::QASYMM8_SIGNED:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<int8_t*>(memory));
                break;
            case arm_compute::DataType::F16:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<armnn::Half*>(memory));
                break;
            case arm_compute::DataType::S16:
            case arm_compute::DataType::QSYMM16:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<int16_t*>(memory));
                break;
            case arm_compute::DataType::S32:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<int32_t*>(memory));
                break;
            default:
            {
                throw armnn::UnimplementedException();
            }
        }
        const_cast<armnn::ClImportTensorHandle*>(this)->Unmap();
    }

    // Only used for testing
    void CopyInFrom(const void* memory) override
    {
        this->Map(true);
        switch(this->GetDataType())
        {
            case arm_compute::DataType::F32:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const float*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::U8:
            case arm_compute::DataType::QASYMM8:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const uint8_t*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::F16:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const armnn::Half*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::S16:
            case arm_compute::DataType::QSYMM8_PER_CHANNEL:
            case arm_compute::DataType::QASYMM8_SIGNED:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const int8_t*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::QSYMM16:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const int16_t*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::S32:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const int32_t*>(memory),
                                                                 this->GetTensor());
                break;
            default:
            {
                throw armnn::UnimplementedException();
            }
        }
        this->Unmap();
    }

    arm_compute::CLTensor m_Tensor;
    MemorySourceFlags m_ImportFlags;
};

class ClImportSubTensorHandle : public IClImportTensorHandle
{
public:
    ClImportSubTensorHandle(IClImportTensorHandle* parent,
                      const arm_compute::TensorShape& shape,
                      const arm_compute::Coordinates& coords)
    : m_Tensor(&parent->GetTensor(), shape, coords)
    {
        parentHandle = parent;
    }

    arm_compute::CLSubTensor& GetTensor() override { return m_Tensor; }
    arm_compute::CLSubTensor const& GetTensor() const override { return m_Tensor; }

    virtual void Allocate() override {}
    virtual void Manage() override {}

    virtual const void* Map(bool blocking = true) const override
    {
        IgnoreUnused(blocking);
        return static_cast<const void*>(m_Tensor.buffer() + m_Tensor.info()->offset_first_element_in_bytes());
    }
    virtual void Unmap() const override {}

    virtual ITensorHandle* GetParent() const override { return parentHandle; }

    virtual arm_compute::DataType GetDataType() const override
    {
        return m_Tensor.info()->data_type();
    }

    virtual void SetMemoryGroup(const std::shared_ptr<arm_compute::IMemoryGroup>& memoryGroup) override
    {
        IgnoreUnused(memoryGroup);
    }

    TensorShape GetStrides() const override
    {
        return armcomputetensorutils::GetStrides(m_Tensor.info()->strides_in_bytes());
    }

    TensorShape GetShape() const override
    {
        return armcomputetensorutils::GetShape(m_Tensor.info()->tensor_shape());
    }

private:
    // Only used for testing
    void CopyOutTo(void* memory) const override
    {
        const_cast<ClImportSubTensorHandle*>(this)->Map(true);
        switch(this->GetDataType())
        {
            case arm_compute::DataType::F32:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<float*>(memory));
                break;
            case arm_compute::DataType::U8:
            case arm_compute::DataType::QASYMM8:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<uint8_t*>(memory));
                break;
            case arm_compute::DataType::F16:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<armnn::Half*>(memory));
                break;
            case arm_compute::DataType::QSYMM8_PER_CHANNEL:
            case arm_compute::DataType::QASYMM8_SIGNED:
            armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                             static_cast<int8_t*>(memory));
                break;
            case arm_compute::DataType::S16:
            case arm_compute::DataType::QSYMM16:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<int16_t*>(memory));
                break;
            case arm_compute::DataType::S32:
                armcomputetensorutils::CopyArmComputeITensorData(this->GetTensor(),
                                                                 static_cast<int32_t*>(memory));
                break;
            default:
            {
                throw armnn::UnimplementedException();
            }
        }
        const_cast<ClImportSubTensorHandle*>(this)->Unmap();
    }

    // Only used for testing
    void CopyInFrom(const void* memory) override
    {
        this->Map(true);
        switch(this->GetDataType())
        {
            case arm_compute::DataType::F32:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const float*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::U8:
            case arm_compute::DataType::QASYMM8:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const uint8_t*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::F16:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const armnn::Half*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::QSYMM8_PER_CHANNEL:
            case arm_compute::DataType::QASYMM8_SIGNED:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const int8_t*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::S16:
            case arm_compute::DataType::QSYMM16:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const int16_t*>(memory),
                                                                 this->GetTensor());
                break;
            case arm_compute::DataType::S32:
                armcomputetensorutils::CopyArmComputeITensorData(static_cast<const int32_t*>(memory),
                                                                 this->GetTensor());
                break;
            default:
            {
                throw armnn::UnimplementedException();
            }
        }
        this->Unmap();
    }

    mutable arm_compute::CLSubTensor m_Tensor;
    ITensorHandle* parentHandle = nullptr;
};

} // namespace armnn