// Fill out your copyright notice in the Description page of Project Settings.

#include "Tasks_System_Example.h"
#include "Tasks/Task.h"
#include "Tasks/Pipe.h"
#include "Tasks/TaskConcurrencyLimiter.h"

ATasks_System_Example::ATasks_System_Example()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATasks_System_Example::BeginPlay()
{
	Super::BeginPlay();
	
	UE_LOG(LogTemp, Warning, TEXT("========== Tasks System Examples Start =========="));
	
	// 运行各种示例
	Example_BasicLaunch();
	Example_TaskWithResult();
	Example_Prerequisites();
	Example_TaskEvent();
	Example_NestedTasks();
	Example_Pipe();
	Example_WaitMultipleTasks();
	Example_CancellationToken();
	Example_TaskPriority();
	Example_MakeCompletedTask();
	
	UE_LOG(LogTemp, Warning, TEXT("========== Advanced Examples =========="));
	
	Example_FireAndForget_MutableLambda_Reset();
	Example_AccessTaskFromInside();
	Example_IsAwaitable();
	Example_PipeAsAsyncClass();
	Example_PipeSuspension();
	Example_NamedThreadTask();
	Example_TaskPriorityCVar();
	Example_TaskConcurrencyLimiter();
	Example_PipedPrereqAndMoveOnlyResult();
	Example_DeepRetraction_WaitTimeout();
	
	UE_LOG(LogTemp, Warning, TEXT("========== Tasks System Examples End =========="));
}

void ATasks_System_Example::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// ============================================================================
// 示例1: 基础任务启动 (Launch)
// ============================================================================
void ATasks_System_Example::Example_BasicLaunch()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 1] Basic Task Launch"));
	
	/*
	 * UE::Tasks::Launch() 是任务系统的核心入口:
	 * 
	 * 函数签名 (Task.h 265-278行):
	 *   template<typename TaskBodyType>
	 *   TTask<TInvokeResult_T<TaskBodyType>> Launch(
	 *       const TCHAR* DebugName,
	 *       TaskBodyType&& TaskBody,
	 *       ETaskPriority Priority = ETaskPriority::Normal,
	 *       EExtendedTaskPriority ExtendedPriority = EExtendedTaskPriority::None,
	 *       ETaskFlags Flags = ETaskFlags::None
	 *   );
	 *
	 * 参数说明:
	 *   - DebugName: 调试名称,用于调试器和性能分析器中标识任务
	 *     推荐使用 UE_SOURCE_LOCATION 宏自动生成
	 *   - TaskBody: 可调用对象(Lambda/函数指针/Functor), 将被异步执行
	 *   - Priority: 任务优先级, 影响调度顺序 (ETaskPriority枚举)
	 *   - ExtendedPriority: 扩展优先级, 用于内联执行或命名线程
	 *   - Flags: 任务标志, 如 DoNotRunInsideBusyWait
	 *
	 * 返回值:
	 *   TTask<ResultType> - 任务句柄, 可用于等待完成或获取结果
	 *   FTask 是 TTask<void> 的别名 (Task.h 333行):
	 *     using FTask = Private::FTaskHandle;
	 *
	 * 内部流程 (Task.h 110-126行):
	 *   1. 创建 TExecutableTask 对象 (存储任务体)
	 *   2. 赋值给 Pimpl (在Launch前,以支持任务内部访问自身)
	 *   3. 调用 TryLaunch 尝试调度执行
	 *
	 * TTask 有效性检查 (Task.h 62-71行):
	 *   - IsValid(): 任务句柄是否引用了一个任务
	 *   - IsCompleted(): 任务执行是否完成
	 *     默认构造的任务句柄为"空",IsValid()返回false
	 *     IsCompleted()在无效时也返回true(空任务视为已完成)
	 */
	
	// 最简形式: 启动一个无返回值的任务
	UE::Tasks::FTask Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [Task] Hello from Tasks System!"));
	});
	
	// 检查任务有效性
	check(Task.IsValid());
	
	// 等待任务完成 (会尝试撤回任务就地执行, 若失败则阻塞)
	Task.Wait();
	
	check(Task.IsCompleted());
	UE_LOG(LogTemp, Log, TEXT("  Task completed"));
	
	// 默认构造的任务句柄
	UE::Tasks::FTask EmptyTask;
	check(!EmptyTask.IsValid());
	check(EmptyTask.IsCompleted());  // 空任务视为已完成
}

// ============================================================================
// 示例2: 带返回值的任务
// ============================================================================
void ATasks_System_Example::Example_TaskWithResult()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 2] Task With Result"));
	
	/*
	 * TTask<ResultType> 泛型任务句柄 (Task.h 189-210行):
	 *
	 * template<typename ResultType>
	 * class TTask : public Private::FTaskHandle
	 * {
	 * public:
	 *     TTask() = default;
	 *
	 *     // 等待任务完成并返回结果
	 *     ResultType& GetResult()
	 *     {
	 *         check(IsValid());
	 *         FTaskHandle::Wait();
	 *         return static_cast<Private::TTaskWithResult<ResultType>*>(
	 *             Pimpl.GetReference())->GetResult();
	 *     }
	 * };
	 *
	 * 返回类型自动推断:
	 *   Launch 使用 TInvokeResult_T<TaskBodyType> 自动推断
	 *   Lambda 返回 int32 → TTask<int32>
	 *   Lambda 返回 void  → TTask<void> (即 FTask)
	 *
	 * 结果存储 (TaskPrivate.h 833-857行):
	 *   TTaskWithResult 使用 TTypeCompatibleBytes<ResultType> 存储结果
	 *   结果在 ExecuteTask() 中通过 placement new 构造
	 */
	
	// Lambda返回int32, 自动推断为 TTask<int32>
	UE::Tasks::TTask<int32> IntTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []() -> int32
	{
		int32 Sum = 0;
		for (int32 i = 1; i <= 100; ++i)
		{
			Sum += i;
		}
		return Sum;
	});
	
	// GetResult() 会先Wait(), 再返回结果引用
	int32 Result = IntTask.GetResult();
	UE_LOG(LogTemp, Log, TEXT("  Sum 1..100 = %d"), Result);
	
	// 返回FString的任务
	UE::Tasks::TTask<FString> StrTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []() -> FString
	{
		return FString::Printf(TEXT("Task Result: %d"), 42);
	});
	
	FString StrResult = StrTask.GetResult();
	UE_LOG(LogTemp, Log, TEXT("  %s"), *StrResult);
}

