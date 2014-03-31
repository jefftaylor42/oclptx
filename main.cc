/* Copyright 2014
 *  Afshin Haidari
 *  Steve Novakov
 *  Jeff Taylor
 */

#include <unistd.h>
#include <cassert>
#include <thread>
#include <chrono>

#include "fifo.h"
#include "oclenv.h"
#include "oclptxhandler.h"
#include "samplemanager.h"
#include "threading.h"


cl_ulong8 rng_zero = {0,};

int main(int argc, char **argv)
{
  const int kStepsPerKernel = 10;
  const int kNumReducers = 1;
  FILE *global_fd;
  Fifo<struct OclPtxHandler::particle_data> *particles_fifo;

  // Create our oclenv
  OclEnv env();
  env.OclInit();
  env.NewCLCommandQueues();

  // Startup the samplemanager
  SampleManager *sample_manager = &SampleManager::GetInstance();
  sample_manager->ParseCommandLine(argc, argv);
  particles_fifo = sample_manager->GetSeedParticles();

  env.AvailableGPUMem(
    sample_manager->GetFDataPtr(),
    sample_manager->GetOclptxOptions(),
    sample_manager->GetWayMasksToVector()->size(),
    NULL,
    NULL
  );
  env.AllocateSamples(
    sample_manager->GetFDataPtr(),
    sample_manager->GetPhiDataPtr(),
    sample_manager->GetThetaDataPtr(),
    sample_manager->GetBrainMaskToArray(),
    NULL,
    NULL,
    NULL
  );

  env.CreateKernels("standard");

  auto t_end = std::chrono::high_resolution_clock::now();
  auto t_start = std::chrono::high_resolution_clock::now();

  global_fd = fopen("./path_output", "w");
  if (NULL == global_fd)
  {
    perror("Couldn't open file");
    exit(1);
  }

  struct OclPtxHandler::particle_attrs attrs = {
    kStepsPerKernel,
    10, // max_steps
    0, // Particles per side not determined here.
    env.GetEnvData()->nx,
    env.GetEnvData()->ny,
    env.GetEnvData()->nz,
    1, // num_samples
    0.2, // curvature threshold
    env.GetEnvData()->n_waypts,
    sample_manager->GetOclptxOptions().steplength.value(),
    env.GetEnvData()->lx,
    env.GetEnvData()->ly,
    env.GetEnvData()->lz
    }; // num waymasks.
  int num_dev = env.HowManyDevices();

  // Create a new oclptxhandler.
  OclPtxHandler *handler = new OclPtxHandler[num_dev];
  std::thread *gpu_managers[num_dev];

  // may want to change the location of this later
  t_start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_dev; ++i)
  {
    handler[i].Init(env.GetContext(),
                    env.GetCq(i),
                    env.GetKernel(i),
                    env.GetSumKernel(i),
                    &attrs,
                    global_fd,
                    env.GetEnvData());

    gpu_managers[i] = new std::thread(
        threading::RunThreads,
        &handler[i],
        particles_fifo,
        kNumReducers);
  }

  for (int i = 0; i < num_dev; ++i)
  {
    gpu_managers[i]->join();
  }

  t_end = std::chrono::high_resolution_clock::now();
    std::cout<< "Total Tracking Time (ns): " <<
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          t_end-t_start).count() << std::endl;
  delete[] handler;

  fclose(global_fd);

  return 0;
}
