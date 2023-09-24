#include <libutils/misc.h>
#include <libutils/timer.h>
#include <libutils/fast_random.h>

#include <libgpu/context.h>
#include <libgpu/shared_device_buffer.h>

#include "cl/sum_cl.h"


template<typename T>
void raiseFail(const T &a, const T &b, std::string message, std::string filename, int line)
{
    if (a != b) {
        std::cerr << message << " But " << a << " != " << b << ", " << filename << ":" << line << std::endl;
        throw std::runtime_error(message);
    }
}

#define EXPECT_THE_SAME(a, b, message) raiseFail(a, b, message, __FILE__, __LINE__)


int main(int argc, char **argv)
{
    int benchmarkingIters = 10;

    unsigned int reference_sum = 0;
    unsigned int n = 100*1000*1000;
    std::vector<unsigned int> as(n, 0);
    FastRandom r(42);
    for (int i = 0; i < n; ++i) {
        as[i] = (unsigned int) r.next(0, std::numeric_limits<unsigned int>::max() / n);
        reference_sum += as[i];
    }

    {
        timer t;
        for (int iter = 0; iter < benchmarkingIters; ++iter) {
            unsigned int sum = 0;
            for (int i = 0; i < n; ++i) {
                sum += as[i];
            }
            EXPECT_THE_SAME(reference_sum, sum, "CPU result should be consistent!");
            t.nextLap();
        }
        std::cout << "CPU:     " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "CPU:     " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }

    {
        timer t;
        for (int iter = 0; iter < benchmarkingIters; ++iter) {
            unsigned int sum = 0;
            #pragma omp parallel for reduction(+:sum)
            for (int i = 0; i < n; ++i) {
                sum += as[i];
            }
            EXPECT_THE_SAME(reference_sum, sum, "CPU OpenMP result should be consistent!");
            t.nextLap();
        }
        std::cout << "CPU OMP: " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "CPU OMP: " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
    }

    {
        // constants
        const int workGroupSize = 128;
        const int global_work_size = (n + workGroupSize - 1) / workGroupSize * workGroupSize;
        std::vector<std::string> kernel_names {
            "sum_dummy", // all work is being done by single workitem 
            "sum_global_atomic", // each workitem atomically adds its own value
            "sum_loop", // each workitem adds VALUES_PER_WORKITEM. Not optimezed for coalescence
            "sum_loop_coalesced", // same, but optimized
            "sum_local_memory", // using local memory
            "sum_tree", // using tree gathering
        };

        gpu::Device device = gpu::chooseGPUDevice(argc, argv);
        gpu::Context context;
        context.init(device.device_id_opencl);
        context.activate();

        // prepare input and output buffers
        gpu::gpu_mem_32u as_gpu, result_gpu;
        as_gpu.resizeN(n);
        as_gpu.writeN(as.data(), n);

        result_gpu.resizeN(1);
        unsigned int result, zero;
        zero = 0;

        // benchmark
        for (auto name : kernel_names ) {
            ocl::Kernel kernel(sum_kernel, sum_kernel_length, name);
            kernel.compile(false);

            timer t;
            for (int iter=0; iter < benchmarkingIters; ++iter) {
                result_gpu.writeN(&zero, 1); // clear result
                kernel.exec(gpu::WorkSize(workGroupSize, global_work_size), as_gpu, result_gpu, n);
                result_gpu.readN(&result, 1);
                EXPECT_THE_SAME(reference_sum, result, name);
                t.nextLap();
            }
            std::cout << name << ":     " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
            std::cout << name << ":     " << (n/1000.0/1000.0) / t.lapAvg() << " millions/s" << std::endl;
        }
    }

}