// ============================================================================
// 示例3: 任务先决条件 (Prerequisites)
// ============================================================================
void ATasks_System_Example::Example_Prerequisites()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 3] Task Prerequisites"));
	
	/*
	 * 带先决条件的 Launch (Task.h 288-302行):
	 *
	 *   template<typename TaskBodyType, typename PrerequisitesCollectionType>
	 *   TTask<TInvokeResult_T<TaskBodyType>> Launch(
	 *       const TCHAR* DebugName,
	 *       TaskBodyType&& TaskBody,
	 *       PrerequisitesCollectionType&& Prerequisites,  // 先决条件集合
	 *       ETaskPriority Priority = ETaskPriority::Normal,
	 *       ...
	 *   );
	 *
	 * Prerequisites 参数:
	 *   接受任何可迭代集合 (.begin()/.end()), 推荐使用:
	 *   - UE::Tasks::Prerequisites(Task1, Task2, ...) 辅助函数
	 *     返回 TStaticArray<FTaskBase*, N> (Task.h 363-370行)
	 *   - TArray<FTask> 容器
	 *
	 * 先决条件机制 (TaskPrivate.h 223-246行):
	 *   AddPrerequisites() 将当前任务注册为先决条件的后续任务(Subsequent)
	 *   - 先增加 NumLocks 计数 (假设添加成功)
	 *   - 调用 Prerequisite.AddSubsequent(*this)
	 *   - 若先决条件已完成(AddSubsequent返回false), 则回退NumLocks
	 *   - 先决条件完成时调用后续任务的 TryUnlock
	 *   - 所有锁都释放后任务才会被调度执行
	 *
	 * 执行流程:
	 *
	 *   TaskA.Launch()         TaskB.Launch(Prerequisites={TaskA})
	 *        |                              |
	 *   调度执行                    AddPrerequisites(TaskA)
	 *        |                              |
	 *   执行任务体                  NumLocks += 1 (等待TaskA)
	 *        |                              |
	 *   完成 → Close()                      |
	 *        |                              |
	 *   遍历Subsequents            TaskA完成 → TryUnlock(TaskB)
	 *        |                              |
	 *   TaskB.TryUnlock()          NumLocks -= 1 → 变为0
	 *                                       |
	 *                              调度TaskB执行
	 */
	
	// 创建第一个任务
	UE::Tasks::FTask TaskA = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [TaskA] Executing first..."));
		FPlatformProcess::Sleep(0.01f);
		UE_LOG(LogTemp, Log, TEXT("  [TaskA] Done"));
	});
	
	// 创建依赖TaskA的第二个任务 - 使用 Prerequisites() 辅助函数
	UE::Tasks::FTask TaskB = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [TaskB] Executing after TaskA..."));
	},
	UE::Tasks::Prerequisites(TaskA));  // TaskA必须先完成
	
	// 创建依赖TaskB的第三个任务
	UE::Tasks::FTask TaskC = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [TaskC] Executing after TaskB..."));
	},
	UE::Tasks::Prerequisites(TaskB));
	
	// 等待最终任务
	TaskC.Wait();
	UE_LOG(LogTemp, Log, TEXT("  All prerequisite tasks completed"));
	
	// 也可以使用 TArray<FTask> 作为先决条件
	UE::Tasks::FTask IndependentA = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [IndependentA] Running..."));
	});
	
	UE::Tasks::FTask IndependentB = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [IndependentB] Running..."));
	});
	
	// 使用TArray作为先决条件集合
	TArray<UE::Tasks::FTask> PrereqTasks;
	PrereqTasks.Add(IndependentA);
	PrereqTasks.Add(IndependentB);
	
	UE::Tasks::FTask FinalTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [FinalTask] Both A and B completed!"));
	},
	PrereqTasks);
	
	FinalTask.Wait();
}

// ============================================================================
// 示例4: FTaskEvent 同步原语
// ============================================================================
void ATasks_System_Example::Example_TaskEvent()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 4] FTaskEvent - Synchronization Primitive"));
	
	/*
	 * FTaskEvent (Task.h 233-257行):
	 *
	 * 作为 FEvent 在任务系统中的推荐替代品
	 * 当用作任务先决条件或嵌套任务时, 不会阻塞工作线程
	 *
	 * class FTaskEvent : public Private::FTaskHandle
	 * {
	 * public:
	 *     explicit FTaskEvent(const TCHAR* DebugName);
	 *
	 *     // 添加先决条件, 必须在Trigger()之前调用
	 *     template<typename PrerequisitesType>
	 *     void AddPrerequisites(const PrerequisitesType& Prerequisites);
	 *
	 *     void Trigger();
	 * };
	 *
	 * 内部实现 (TaskPrivate.h 963-986行):
	 *   FTaskEventBase 继承自 FTaskBase
	 *   - 没有任务体 (ExecuteTask 中 checkNoEntry)
	 *   - 使用 EExtendedTaskPriority::TaskEvent 优先级
	 *   - Trigger() 调用 FTaskBase::Trigger() 
	 *     → TryLaunch → TryUnlock → 直接Close (无需调度执行)
	 *   - 使用专用分配器 FTaskEventBaseAllocator (无锁固定大小)
	 *
	 * 与 FEvent 的区别:
	 *   FEvent:
	 *   - 基于OS信号量/条件变量
	 *   - Wait()会阻塞线程
	 *   - 不与任务系统集成
	 *
	 *   FTaskEvent:
	 *   - 任务系统原生支持
	 *   - 可作为任务的先决条件 (不阻塞工作线程)
	 *   - 可用作嵌套任务 (父任务不阻塞)
	 *   - 支持多次Trigger (幂等)
	 */
	
	// 创建一个任务事件
	UE::Tasks::FTaskEvent Event(UE_SOURCE_LOCATION);
	
	// 创建依赖该事件的任务
	UE::Tasks::FTask WaitingTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [WaitingTask] Event was triggered, now executing!"));
	},
	UE::Tasks::Prerequisites(Event));
	
	// 事件尚未触发, 任务不会执行
	UE_LOG(LogTemp, Log, TEXT("  Event not triggered yet, task is waiting..."));
	FPlatformProcess::Sleep(0.01f);
	
	// 触发事件 - 解锁等待的任务
	Event.Trigger();
	UE_LOG(LogTemp, Log, TEXT("  Event triggered!"));
	
	WaitingTask.Wait();
	
	// FTaskEvent 可以安全地多次 Trigger
	Event.Trigger();  // 第二次Trigger, 幂等操作
	
	// FTaskEvent 也可以有自己的先决条件
	UE::Tasks::FTask SetupTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [SetupTask] Preparing data..."));
		FPlatformProcess::Sleep(0.01f);
	});
	
	UE::Tasks::FTaskEvent DataReady(UE_SOURCE_LOCATION);
	DataReady.AddPrerequisites(UE::Tasks::Prerequisites(SetupTask));
	
	UE::Tasks::FTask ProcessTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [ProcessTask] Processing data after event..."));
	},
	UE::Tasks::Prerequisites(DataReady));
	
	// SetupTask完成后手动触发事件
	DataReady.Trigger();
	ProcessTask.Wait();
}

// ============================================================================
// 示例5: 嵌套任务 (Nested Tasks)
// ============================================================================
void ATasks_System_Example::Example_NestedTasks()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 5] Nested Tasks"));
	
	/*
	 * UE::Tasks::AddNested() (Task.h 504-513行):
	 *
	 * 将嵌套任务添加到当前正在执行的父任务中
	 * 父任务在所有嵌套任务完成之前不会被标记为已完成
	 *
	 * template<typename TaskType>
	 * void AddNested(const TaskType& Nested)
	 * {
	 *     Private::FTaskBase* Parent = Private::GetCurrentTask();
	 *     check(Parent != nullptr);  // 必须在任务执行上下文中调用
	 *     Parent->AddNested(*Nested.Pimpl);
	 * }
	 *
	 * 内部机制 (TaskPrivate.h 444-462行):
	 *   void FTaskBase::AddNested(FTaskBase& Nested)
	 *   {
	 *     // 增加NumLocks计数 (阻止父任务完成)
	 *     uint32 PrevNumLocks = NumLocks.fetch_add(1, ...);
	 *     // 检查必须在执行阶段 (ExecutionFlag已设置)
	 *     checkf(PrevNumLocks > ExecutionFlag, ...);
	 *     // 将嵌套任务注册为当前任务的后续
	 *     if (Nested.AddSubsequent(*this))
	 *     {
	 *         Nested.AddRef();
	 *         Prerequisites.Push(&Nested);
	 *     }
	 *     else // 嵌套任务已完成
	 *     {
	 *         NumLocks.fetch_sub(1, ...);
	 *     }
	 *   }
	 *
	 * 与显式Wait的区别:
	 *
	 *   显式Wait (阻塞工作线程):
	 *     Launch([] {
	 *         FTask SubTask = Launch([] { ... });
	 *         SubTask.Wait();  // 阻塞! 工作线程被占用
	 *     });
	 *
	 *   嵌套任务 (非阻塞):
	 *     Launch([] {
	 *         FTask SubTask = Launch([] { ... });
	 *         AddNested(SubTask);  // 非阻塞! 工作线程可以执行其他任务
	 *     });
	 *
	 * 完成流程:
	 *   1. 父任务执行完毕 → NumLocks = ExecutionFlag + 嵌套任务数
	 *   2. 父任务执行结束时 NumLocks -= 1
	 *   3. 若仍有嵌套任务未完成, 父任务不关闭
	 *   4. 每个嵌套任务完成时 TryUnlock 父任务
	 *   5. 最后一个嵌套任务完成 → NumLocks == ExecutionFlag → Close()
	 */
	
	UE::Tasks::FTask ParentTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [Parent] Starting execution..."));
		
		// 在父任务中启动子任务
		UE::Tasks::FTask ChildA = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
		{
			FPlatformProcess::Sleep(0.02f);
			UE_LOG(LogTemp, Log, TEXT("  [ChildA] Completed"));
		});
		
		UE::Tasks::FTask ChildB = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
		{
			FPlatformProcess::Sleep(0.01f);
			UE_LOG(LogTemp, Log, TEXT("  [ChildB] Completed"));
		});
		
		// 添加为嵌套任务 - 父任务不会在子任务完成前标记为完成
		// 且不会阻塞当前工作线程
		UE::Tasks::AddNested(ChildA);
		UE::Tasks::AddNested(ChildB);
		
		UE_LOG(LogTemp, Log, TEXT("  [Parent] Execution finished, but waiting for children..."));
		// 父任务体执行完毕, 但ParentTask.IsCompleted()直到子任务完成才返回true
	});
	
	// 等待父任务(会等待所有嵌套子任务)
	ParentTask.Wait();
	UE_LOG(LogTemp, Log, TEXT("  Parent and all nested tasks completed"));
}

