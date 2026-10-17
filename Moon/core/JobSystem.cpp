#include <algorithm>
#include <functional>
#include <string>
#include <array>
#include <map>
#include <unordered_map>
#include <cstddef>
#include <cfloat>
#include <cstring>
#include <utility>
#include <memory>
#include <thread>
#include <math.h>
#include <Windows.h>
#include "JobSystem.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#define NOMINMAX
#include <Windows.h>
#include <comdef.h>
#include "core/log.h"


namespace MOON
{
    namespace System
    {
        class SpinLock
        {
            std::atomic_flag locked = ATOMIC_FLAG_INIT;

        public:
            inline void lock()
            {
              
                int spin = 0;
                while(!TryLock())
                {
                    if(spin < 10)
                    {
                        _mm_pause(); // SMT thread swap can occur here
                    }
                    else
                    {
                        std::this_thread::yield(); // OS thread swap can occur here. It is important to keep it as fallback, to avoid any chance of lockup by busy wait
                    }

                    spin++;
                }
            }

            inline bool TryLock()
            {
                return !locked.test_and_set(std::memory_order_acquire);
            }

            inline void unlock()
            {
                locked.clear(std::memory_order_release);
            }
        };

        namespace JobSystem
        {
            struct Job
            {
                Context* ctx;
                Function<void(JobDispatchArgs)> task;
                uint32_t groupID;
                uint32_t groupJobOffset;
                uint32_t groupJobEnd;
                uint32_t sharedmemory_size;
            };

            struct JobQueue
            {
                std::deque<Job> queue;
                std::mutex locker;

                inline void push_back(const Job& item)
                {
                    std::scoped_lock lock(locker);
                    queue.push_back(item);
                }

                inline bool pop_front(Job& item)
                {
                    std::scoped_lock lock(locker);
                    if(queue.empty())
                    {
                        return false;
                    }
                    item = Move(queue.front());
                    queue.pop_front();
                    return true;
                }
            };

            // This structure is responsible to stop worker thread loops.
            //    Once this is destroyed, worker threads will be woken up and end their loops.
            struct InternalState
            {
                uint32_t numCores   = 0;
                uint32_t numThreads = 0;
                JobQueue* jobQueuePerThread = nullptr;
                std::atomic_bool alive { true };
                std::condition_variable wakeCondition;
                std::mutex wakeMutex;
                std::atomic<uint32_t> nextQueue { 0 };
                std::vector<std::thread> threads;

                ~InternalState()
                {
                    stop();
                    delete[] jobQueuePerThread;
                    jobQueuePerThread = nullptr;
                }

                /** Stops the workers and waits for them, so that nothing below this
                 * point can still be reading the queues.
                 *
                 * The flag is cleared while holding the very mutex the workers sleep on,
                 * and they read it again once they hold that mutex: a worker therefore
                 * either sees the flag before it waits, or is woken out of the wait -
                 * neither of them can slip through the gap between the two and sleep
                 * forever. That gap is what the previous version tried to paper over by
                 * spinning a second thread on notify_all() (with a plain bool the
                 * compiler is free to hoist out of the loop, which turned the join below
                 * into a hang); the threads themselves were detached, so the join loop
                 * never ran and the queues were deleted under working threads.
                 */
                void stop()
                {
                    {
                        std::lock_guard<std::mutex> lock(wakeMutex);
                        alive.store(false); // indicate that new jobs cannot be started from this point
                        wakeCondition.notify_all(); // wakes up sleeping worker threads
                    }
                    for (std::thread& worker : threads)
                    {
                        if (worker.joinable())
                        {
                            worker.join();
                        }
                    }
                    threads.clear();
                }
            };
            static InternalState* internal_state = nullptr;

            // Start working on a job queue
            //    After the job queue is finished, it can switch to an other queue and steal jobs from there
            inline void work(uint32_t startingQueue)
            {
                Job job;
                for(uint32_t i = 0; i < internal_state->numThreads; ++i)
                {
                    JobQueue& job_queue = internal_state->jobQueuePerThread[startingQueue % internal_state->numThreads];
                    while(job_queue.pop_front(job))
                    {
                        JobDispatchArgs args;
                        args.groupID = job.groupID;
                        if(job.sharedmemory_size > 0)
                        {
                            thread_local static std::vector<uint8_t> shared_allocation_data;
                            shared_allocation_data.reserve(job.sharedmemory_size);
                            args.sharedmemory = shared_allocation_data.data();
                        }
                        else
                        {
                            args.sharedmemory = nullptr;
                        }

                        for(uint32_t j = job.groupJobOffset; j < job.groupJobEnd; ++j)
                        {
                            args.jobIndex          = j;
                            args.groupIndex        = j - job.groupJobOffset;
                            args.isFirstJobInGroup = (j == job.groupJobOffset);
                            args.isLastJobInGroup  = (j == job.groupJobEnd - 1);
                            job.task(args);
                        }

                        job.ctx->counter.fetch_sub(1);
                    }
                    startingQueue++; // go to next queue
                }
            }

