/* file: execution_context_sycl.h */
/*******************************************************************************
* Copyright 2014-2020 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifdef DAAL_SYCL_INTERFACE
    #ifndef __DAAL_SERVICES_INTERNAL_SYCL_EXECUTION_CONTEXT_SYCL_H__
        #define __DAAL_SERVICES_INTERNAL_SYCL_EXECUTION_CONTEXT_SYCL_H__

        #include "services/daal_string.h"
        #include "services/internal/hash_table.h"
        #include "services/internal/sycl/execution_context.h"
        #include "services/internal/sycl/kernel_scheduler_sycl.h"
        #include "services/internal/sycl/math/blas_executor.h"
        #include "services/internal/sycl/math/lapack_executor.h"
        #include "services/internal/sycl/error_handling.h"

namespace daal
{
namespace services
{
namespace internal
{
namespace sycl
{
namespace interface1
{
class OpenClKernelFactory : public Base, public ClKernelFactoryIface
{
public:
    explicit OpenClKernelFactory(cl::sycl::queue & deviceQueue)
        : _currentProgramRef(nullptr), _executionTarget(ExecutionTargetIds::unspecified), _deviceQueue(deviceQueue)
    {}

    ~OpenClKernelFactory() DAAL_C11_OVERRIDE {}

    void build(ExecutionTargetId target, const char * name, const char * program, const char * options, services::Status & status) DAAL_C11_OVERRIDE
    {
        services::String key = name;
        const bool res       = programHashTable.contain(key, status);
        DAAL_CHECK_STATUS_RETURN_VOID_IF_FAIL(status);

        if (!res)
        {
        #ifndef DAAL_DISABLE_LEVEL_ZERO
            const bool isOpenCLBackendAvailable = !_deviceQueue.get_device().template get_info<cl::sycl::info::device::opencl_c_version>().empty();
            if (isOpenCLBackendAvailable)
            {
        #endif // DAAL_DISABLE_LEVEL_ZERO \
            // OpenCl branch
                auto programPtr = services::SharedPtr<OpenClProgramRef>(
                    new OpenClProgramRef(_deviceQueue.get_context().get(), _deviceQueue.get_device().get(), name, program, options, status));
                DAAL_CHECK_STATUS_RETURN_VOID_IF_FAIL(status);

                programHashTable.add(key, programPtr, status);
                DAAL_CHECK_STATUS_RETURN_VOID_IF_FAIL(status);

                _currentProgramRef = programPtr.get();
        #ifndef DAAL_DISABLE_LEVEL_ZERO
            }
            else
            {
                // Level zero branch
                if (nullptr == _levelZeroOpenClInteropContext.getOpenClDeviceRef().get())
                {
                    _levelZeroOpenClInteropContext.reset(_deviceQueue, status);
                    DAAL_CHECK_STATUS_RETURN_VOID_IF_FAIL(status);
                }

                auto programPtr = services::SharedPtr<OpenClProgramRef>(
                    new OpenClProgramRef(_levelZeroOpenClInteropContext.getOpenClContextRef().get(),
                                         _levelZeroOpenClInteropContext.getOpenClDeviceRef().get(), _deviceQueue, name, program, options, status));
                DAAL_CHECK_STATUS_RETURN_VOID_IF_FAIL(status);

                programHashTable.add(key, programPtr, status);
                DAAL_CHECK_STATUS_RETURN_VOID_IF_FAIL(status);

                _currentProgramRef = programPtr.get();
            }
        #endif // DAAL_DISABLE_LEVEL_ZERO
        }
        else
        {
            _currentProgramRef = programHashTable.get(key, status).get();
            DAAL_CHECK_STATUS_RETURN_VOID_IF_FAIL(status);
        }

        _executionTarget = target;
    }

    KernelPtr getKernel(const char * kernelName, services::Status & status) DAAL_C11_OVERRIDE
    {
        if (_currentProgramRef == nullptr)
        {
            status |= services::ErrorExecutionContext;
            return KernelPtr();
        }

        services::String key = _currentProgramRef->getName();
        key.add(kernelName);

        bool res = kernelHashTable.contain(key, status);
        DAAL_CHECK_STATUS_RETURN_IF_FAIL(status, KernelPtr());

        if (res)
        {
            auto kernel = kernelHashTable.get(key, status);
            DAAL_CHECK_STATUS_RETURN_IF_FAIL(status, KernelPtr());

            return kernel;
        }
        else
        {
            KernelPtr kernel;
        #ifndef DAAL_DISABLE_LEVEL_ZERO
            const bool isOpenCLBackendAvailable = !_deviceQueue.get_device().template get_info<cl::sycl::info::device::opencl_c_version>().empty();
            if (isOpenCLBackendAvailable)
            {
        #endif // DAAL_DISABLE_LEVEL_ZERO

                // OpenCL branch
                auto kernelRef = OpenClKernelRef(_currentProgramRef->get(), kernelName, status);
                DAAL_CHECK_STATUS_RETURN_IF_FAIL(status, KernelPtr());

                kernel.reset(new OpenClKernelNative(_executionTarget, *_currentProgramRef, kernelRef));
        #ifndef DAAL_DISABLE_LEVEL_ZERO
            }
            else
            {
                // Level zero branch
                auto kernelRef = OpenClKernelLevelZeroRef(kernelName, status);
                DAAL_CHECK_STATUS_RETURN_IF_FAIL(status, KernelPtr());

                kernel.reset(new OpenClKernelLevelZero(_executionTarget, *_currentProgramRef, kernelRef));
            }
        #endif // DAAL_DISABLE_LEVEL_ZERO
            kernelHashTable.add(key, kernel, status);
            DAAL_CHECK_STATUS_RETURN_IF_FAIL(status, KernelPtr());

            return kernel;
        }
    }

private:
    static const size_t SIZE_HASHTABLE_PROGRAM = 1024;
    static const size_t SIZE_HASHTABLE_KERNEL  = 4096;
    services::internal::HashTable<OpenClProgramRef, SIZE_HASHTABLE_PROGRAM> programHashTable;
    services::internal::HashTable<KernelIface, SIZE_HASHTABLE_KERNEL> kernelHashTable;

    OpenClProgramRef * _currentProgramRef;
        #ifndef DAAL_DISABLE_LEVEL_ZERO
    LevelZeroOpenClInteropContext _levelZeroOpenClInteropContext;
        #endif // DAAL_DISABLE_LEVEL_ZERO

    ExecutionTargetId _executionTarget;
    cl::sycl::queue & _deviceQueue;
};

class SyclExecutionContextImpl : public Base, public ExecutionContextIface
{
public:
    explicit SyclExecutionContextImpl(const cl::sycl::queue & deviceQueue)
        : _deviceQueue(deviceQueue), _kernelFactory(_deviceQueue), _kernelScheduler(_deviceQueue)
    {
        const auto & device          = _deviceQueue.get_device();
        _infoDevice.isCpu            = device.is_cpu() || device.is_host();
        _infoDevice.maxWorkGroupSize = device.get_info<cl::sycl::info::device::max_work_group_size>();
    }

    void run(const KernelRange & range, const KernelPtr & kernel, const KernelArguments & args, services::Status & status) DAAL_C11_OVERRIDE
    {
        // TODO: Thread safe?
        // TODO: Check for input arguments
        // TODO: Need to save reference to kernel to prevent
        //       releasing in case of asynchronous execution?
        kernel->schedule(_kernelScheduler, range, args, status);
    }

    void run(const KernelNDRange & range, const KernelPtr & kernel, const KernelArguments & args, services::Status & status) DAAL_C11_OVERRIDE
    {
        // TODO: Thread safe?
        // TODO: Check for input arguments
        // TODO: Need to save reference to kernel to prevent
        //       releasing in case of asynchronous execution?
        kernel->schedule(_kernelScheduler, range, args, status);
    }

    void gemm(math::Transpose transa, math::Transpose transb, size_t m, size_t n, size_t k, double alpha, const UniversalBuffer & a_buffer,
              size_t lda, size_t offsetA, const UniversalBuffer & b_buffer, size_t ldb, size_t offsetB, double beta, UniversalBuffer & c_buffer,
              size_t ldc, size_t offsetC, services::Status & status) DAAL_C11_OVERRIDE
    {
        DAAL_ASSERT(a_buffer.type() == b_buffer.type());
        DAAL_ASSERT(b_buffer.type() == c_buffer.type());

        // TODO: Check for input arguments
        math::GemmExecutor::run(_deviceQueue, transa, transb, m, n, k, alpha, a_buffer, lda, offsetA, b_buffer, ldb, offsetB, beta, c_buffer, ldc,
                                offsetC, status);
    }

    void syrk(math::UpLo upper_lower, math::Transpose trans, size_t n, size_t k, double alpha, const UniversalBuffer & a_buffer, size_t lda,
              size_t offsetA, double beta, UniversalBuffer & c_buffer, size_t ldc, size_t offsetC, services::Status & status) DAAL_C11_OVERRIDE
    {
        DAAL_ASSERT(a_buffer.type() == c_buffer.type());

        math::SyrkExecutor::run(_deviceQueue, upper_lower, trans, n, k, alpha, a_buffer, lda, offsetA, beta, c_buffer, ldc, offsetC, status);
    }

    void axpy(const uint32_t n, const double a, const UniversalBuffer x_buffer, const int incx, const UniversalBuffer y_buffer, const int incy,
              services::Status & status) DAAL_C11_OVERRIDE
    {
        DAAL_ASSERT(x_buffer.type() == y_buffer.type());

        math::AxpyExecutor::run(_deviceQueue, n, a, x_buffer, incx, y_buffer, incy, status);
    }

    void potrf(math::UpLo uplo, size_t n, UniversalBuffer & a_buffer, size_t lda, services::Status & status) DAAL_C11_OVERRIDE
    {
        math::PotrfExecutor::run(_deviceQueue, uplo, n, a_buffer, lda, status);
    }

    void potrs(math::UpLo uplo, size_t n, size_t ny, UniversalBuffer & a_buffer, size_t lda, UniversalBuffer & b_buffer, size_t ldb,
               services::Status & status) DAAL_C11_OVERRIDE
    {
        DAAL_ASSERT(a_buffer.type() == b_buffer.type());
        math::PotrsExecutor::run(_deviceQueue, uplo, n, ny, a_buffer, lda, b_buffer, ldb, status);
    }

    UniversalBuffer allocate(TypeId type, size_t bufferSize, services::Status & status) DAAL_C11_OVERRIDE
    {
        // TODO: Thread safe?
        try
        {
            auto buffer = BufferAllocator::allocate(type, bufferSize);
            return buffer;
        }
        catch (cl::sycl::exception const & e)
        {
            convertSyclExceptionToStatus(e, status);
            return UniversalBuffer();
        }
    }

    void copy(UniversalBuffer dest, size_t desOffset, UniversalBuffer src, size_t srcOffset, size_t count,
              services::Status & status) DAAL_C11_OVERRIDE
    {
        DAAL_ASSERT(dest.type() == src.type());
        // TODO: Thread safe?
        try
        {
            BufferCopier::copy(_deviceQueue, dest, desOffset, src, srcOffset, count, status);
        }
        catch (cl::sycl::exception const & e)
        {
            convertSyclExceptionToStatus(e, status);
        }
    }

    void fill(UniversalBuffer dest, double value, services::Status & status) DAAL_C11_OVERRIDE
    {
        // TODO: Thread safe?
        try
        {
            BufferFiller::fill(_deviceQueue, dest, value, status);
        }
        catch (cl::sycl::exception const & e)
        {
            convertSyclExceptionToStatus(e, status);
        }
    }

    ClKernelFactoryIface & getClKernelFactory() DAAL_C11_OVERRIDE { return _kernelFactory; }

    InfoDevice & getInfoDevice() DAAL_C11_OVERRIDE { return _infoDevice; }

    void copy(UniversalBuffer dest, size_t desOffset, void * src, size_t srcOffset, size_t count, services::Status & status) DAAL_C11_OVERRIDE
    {
        // TODO: Thread safe?
        try
        {
            ArrayCopier::copy(_deviceQueue, dest, desOffset, src, srcOffset, count, status);
        }
        catch (cl::sycl::exception const & e)
        {
            convertSyclExceptionToStatus(e, status);
        }
    }

private:
    cl::sycl::queue _deviceQueue;
    OpenClKernelFactory _kernelFactory;
    SyclKernelScheduler _kernelScheduler;
    InfoDevice _infoDevice;
};

/** } */
} // namespace interface1

using interface1::SyclExecutionContextImpl;

} // namespace sycl
} // namespace internal
} // namespace services
} // namespace daal

    #endif
#endif // DAAL_SYCL_INTERFACE