// ============================================================================
// 示例6: FPipe 管道串行执行
// ============================================================================
void ATasks_System_Example::Example_Pipe()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 6] FPipe - Serial Task Execution"));
	
	/*
	 * FPipe (Pipe.h 28-148行):
	 *
	 * 任务管道, 保证管道中的任务串行(非并发)执行
	 * 可作为命名线程(Named Threads)的轻量级替代
	 *
	 * class FPipe
	 * {
	 * public:
	 *     explicit FPipe(const TCHAR* InDebugName);
	 *     ~FPipe();  // check(!HasWork());
	 *
	 *     // 检查管道是否有未完成的任务
	 *     bool HasWork() const;
	 *
	 *     // 等待管道清空
	 *     bool WaitUntilEmpty(FTimespan Timeout = FTimespan::MaxValue());
	 *
	 *     // 在管道中启动任务 (有/无先决条件两个重载)
	 *     template<typename TaskBodyType>
	 *     TTask<...> Launch(const TCHAR* InDebugName, TaskBodyType&& TaskBody, ...);
	 *
	 *     // 检查当前线程是否在执行管道中的任务
	 *     bool IsInContext() const;
	 * };
	 *
	 * 关键特性:
	 *   - FIFO顺序执行 (对无先决条件的任务, 执行顺序与Launch顺序一致)
	 *   - 非并发: 管道中的任务不会同时执行, 可安全访问共享资源
	 *   - 不一定在同一线程: 不同任务可能在不同工作线程上执行
	 *   - 轻量级: 可以创建大量动态管道实例
	 *
	 * 内部实现:
	 *   - 通过原子链表 std::atomic<FTaskBase*> LastTask 管理任务链
	 *   - PushIntoPipe(): 将新任务添加为前一个任务的后续
	 *   - 原子计数 TaskCount 跟踪未完成任务数
	 *   - WaitUntilEmpty 使用 FEventCount 等待
	 *
	 * 管道中任务的执行流程:
	 *
	 *   Pipe.Launch(Task1)    Pipe.Launch(Task2)    Pipe.Launch(Task3)
	 *         |                     |                     |
	 *   LastTask = Task1      PushIntoPipe:          PushIntoPipe:
	 *         |               Task1→后续=Task2       Task2→后续=Task3
	 *         |               LastTask = Task2       LastTask = Task3
	 *         |                     |                     |
	 *   Task1执行              Task1完成              Task2完成
	 *   Task1完成 →           → Task2解锁执行       → Task3解锁执行
	 *
	 * 注意: 管道必须在最后一个任务完成前保持存活!
	 *   ~FPipe() { check(!HasWork()); }
	 */
	
	// 创建管道
	UE::Tasks::FPipe Pipe(UE_SOURCE_LOCATION);
	
	int32 SharedCounter = 0;
	
	// 在管道中启动多个任务 - 保证串行执行, 不需要锁
	UE::Tasks::FTask PipeTask1 = Pipe.Launch(UE_SOURCE_LOCATION, [&SharedCounter]()
	{
		// 安全地访问SharedCounter, 因为管道保证非并发
		SharedCounter += 10;
		UE_LOG(LogTemp, Log, TEXT("  [Pipe Task 1] Counter = %d"), SharedCounter);
	});
	
	UE::Tasks::FTask PipeTask2 = Pipe.Launch(UE_SOURCE_LOCATION, [&SharedCounter]()
	{
		SharedCounter += 20;
		UE_LOG(LogTemp, Log, TEXT("  [Pipe Task 2] Counter = %d"), SharedCounter);
	});
	
	UE::Tasks::FTask PipeTask3 = Pipe.Launch(UE_SOURCE_LOCATION, [&SharedCounter]()
	{
		SharedCounter += 30;
		UE_LOG(LogTemp, Log, TEXT("  [Pipe Task 3] Counter = %d"), SharedCounter);
	});
	
	// 等待管道清空
	Pipe.WaitUntilEmpty();
	
	UE_LOG(LogTemp, Log, TEXT("  Final counter: %d"), SharedCounter);  // 60
	check(SharedCounter == 60);
	
	// 管道中的任务也可以有先决条件
	UE::Tasks::FPipe Pipe2(UE_SOURCE_LOCATION);
	
	UE::Tasks::FTask PrereqTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [PrereqTask] Prerequisite done"));
	});
	
	// 带先决条件的管道任务
	UE::Tasks::FTask PipedWithPrereq = Pipe2.Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [PipedWithPrereq] After prereq, in pipe"));
	},
	UE::Tasks::Prerequisites(PrereqTask));
	
	Pipe2.WaitUntilEmpty();
}

// ============================================================================
// 示例7: 等待多个任务 (Wait / WaitAny / Any)
// ============================================================================
void ATasks_System_Example::Example_WaitMultipleTasks()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 7] Wait / WaitAny / Any"));
	
	/*
	 * 多任务等待函数:
	 *
	 * 1. UE::Tasks::Wait() (Task.h 380-393行):
	 *    等待所有任务完成 (带超时)
	 *    
	 *    template<typename TaskCollectionType>
	 *    bool Wait(const TaskCollectionType& Tasks, FTimespan InTimeout = FTimespan::MaxValue());
	 *
	 *    内部实现: 创建一个以所有任务为先决条件的内联任务, 等待该任务完成
	 *    return Launch(TEXT("Waiting Task"), [](){}, 
	 *        Prerequisites(Tasks), ..., EExtendedTaskPriority::Inline).Wait(InTimeout);
	 *
	 * 2. UE::Tasks::WaitAny() (Task.h 402-450行):
	 *    阻塞直到任意一个任务完成, 返回完成任务的索引
	 *
	 *    template<typename TaskCollectionType>
	 *    int32 WaitAny(const TaskCollectionType& Tasks, FTimespan Timeout = FTimespan::MaxValue());
	 *
	 *    返回: 第一个完成的任务索引, 超时返回 INDEX_NONE
	 *    内部实现: 
	 *    - 先检查是否有已完成的任务 (快速路径)
	 *    - 为每个任务创建一个后续内联任务
	 *    - 使用 FManualResetEvent + 共享数据等待第一个触发
	 *
	 * 3. UE::Tasks::Any() (Task.h 453-502行):
	 *    返回一个在任意输入任务完成时即完成的FTask
	 *
	 *    template<typename TaskCollectionType>
	 *    FTask Any(const TaskCollectionType& Tasks);
	 *
	 *    内部实现:
	 *    - 创建 FTaskEvent 作为结果
	 *    - 为每个任务创建后续内联任务
	 *    - 第一个完成的任务触发 FTaskEvent
	 *    - 使用引用计数管理共享数据生命周期
	 */
	
	// 创建多个任务
	TArray<UE::Tasks::FTask> Tasks;
	
	Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		FPlatformProcess::Sleep(0.03f);
		UE_LOG(LogTemp, Log, TEXT("  [Task 0] Completed (30ms)"));
	}));
	
	Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		FPlatformProcess::Sleep(0.01f);
		UE_LOG(LogTemp, Log, TEXT("  [Task 1] Completed (10ms)"));
	}));
	
	Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		FPlatformProcess::Sleep(0.02f);
		UE_LOG(LogTemp, Log, TEXT("  [Task 2] Completed (20ms)"));
	}));
	
	// WaitAny: 等待任意一个完成
	int32 CompletedIndex = UE::Tasks::WaitAny(Tasks);
	UE_LOG(LogTemp, Log, TEXT("  First completed task index: %d"), CompletedIndex);
	
	// Wait: 等待所有完成
	UE::Tasks::Wait(Tasks);
	UE_LOG(LogTemp, Log, TEXT("  All tasks completed"));
	
	// Any: 返回一个在任意任务完成时完成的FTask
	TArray<UE::Tasks::FTask> MoreTasks;
	MoreTasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		FPlatformProcess::Sleep(0.02f);
	}));
	MoreTasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		FPlatformProcess::Sleep(0.01f);
	}));
	
	UE::Tasks::FTask AnyTask = UE::Tasks::Any(MoreTasks);
	AnyTask.Wait();
	UE_LOG(LogTemp, Log, TEXT("  Any() task completed (at least one sub-task done)"));
	
	// 清理: 等待所有剩余任务
	UE::Tasks::Wait(MoreTasks);
}