            void OnInit(uint32_t reservedThreads)
            {
                if(!internal_state)
                    internal_state = new InternalState();

                if(internal_state->numThreads > 0)
                    return;

                // Retrieve the number of hardware threads in this System:
                internal_state->numCores = std::thread::hardware_concurrency();

                // Calculate the actual number of worker threads we want:
                uint32_t va = internal_state->numCores - reservedThreads;

                internal_state->numThreads = va>1?va:1;

                // Keep one for update thread
                internal_state->jobQueuePerThread = new JobQueue[internal_state->numThreads];
                internal_state->threads.reserve(internal_state->numThreads);

                for(uint32_t threadID = 0; threadID < internal_state->numThreads; ++threadID)
                {
                    std::thread& worker = internal_state->threads.emplace_back([threadID]
                                                                              {
                                //ThreadContext& threadContext = *GetThreadContext();
                                //threadContext = ThreadContextAlloc();
                                //String8 name = PushStr8F(threadContext.ScratchArenas[0], "JobSystem_%u", threadID);
                                //LUMOS_PROFILE_SETTHREADNAME((const char*)name.str);
                                //SetThreadName(name);

                                while (internal_state->alive.load())
                                {
                                    work(threadID);

                                    // finished with jobs, put to sleep
                                    std::unique_lock<std::mutex> lock(internal_state->wakeMutex);
                                    // Read the flag again now that the lock is held: stop()
                                    // clears it under this same lock, so a stop that lands
                                    // between the loop condition and this line is still seen
                                    // here - otherwise the worker would sleep through the
                                    // wake-up and the join in stop() would never return.
                                    if (!internal_state->alive.load())
                                    {
                                        break;
                                    }
                                    internal_state->wakeCondition.wait(lock);

                                } });

                                // Do Windows-specific thread setup:
                                HANDLE handle = (HANDLE)worker.native_handle();

                                // Put each thread on to dedicated core
                                DWORD_PTR affinityMask = 1ull << threadID;
                                DWORD_PTR affinity_result = SetThreadAffinityMask(handle, affinityMask);
                                if (!(affinity_result > 0))
                                    __debugbreak();

                                // Increase thread priority:
                                // BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
                                // ASSERT(priority_result != 0, "");

                                // Name the thread:
                                std::wstring wthreadname = L"JobSystem_" + std::to_wstring(threadID);
#if defined _MSC_VER
                                HRESULT hr = SetThreadDescription(handle, wthreadname.c_str());
                                if (!((((HRESULT)(hr)) >= 0)))
                                    __debugbreak();
#endif
                    // Kept joinable on purpose: Release() -> ~InternalState() -> stop()
                    // joins them before the queues they read are deleted. A detached
                    // worker survives that, keeps running against freed memory, and is
                    // one of the reasons the process could hang on the way out.
                }
                CORE_INFO("Initialised JobSystem with [{} cores] [{} threads]", internal_state->numCores, internal_state->numThreads);
            }

            void Release()
            {
                delete internal_state;
                internal_state = nullptr;
            }

            uint32_t GetThreadCount()
            {
                return internal_state->numThreads;
            }

            void Execute(Context& ctx, const Function<void(JobDispatchArgs)>& task)
            {
                
                // Context state is updated:
                ctx.counter.fetch_add(1);

                Job job;
                job.ctx               = &ctx;
                job.task              = task;
                job.groupID           = 0;
                job.groupJobOffset    = 0;
                job.groupJobEnd       = 1;
                job.sharedmemory_size = 0;

                internal_state->jobQueuePerThread[internal_state->nextQueue.fetch_add(1) % internal_state->numThreads].push_back(job);
                internal_state->wakeCondition.notify_one();
            }

            void DelayExecute(const Function<void(JobDispatchArgs)>& task)
            {
				static Context ctx;
                Execute(ctx,task);
            }

            void Dispatch(Context& ctx, uint32_t jobCount, uint32_t groupSize, const Function<void(JobDispatchArgs)>& task, size_t sharedmemory_size)
            {
               
                if(jobCount == 0 || groupSize == 0)
                {
                    return;
                }

                const uint32_t groupCount = DispatchGroupCount(jobCount, groupSize);

                // Context state is updated:
                ctx.counter.fetch_add(groupCount);

                Job job;
                job.ctx               = &ctx;
                job.task              = task;
                job.sharedmemory_size = (uint32_t)sharedmemory_size;

                for(uint32_t groupID = 0; groupID < groupCount; ++groupID)
                {
                    // For each group, generate one real job:
                    job.groupID        = groupID;
                    job.groupJobOffset = groupID * groupSize;
                    job.groupJobEnd    = job.groupJobOffset + groupSize< jobCount?(job.groupJobOffset + groupSize):jobCount;

                    internal_state->jobQueuePerThread[internal_state->nextQueue.fetch_add(1) % internal_state->numThreads].push_back(job);
                }

                internal_state->wakeCondition.notify_one();
            }

            uint32_t DispatchGroupCount(uint32_t jobCount, uint32_t groupSize)
            {
                // Calculate the amount of job groups to dispatch (overestimate, or "ceil"):
                return (jobCount + groupSize - 1) / groupSize;
            }

            bool IsBusy(const Context& ctx)
            {
                // Whenever the main thread label is not reached by the workers, it indicates that some worker is still alive
                return ctx.counter.load() > 0;
            }

            void Wait(const Context& ctx)
            {
               
                if(IsBusy(ctx))
                {
                    // Wake any threads that might be sleeping:
                    internal_state->wakeCondition.notify_all();

                    // work() will pick up any jobs that are on stand by and execute them on this thread:
                    work(internal_state->nextQueue.fetch_add(1) % internal_state->numThreads);

                    while(IsBusy(ctx))
                    {
                        // If we are here, then there are still remaining jobs that work() couldn't pick up.
                        //    In this case those jobs are not standing by on a queue but currently executing
                        //    on other threads, so they cannot be picked up by this thread.
                        //    Allow to swap out this thread by OS to not spin endlessly for nothing
                        std::this_thread::yield();
                    }
                }
            }
        }
    }
}