// ============================================================================
// 示例8: 任务取消 (FCancellationToken)
// ============================================================================
void ATasks_System_Example::Example_CancellationToken()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 8] FCancellationToken - Task Cancellation"));
	
	/*
	 * FCancellationToken (Task.h 569-587行):
	 *
	 * 支持任务执行中途取消的协作式取消机制
	 *
	 * class FCancellationToken
	 * {
	 * public:
	 *     UE_NONCOPYABLE(FCancellationToken);
	 *     FCancellationToken() = default;
	 *
	 *     void Cancel()
	 *     {
	 *         bCanceled.store(true, std::memory_order_relaxed);
	 *     }
	 *
	 *     bool IsCanceled() const
	 *     {
	 *         return bCanceled.load(std::memory_order_relaxed);
	 *     }
	 *
	 * private:
	 *     std::atomic<bool> bCanceled{ false };
	 * };
	 *
	 * 使用规则:
	 *   1. 由用户负责管理 CancellationToken 的生命周期
	 *   2. 由用户负责在任务体中检查取消状态
	 *   3. 由用户负责提前返回和做必要的清理
	 *   4. 不能完全跳过任务执行 (任务仍会被调度)
	 *   5. 等待已取消的任务仍会阻塞直到任务完成
	 *   6. 取消一个任务不影响其后续任务 (除非共享同一个Token)
	 *
	 * FCancellationTokenScope (Task.h 589-610行):
	 *   RAII作用域, 将CancellationToken设为当前线程的活跃Token
	 *   static bool IsCurrentWorkCanceled(); // 检查当前工作是否被取消
	 */
	
	UE::Tasks::FCancellationToken Token;
	
	// 启动一个可取消的长时间任务
	UE::Tasks::FTask CancellableTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Token]()
	{
		for (int32 i = 0; i < 100; ++i)
		{
			// 检查取消标志
			if (Token.IsCanceled())
			{
				UE_LOG(LogTemp, Log, TEXT("  [Task] Canceled at iteration %d"), i);
				return;  // 提前退出
			}
			
			// 模拟工作
			FPlatformProcess::Sleep(0.001f);
		}
		UE_LOG(LogTemp, Log, TEXT("  [Task] Completed all iterations"));
	});
	
	// 短暂延迟后取消
	FPlatformProcess::Sleep(0.01f);
	Token.Cancel();
	UE_LOG(LogTemp, Log, TEXT("  Cancel signal sent"));
	
	// 等待任务完成(任务会在检测到取消后提前退出)
	CancellableTask.Wait();
	UE_LOG(LogTemp, Log, TEXT("  Cancellable task finished"));
}

// ============================================================================
// 示例9: 任务优先级
// ============================================================================
void ATasks_System_Example::Example_TaskPriority()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 9] Task Priority"));
	
	/*
	 * 任务优先级体系:
	 *
	 * 1. ETaskPriority (LowLevelTasks - Async/Fundamental/Task.h 17-28行):
	 *    enum class ETaskPriority : int8
	 *    {
	 *        High,             // 高优先级前台任务
	 *        Normal,           // 普通优先级前台任务
	 *        Default = Normal,
	 *        ForegroundCount,
	 *        BackgroundHigh = ForegroundCount,   // 高优先级后台任务
	 *        BackgroundNormal,                   // 普通优先级后台任务
	 *        BackgroundLow,                      // 低优先级后台任务
	 *        Count,
	 *        Inherit,          // 继承父任务的优先级
	 *    };
	 *
	 * 2. EExtendedTaskPriority (TaskPrivate.h 59-83行):
	 *    enum class EExtendedTaskPriority : int8
	 *    {
	 *        None,                     // 普通调度
	 *        Inline,                   // 内联执行 (解锁线程直接执行, 不调度)
	 *        TaskEvent,                // 任务事件优先级
	 *        GameThreadNormalPri,      // 游戏线程普通优先级
	 *        GameThreadHiPri,          // 游戏线程高优先级
	 *        RenderThreadNormalPri,    // 渲染线程普通优先级
	 *        RHIThreadNormalPri,       // RHI线程普通优先级
	 *        ...
	 *    };
	 *
	 * 3. ETaskFlags (TaskPrivate.h 88-92行):
	 *    enum class ETaskFlags
	 *    {
	 *        None,
	 *        DoNotRunInsideBusyWait   // 不在忙等待期间拾取此任务
	 *    };
	 *
	 * 优先级继承 (LowLevelTasks - Async/Fundamental/Task.h 453-472行):
	 *   如果 Priority == ETaskPriority::Inherit:
	 *   - 从当前执行的父任务继承优先级
	 *   - 如果没有父任务, 使用 ETaskPriority::Default
	 */
	
	// 高优先级任务
	UE::Tasks::FTask HighPriTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [High Priority] Executing"));
	},
	LowLevelTasks::ETaskPriority::High);
	
	// 后台低优先级任务
	UE::Tasks::FTask BgTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [Background Low] Executing"));
	},
	LowLevelTasks::ETaskPriority::BackgroundLow);
	
	// 普通优先级任务 (默认)
	UE::Tasks::FTask NormalTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [Normal Priority] Executing"));
	});
	// ETaskPriority::Normal 是默认值, 无需显式指定
	
	HighPriTask.Wait();
	BgTask.Wait();
	NormalTask.Wait();
	
	UE_LOG(LogTemp, Log, TEXT("  All priority tasks completed"));
}

// ============================================================================
// 示例10: MakeCompletedTask 立即完成的任务
// ============================================================================
void ATasks_System_Example::Example_MakeCompletedTask()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 10] MakeCompletedTask"));
	
	/*
	 * MakeCompletedTask (Task.h 548-556行):
	 *
	 * 创建一个已经完成的、携带结果值的任务
	 *
	 * template<typename ResultType, typename... ArgTypes>
	 * TTask<ResultType> MakeCompletedTask(ArgTypes&&... Args)
	 * {
	 *     return Launch(
	 *         UE_SOURCE_LOCATION,
	 *         [&] { return ResultType(Forward<ArgTypes>(Args)...); },
	 *         ETaskPriority::Default,
	 *         EExtendedTaskPriority::Inline);  // Inline表示立即执行
	 * }
	 *
	 * 用途:
	 *   - 当需要返回一个"已完成"的任务时 (如缓存命中, 同步代码路径)
	 *   - 简化需要统一返回 TTask 的接口
	 *   - 类似 MakeFulfilledPromise 的作用
	 *
	 * 内部通过 EExtendedTaskPriority::Inline 实现:
	 *   Inline任务不会被调度到工作线程, 而是由解锁它的线程直接执行
	 *   (TaskPrivate.h TryUnlock中的逻辑)
	 */
	
	// 创建一个已完成的int32任务
	UE::Tasks::TTask<int32> CompletedIntTask = UE::Tasks::MakeCompletedTask<int32>(42);
	
	// 立即可用, 无需等待
	check(CompletedIntTask.IsCompleted());
	int32 Value = CompletedIntTask.GetResult();
	UE_LOG(LogTemp, Log, TEXT("  Completed task result: %d"), Value);
	
	// 创建一个已完成的FString任务
	UE::Tasks::TTask<FString> CompletedStrTask = UE::Tasks::MakeCompletedTask<FString>(TEXT("Immediate Result"));
	
	check(CompletedStrTask.IsCompleted());
	FString StrValue = CompletedStrTask.GetResult();
	UE_LOG(LogTemp, Log, TEXT("  Completed task result: %s"), *StrValue);
	
	// 可用作其他任务的先决条件 (立即满足)
	UE::Tasks::FTask FollowUp = UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
	{
		UE_LOG(LogTemp, Log, TEXT("  [FollowUp] Executed after completed task"));
	},
	UE::Tasks::Prerequisites(CompletedIntTask));
	
	FollowUp.Wait();
}

// ============================================================================
// 示例11: Fire-and-Forget / Mutable Lambda / 任务句柄重置
// ============================================================================
void ATasks_System_Example::Example_FireAndForget_MutableLambda_Reset()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 11] Fire-and-Forget / Mutable Lambda / Task Handle Reset"));
	
	/*
	 * 1. Fire-and-Forget (TasksTest.cpp 186-194行):
	 *    启动任务时不保留句柄, 任务会自动完成并释放
	 *    TExecutableTask 使用侵入式引用计数, 即使外部无引用也会在完成后正确析构
	 *
	 * 2. Mutable Lambda (TasksTest.cpp 196-199行):
	 *    Launch 支持 mutable lambda (可修改捕获的变量副本)
	 *    源码 (Task.h 265行):
	 *      template<typename TaskBodyType>
	 *      TTask<TInvokeResult_T<TaskBodyType>> Launch(..., TaskBodyType&& TaskBody, ...);
	 *    TaskBodyType 为万能引用, 完美转发支持任何可调用对象
	 *
	 * 3. 句柄重置 (TasksTest.cpp 201-205行):
	 *    通过 Task = {} 重置任务句柄, 释放对底层任务对象的引用
	 *    对于作为成员变量持有的任务句柄, 这可以提前释放内存
	 *    内部: Pimpl (TRefCountPtr<FTaskBase>) 被置空 → 引用计数-1
	 */
	
	// --- Fire-and-Forget ---
	// 不保留任务句柄, 任务仍会执行
	std::atomic<bool> bFireAndForgetDone{false};
	UE::Tasks::Launch(UE_SOURCE_LOCATION, [&bFireAndForgetDone]
	{
		bFireAndForgetDone = true;
	});
	// 等待任务完成 (因为没有句柄, 用原子变量轮询)
	while (!bFireAndForgetDone)
	{
		FPlatformProcess::Yield();
	}
	UE_LOG(LogTemp, Log, TEXT("  Fire-and-forget task completed"));
	
	// --- Mutable Lambda ---
	// mutable lambda 可以修改按值捕获的变量副本
	int32 CapturedValue = 10;
	UE::Tasks::Launch(UE_SOURCE_LOCATION, [CapturedValue]() mutable
	{
		CapturedValue += 5;  // 修改副本, 不影响外部
		UE_LOG(LogTemp, Log, TEXT("  Mutable lambda: CapturedValue = %d"), CapturedValue);
	}).Wait();
	
	// 带返回值的 mutable lambda
	bool bResult = UE::Tasks::Launch(UE_SOURCE_LOCATION, []() mutable { return false; }).GetResult();
	UE_LOG(LogTemp, Log, TEXT("  Mutable lambda with result: %s"), bResult ? TEXT("true") : TEXT("false"));
	
	// --- 任务句柄重置 ---
	UE::Tasks::FTask Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [] {});
	Task.Wait();
	check(Task.IsValid());
	
	// 重置句柄, 释放对任务对象的引用
	Task = {};
	check(!Task.IsValid());
	check(Task.IsCompleted());  // 无效句柄视为已完成
	UE_LOG(LogTemp, Log, TEXT("  Task handle reset: IsValid=%s, IsCompleted=%s"),
		Task.IsValid() ? TEXT("true") : TEXT("false"),
		Task.IsCompleted() ? TEXT("true") : TEXT("false"));
}

// ============================================================================
// 示例12: 任务内部访问自身 (Task.Launch 成员函数)
// ============================================================================
void ATasks_System_Example::Example_AccessTaskFromInside()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 12] Accessing Task from Inside Its Execution"));
	
	/*
	 * FTaskHandle::Launch 成员函数 (Task.h 110-126行):
	 *
	 * template<typename TaskBodyType>
	 * void Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, ...)
	 * {
	 *     check(!IsValid());
	 *     using FExecutableTask = Private::TExecutableTask<std::decay_t<TaskBodyType>>;
	 *     FExecutableTask* Task = FExecutableTask::Create(...);
	 *     *Pimpl.GetInitReference() = Task;  // 在Launch前赋值!
	 *     Task->TryLaunch(sizeof(*Task));
	 * }
	 *
	 * 关键: Pimpl在TryLaunch之前就已赋值, 因此任务体可以通过引用访问自身句柄
	 *
	 * TasksTest.cpp 207-211行:
	 *   FTask Task;
	 *   Task.Launch(UE_SOURCE_LOCATION, [&Task] { check(!Task.IsCompleted()); });
	 *   Task.Wait();
	 *
	 * 注意: 这使用的是 FTaskHandle::Launch 成员函数, 不是 UE::Tasks::Launch 自由函数
	 *       自由函数内部也调用此成员函数, 但返回的是临时对象, 无法在lambda中引用
	 */
	
	UE::Tasks::FTask Task;
	Task.Launch(UE_SOURCE_LOCATION, [&Task]
	{
		// 此时任务正在执行, IsCompleted() 应该为 false
		check(!Task.IsCompleted());
		check(Task.IsValid());
		UE_LOG(LogTemp, Log, TEXT("  Inside task: IsCompleted=%s"), Task.IsCompleted() ? TEXT("true") : TEXT("false"));
	});
	Task.Wait();
	UE_LOG(LogTemp, Log, TEXT("  After wait: IsCompleted=%s"), Task.IsCompleted() ? TEXT("true") : TEXT("false"));
}

// ============================================================================
// 示例13: IsAwaitable() 死锁检测
// ============================================================================
void ATasks_System_Example::Example_IsAwaitable()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 13] IsAwaitable() - Deadlock Detection"));
	
	/*
	 * FTaskHandle::IsAwaitable() (Task.h 83-101行):
	 *
	 * 检查在当前上下文中等待此任务是否安全 (是否会导致死锁)
	 *
	 * bool IsAwaitable() const
	 * {
	 *     if (!IsValid()) return true;
	 *     return Pimpl->IsAwaitable();
	 * }
	 *
	 * FTaskBase::IsAwaitable (TaskPrivate.h):
	 *   检查当前执行的任务是否就是自身 (包括嵌套的内联任务)
	 *   - 在任务自身内部调用 Task.Wait() 会死锁
	 *   - 在任务的内联子任务内部调用父任务的 Wait() 也会死锁
	 *   - IsAwaitable() 返回 false 表示等待会导致死锁
	 *
	 * TasksTest.cpp 331-361行:
	 *   1. 基本场景: 在任务内部 Task.IsAwaitable() 返回 false
	 *   2. 内联子任务场景: 外层任务启动 Inline 内层任务,
	 *      内层任务中 Outer.IsAwaitable() 仍返回 false
	 */
	
	// 场景1: 任务内部检查自身 IsAwaitable
	{
		UE::Tasks::FTask Task;
		Task.Launch(UE_SOURCE_LOCATION, [&Task]
		{
			// 在任务自身执行上下文中, 等待自身会死锁
			bool bAwaitable = Task.IsAwaitable();
			check(!bAwaitable);
			UE_LOG(LogTemp, Log, TEXT("  [Inside task] IsAwaitable = %s (expected false)"),
				bAwaitable ? TEXT("true") : TEXT("false"));
		});
		
		// 在外部, 等待该任务是安全的
		check(Task.IsAwaitable());
		Task.Wait();
	}
	
	// 场景2: 内联子任务中检查外层任务的 IsAwaitable
	{
		UE::Tasks::FTask Outer;
		Outer.Launch(UE_SOURCE_LOCATION, [&Outer]
		{
			// 内联子任务 (同步执行在同一线程)
			UE::Tasks::FTask Inner = UE::Tasks::Launch(
				UE_SOURCE_LOCATION,
				[&Outer]
				{
					// 虽然是"内层"任务, 但因为是 Inline 执行
					// 仍在 Outer 的执行上下文中
					check(!Outer.IsAwaitable());
					UE_LOG(LogTemp, Log, TEXT("  [Inline inner] Outer.IsAwaitable = false (correct)"));
				},
				LowLevelTasks::ETaskPriority::Default,
				UE::Tasks::EExtendedTaskPriority::Inline
			);
			check(Inner.IsCompleted());
		});
		Outer.Wait();
	}
	
	UE_LOG(LogTemp, Log, TEXT("  IsAwaitable checks passed"));
}

// ============================================================================
// 示例14: Pipe 作为异步类 ("Primitive Actor" 模式)
// ============================================================================
void ATasks_System_Example::Example_PipeAsAsyncClass()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 14] Pipe as Async Class (Primitive Actor Pattern)"));
	
	/*
	 * 管道可以用来构建线程安全的异步类接口 (TasksTest.cpp 482-508行)
	 *
	 * 设计模式:
	 *   class FAsyncClass
	 *   {
	 *       TTask<bool> DoSomething()
	 *       {
	 *           return Pipe.Launch(TEXT("DoSomething()"), [this] { return DoSomethingImpl(); });
	 *       }
	 *       FPipe Pipe{ UE_SOURCE_LOCATION };
	 *   };
	 *
	 * 所有方法都通过管道调度, 保证:
	 *   1. 内部状态不会被并发访问 (无需锁)
	 *   2. 多线程可安全调用实例的公开方法
	 *   3. 执行顺序与调用顺序一致 (FIFO)
	 *
	 * 类似于 Actor Model (如 Erlang/Akka):
	 *   - 每个实例有自己的"邮箱" (Pipe)
	 *   - 消息按顺序处理
	 *   - 内部状态天然线程安全
	 */
	
	class FAsyncCounter
	{
	public:
		UE::Tasks::TTask<int32> Add(int32 Value)
		{
			return Pipe.Launch(TEXT("Add"), [this, Value]() -> int32
			{
				Counter += Value;
				return Counter;
			});
		}
		
		UE::Tasks::TTask<int32> GetValue()
		{
			return Pipe.Launch(TEXT("GetValue"), [this]() -> int32
			{
				return Counter;
			});
		}
		
		void WaitForEmpty()
		{
			Pipe.WaitUntilEmpty();
		}
		
	private:
		UE::Tasks::FPipe Pipe{UE_SOURCE_LOCATION};
		int32 Counter = 0;
	};
	
	FAsyncCounter AsyncCounter;
	
	// 可从多线程安全调用
	UE::Tasks::FTask Caller1 = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&AsyncCounter]
	{
		AsyncCounter.Add(10);
		AsyncCounter.Add(20);
	});
	
	UE::Tasks::FTask Caller2 = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&AsyncCounter]
	{
		AsyncCounter.Add(30);
	});
	
	Caller1.Wait();
	Caller2.Wait();
	AsyncCounter.WaitForEmpty();
	
	int32 FinalValue = AsyncCounter.GetValue().GetResult();
	UE_LOG(LogTemp, Log, TEXT("  AsyncCounter final value: %d (expected 60)"), FinalValue);
	check(FinalValue == 60);
	AsyncCounter.WaitForEmpty();
}

// ============================================================================
// 示例15: FPipeSuspensionScope (Pipe 挂起/恢复)
// ============================================================================
void ATasks_System_Example::Example_PipeSuspension()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 15] Pipe Suspension (FPipeSuspensionScope Pattern)"));
	
	/*
	 * FPipeSuspensionScope (TasksTest.cpp 294-329行):
	 *
	 * 一种挂起管道执行的模式:
	 *   1. 在管道中启动一个任务
	 *   2. 该任务用 AddNested(ResumeSignal) 将自身"挂起"
	 *   3. 触发 SuspendSignal 通知外部"已挂起"
	 *   4. 析构时触发 ResumeSignal, 恢复管道执行
	 *
	 * struct FPipeSuspensionScope
	 * {
	 *     explicit FPipeSuspensionScope(FPipe& Pipe)
	 *     {
	 *         Pipe.Launch(UE_SOURCE_LOCATION,
	 *             [this]
	 *             {
	 *                 AddNested(ResumeSignal);   // 挂起: 任务不完成直到ResumeSignal触发
	 *                 SuspendSignal.Trigger();   // 通知外部已挂起
	 *             }
	 *         );
	 *         SuspendSignal.Wait();  // 等待挂起完成
	 *     }
	 *
	 *     ~FPipeSuspensionScope()
	 *     {
	 *         ResumeSignal.Trigger();  // 恢复管道
	 *     }
	 *
	 *     FTaskEvent SuspendSignal{ UE_SOURCE_LOCATION };
	 *     FTaskEvent ResumeSignal{ UE_SOURCE_LOCATION };
	 * };
	 *
	 * 用途:
	 *   - 原子性地暂停管道, 在不丢失任务的情况下进行外部操作
	 *   - 管道中后续任务会排队等待, 不会丢失
	 *   - RAII 保证恢复
	 */
	
	struct FPipeSuspensionScope
	{
		explicit FPipeSuspensionScope(UE::Tasks::FPipe& InPipe)
		{
			InPipe.Launch(UE_SOURCE_LOCATION, [this]
			{
				UE::Tasks::AddNested(ResumeSignal);
				SuspendSignal.Trigger();
			});
			SuspendSignal.Wait();
		}
		
		~FPipeSuspensionScope()
		{
			ResumeSignal.Trigger();
		}
		
		UE::Tasks::FTaskEvent SuspendSignal{UE_SOURCE_LOCATION};
		UE::Tasks::FTaskEvent ResumeSignal{UE_SOURCE_LOCATION};
	};
	
	UE::Tasks::FPipe Pipe{UE_SOURCE_LOCATION};
	UE::Tasks::FTask Task;
	{
		// 挂起管道
		FPipeSuspensionScope Suspension(Pipe);
		
		// 此时管道被挂起, 新任务排队但不执行
		Task = Pipe.Launch(UE_SOURCE_LOCATION, []
		{
			UE_LOG(LogTemp, Log, TEXT("  [Suspended pipe task] Executed after resume"));
		});
		
		// 验证任务确实被挂起 (带超时的Wait返回false)
		bool bCompleted = Task.Wait(FTimespan::FromMilliseconds(100));
		UE_LOG(LogTemp, Log, TEXT("  Task completed during suspension: %s (expected false)"),
			bCompleted ? TEXT("true") : TEXT("false"));
		
	}  // FPipeSuspensionScope 析构, 触发 ResumeSignal, 管道恢复
	
	Task.Wait();
	Pipe.WaitUntilEmpty();
	UE_LOG(LogTemp, Log, TEXT("  Pipe suspension/resume completed"));
}

// ============================================================================
// 示例16: 命名线程任务 (Named Thread Task)
// ============================================================================
void ATasks_System_Example::Example_NamedThreadTask()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 16] Named Thread Task"));
	
	/*
	 * EExtendedTaskPriority 命名线程选项 (TaskPrivate.h 59-83行):
	 *
	 *   GameThreadNormalPri,    // 游戏线程普通优先级
	 *   GameThreadHiPri,        // 游戏线程高优先级
	 *   RenderThreadNormalPri,  // 渲染线程普通优先级
	 *   RHIThreadNormalPri,     // RHI线程普通优先级
	 *
	 * TasksTest.cpp 368-377行:
	 *   FTask GTTask = Launch(
	 *       UE_SOURCE_LOCATION,
	 *       [] { check(IsInGameThread()); },
	 *       ETaskPriority::Default,
	 *       EExtendedTaskPriority::GameThreadNormalPri
	 *   );
	 *   GTTask.Wait();
	 *
	 * 命名线程任务不会在工作线程上执行, 而是会被发送到指定的线程
	 * 等待时需要注意: 如果当前就在目标线程, 会直接执行;
	 * 否则需要目标线程处理其任务队列时才执行
	 *
	 * 用途:
	 *   - 需要在GameThread上操作UObject/蓝图数据
	 *   - 需要在RenderThread上操作渲染资源
	 */
	
	// 启动一个在游戏线程执行的任务
	UE::Tasks::FTask GTTask = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[]
		{
			check(IsInGameThread());
			UE_LOG(LogTemp, Log, TEXT("  [GameThread Task] Executing on GameThread: %s"),
				IsInGameThread() ? TEXT("true") : TEXT("false"));
		},
		LowLevelTasks::ETaskPriority::Default,
		UE::Tasks::EExtendedTaskPriority::GameThreadNormalPri
	);
	
	GTTask.Wait();
	UE_LOG(LogTemp, Log, TEXT("  Named thread task completed"));
}

// ============================================================================
// 示例17: FTaskPriorityCVar (控制台变量配置优先级)
// ============================================================================
void ATasks_System_Example::Example_TaskPriorityCVar()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 17] FTaskPriorityCVar - Console Variable Priority"));
	
	/*
	 * FTaskPriorityCVar (Task.h 515-545行):
	 *
	 * 允许通过控制台变量在运行时配置任务优先级
	 *
	 * class FTaskPriorityCVar
	 * {
	 * public:
	 *     FTaskPriorityCVar(const TCHAR* Name, const TCHAR* Help,
	 *         ETaskPriority DefaultPriority, EExtendedTaskPriority DefaultExtendedPriority);
	 *
	 *     ETaskPriority GetTaskPriority() const { return Priority; }
	 *     EExtendedTaskPriority GetExtendedTaskPriority() const { return ExtendedPriority; }
	 * };
	 *
	 * 内部通过 FAutoConsoleVariableRef 注册到控制台系统
	 * 值格式: "TaskPriorityName [ExtendedPriorityName]"
	 * 例如: "High", "Normal Inline", "BackgroundLow None"
	 *
	 * TasksTest.cpp 1368-1401行:
	 *   FTaskPriorityCVar CVar{ TEXT("CVarName"), TEXT("Help"), ETaskPriority::Normal, EExtendedTaskPriority::None };
	 *   // 可通过控制台命令修改: CVarName "High"
	 *   Launch(UE_SOURCE_LOCATION, [] {}, CVar.GetTaskPriority(), CVar.GetExtendedTaskPriority()).Wait();
	 */
	
	// 创建一个可通过控制台配置的任务优先级
	// 注意: CVar名称必须唯一, 此处使用带前缀的名称避免冲突
	static UE::Tasks::FTaskPriorityCVar ExampleCVar{
		TEXT("TasksExample.Priority"),
		TEXT("Example task priority for demonstration"),
		LowLevelTasks::ETaskPriority::Normal,
		UE::Tasks::EExtendedTaskPriority::None
	};
	
	// 使用CVar的优先级启动任务
	UE::Tasks::FTask Task = UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[]
		{
			UE_LOG(LogTemp, Log, TEXT("  [CVar Task] Executing with CVar-configured priority"));
		},
		ExampleCVar.GetTaskPriority(),
		ExampleCVar.GetExtendedTaskPriority()
	);
	Task.Wait();
	
	UE_LOG(LogTemp, Log, TEXT("  CVar task completed (can change priority at runtime via console: TasksExample.Priority \"High\")"));
}

// ============================================================================
// 示例18: FTaskConcurrencyLimiter (并发限制器)
// ============================================================================
void ATasks_System_Example::Example_TaskConcurrencyLimiter()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 18] FTaskConcurrencyLimiter - Concurrency Limiter"));
	
	/*
	 * FTaskConcurrencyLimiter (TaskConcurrencyLimiter.h):
	 *
	 * 限制同时执行的任务数量的轻量级构造
	 *
	 * class FTaskConcurrencyLimiter
	 * {
	 * public:
	 *     explicit FTaskConcurrencyLimiter(uint32 MaxConcurrency,
	 *         ETaskPriority TaskPriority = ETaskPriority::Default);
	 *
	 *     // 推入一个任务, TaskFunction 接受一个 uint32 Slot 参数
	 *     // Slot 是 [0..MaxConcurrency) 范围内的唯一索引
	 *     template<typename TaskFunctionType>
	 *     void Push(const TCHAR* DebugName, TaskFunctionType&& TaskFunction);
	 *
	 *     // 等待所有任务完成
	 *     bool Wait(FTimespan Timeout = FTimespan::MaxValue());
	 * };
	 *
	 * Slot 参数:
	 *   - 每个并发执行的任务获得一个唯一的Slot索引
	 *   - 可用于索引固定大小的缓冲区, 无需额外同步
	 *   - 任务完成后Slot被回收, 供下一个任务使用
	 *
	 * TasksTest.cpp 1561-1632行:
	 *   FTaskConcurrencyLimiter Limiter(MaxConcurrency);
	 *   Limiter.Push(UE_SOURCE_LOCATION, [](uint32 Slot) { ... });
	 *   Limiter.Wait();
	 *
	 * 用途:
	 *   - 限制IO并发度 (如同时最多4个文件读取)
	 *   - 限制GPU提交并发度
	 *   - 资源池管理
	 */
	
	constexpr uint32 MaxConcurrency = 2;
	constexpr uint32 TotalItems = 8;
	
	std::atomic<uint32> CurrentConcurrency{0};
	std::atomic<uint32> MaxObserved{0};
	std::atomic<uint32> Processed{0};
	
	UE::Tasks::FTaskConcurrencyLimiter Limiter(MaxConcurrency);
	
	for (uint32 i = 0; i < TotalItems; ++i)
	{
		Limiter.Push(UE_SOURCE_LOCATION,
			[&CurrentConcurrency, &MaxObserved, &Processed, MaxConcurrency](uint32 Slot)
			{
				// Slot 是 [0, MaxConcurrency) 范围内的唯一索引
				check(Slot < MaxConcurrency);
				
				uint32 Current = CurrentConcurrency.fetch_add(1) + 1;
				check(Current <= MaxConcurrency);
				
				// 记录观察到的最大并发数
				uint32 PrevMax = MaxObserved.load();
				while (PrevMax < Current && !MaxObserved.compare_exchange_weak(PrevMax, Current)) {}
				
				FPlatformProcess::Sleep(0.005f);  // 模拟工作
				
				CurrentConcurrency.fetch_sub(1);
				Processed.fetch_add(1);
			}
		);
	}
	
	Limiter.Wait();
	
	UE_LOG(LogTemp, Log, TEXT("  Processed: %d/%d, MaxConcurrency observed: %d (limit: %d)"),
		Processed.load(), TotalItems, MaxObserved.load(), MaxConcurrency);
	check(Processed.load() == TotalItems);
	check(MaxObserved.load() <= MaxConcurrency);
}

// ============================================================================
// 示例19: 带先决条件的管道任务不阻塞管道 / Move-Only 结果类型
// ============================================================================
void ATasks_System_Example::Example_PipedPrereqAndMoveOnlyResult()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 19] Piped Task with Prerequisites / Move-Only Result"));
	
	/*
	 * 1. 带先决条件的管道任务不阻塞管道 (TasksTest.cpp 918-933行):
	 *
	 *   FPipe Pipe{ UE_SOURCE_LOCATION };
	 *   FTaskEvent Prereq{ UE_SOURCE_LOCATION };
	 *   FTask Task1{ Pipe.Launch(UE_SOURCE_LOCATION, [] {}, Prereq) };  // 被先决条件阻塞
	 *   FTask Task2{ Pipe.Launch(UE_SOURCE_LOCATION, [] {}) };          // 不被阻塞!
	 *   Task2.Wait();  // Task2 可以先完成
	 *
	 * 管道中被先决条件阻塞的任务不会阻塞整个管道
	 * 因为管道通过 PushIntoPipe 管理任务链, 而先决条件是独立的锁
	 * 当先决条件未满足时, 任务不参与管道的串行链
	 *
	 * 2. Move-Only 结果类型 (TasksTest.cpp 130-183行):
	 *
	 *   支持仅可移动不可复制的返回值类型 (如 TUniquePtr)
	 *   结果存储 (TaskPrivate.h):
	 *     TTypeCompatibleBytes<ResultType> ResultStorage;
	 *     使用 placement new 构造, 支持 move-only 类型
	 */
	
	// --- 管道任务先决条件不阻塞管道 ---
	{
		UE::Tasks::FPipe Pipe{UE_SOURCE_LOCATION};
		UE::Tasks::FTaskEvent Prereq{UE_SOURCE_LOCATION};
		
		// Task1 被先决条件阻塞
		UE::Tasks::FTask Task1 = Pipe.Launch(UE_SOURCE_LOCATION, []
		{
			UE_LOG(LogTemp, Log, TEXT("  [Piped Task1] Executed after prereq"));
		}, UE::Tasks::Prerequisites(Prereq));
		
		// Task1 未完成, 但管道不被阻塞
		FPlatformProcess::Sleep(0.01f);
		check(!Task1.IsCompleted());
		
		// Task2 可以在 Task1 之前完成!
		UE::Tasks::FTask Task2 = Pipe.Launch(UE_SOURCE_LOCATION, []
		{
			UE_LOG(LogTemp, Log, TEXT("  [Piped Task2] Executed before Task1!"));
		});
		Task2.Wait();
		UE_LOG(LogTemp, Log, TEXT("  Task2 completed while Task1 still blocked by prereq"));
		
		// 现在释放先决条件
		Prereq.Trigger();
		Task1.Wait();
		Pipe.WaitUntilEmpty();
	}
	
	// --- Move-Only 结果类型 ---
	{
		// TUniquePtr 是 move-only 类型
		UE::Tasks::TTask<TUniquePtr<int32>> Task = UE::Tasks::Launch(UE_SOURCE_LOCATION,
			[]() -> TUniquePtr<int32>
			{
				return MakeUnique<int32>(42);
			}
		);
		
		TUniquePtr<int32> Result = MoveTemp(Task.GetResult());
		check(Result.IsValid());
		check(*Result == 42);
		UE_LOG(LogTemp, Log, TEXT("  Move-only result: %d"), *Result);
	}
}

// ============================================================================
// 示例20: 深度撤回 (Deep Retraction) / Wait 带超时
// ============================================================================
void ATasks_System_Example::Example_DeepRetraction_WaitTimeout()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 20] Deep Retraction / Wait with Timeout"));
	
	/*
	 * 1. Wait 带超时 (Task.h 72-78行):
	 *
	 *   bool Wait(FTimespan Timeout) const
	 *   {
	 *       return !IsValid() || Pimpl->Wait(FTimeout{ Timeout });
	 *   }
	 *
	 *   返回 true: 任务在超时前完成
	 *   返回 false: 超时, 任务仍未完成
	 *
	 *   整个测试文件中大量使用:
	 *     verify(!Task.Wait(FTimespan::FromMilliseconds(100)));  // 验证任务确实被阻塞
	 *     verify(Task.Wait(FTimespan::Zero()));                  // 验证任务已完成
	 *
	 * 2. 深度撤回 (Deep Retraction) (TasksTest.cpp 1012-1037行):
	 *
	 *   当所有工作线程都被阻塞时, Wait() 会执行"深度撤回":
	 *   - 不只是撤回目标任务, 还会递归地撤回其先决条件和嵌套任务
	 *   - 在当前线程上按正确顺序执行整个任务链
	 *
	 *   void TwoLevelsDeepRetractionTest():
	 *     P11, P12 → P21  (两级先决条件)
	 *                P22
	 *     Task (依赖P21, P22):
	 *       N11 → N21, N22  (两级嵌套任务)
	 *       N12
	 *
	 *   Task.Wait() 时, 调度器会:
	 *     1. 发现Task有先决条件P21, P22
	 *     2. 递归撤回P21的先决条件P11, P12
	 *     3. 依次执行 P11 → P12 → P21 → P22 → Task
	 *     4. Task体创建嵌套任务N11, N12
	 *     5. 递归撤回N11的嵌套任务N21, N22
	 *     6. 依次执行所有嵌套任务
	 *
	 *   这保证了即使所有工作线程都忙, 程序也不会死锁
	 */
	
	// --- Wait 带超时 ---
	{
		UE::Tasks::FTaskEvent Blocker{UE_SOURCE_LOCATION};
		
		UE::Tasks::FTask Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [] {},
			UE::Tasks::Prerequisites(Blocker));
		
		// 超时等待: 100ms 内任务不会完成 (因为 Blocker 未触发)
		bool bCompleted = Task.Wait(FTimespan::FromMilliseconds(100));
		UE_LOG(LogTemp, Log, TEXT("  Wait with timeout: completed=%s (expected false)"),
			bCompleted ? TEXT("true") : TEXT("false"));
		check(!bCompleted);
		
		// 触发先决条件
		Blocker.Trigger();
		
		// 现在等待应该成功
		bCompleted = Task.Wait(FTimespan::FromMilliseconds(100));
		check(bCompleted);
		UE_LOG(LogTemp, Log, TEXT("  After trigger: completed=%s (expected true)"),
			bCompleted ? TEXT("true") : TEXT("false"));
	}
	
	// --- 深度撤回 (简化版, 两级先决条件 + 嵌套任务) ---
	{
		// 两级先决条件
		UE::Tasks::FTask P11 = UE::Tasks::Launch(TEXT("P11"), [] {});
		UE::Tasks::FTask P12 = UE::Tasks::Launch(TEXT("P12"), [] {});
		UE::Tasks::FTask P21 = UE::Tasks::Launch(TEXT("P21"), [] {},
			UE::Tasks::Prerequisites(P11, P12));
		UE::Tasks::FTask P22 = UE::Tasks::Launch(TEXT("P22"), [] {});
		
		// 带嵌套任务的主任务
		UE::Tasks::FTask N11, N12;
		UE::Tasks::FTask Task = UE::Tasks::Launch(UE_SOURCE_LOCATION,
			[&N11, &N12]
			{
				N11 = UE::Tasks::Launch(TEXT("N11"), [] {});
				N12 = UE::Tasks::Launch(TEXT("N12"), [] {});
				UE::Tasks::AddNested(N11);
				UE::Tasks::AddNested(N12);
			},
			UE::Tasks::Prerequisites(P21, P22)
		);
		
		// Wait 会递归地撤回先决条件和嵌套任务
		Task.Wait();
		
		// 验证所有任务都已完成
		check(P11.IsCompleted() && P12.IsCompleted());
		check(P21.IsCompleted() && P22.IsCompleted());
		check(N11.IsCompleted() && N12.IsCompleted());
		check(Task.IsCompleted());
		
		UE_LOG(LogTemp, Log, TEXT("  Deep retraction: all prerequisites and nested tasks completed"));
	}
	
	// --- FTaskEvent.AddPrerequisites 合并大量任务 ---
	{
		// 使用 FTaskEvent.AddPrerequisites 作为多任务合并点
		// (TasksTest.cpp DependenciesPerfTest, 789-816行)
		constexpr int32 NumTasks = 10;
		TArray<UE::Tasks::FTask> Tasks;
		Tasks.Reserve(NumTasks);
		
		for (int32 i = 0; i < NumTasks; ++i)
		{
			Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [] {}));
		}
		
		// FTaskEvent 作为合并点
		UE::Tasks::FTaskEvent Joiner{UE_SOURCE_LOCATION};
		Joiner.AddPrerequisites(Tasks);
		Joiner.Trigger();
		
		Joiner.Wait();
		UE_LOG(LogTemp, Log, TEXT("  FTaskEvent as joiner: all %d tasks joined"), NumTasks);
	}
}
