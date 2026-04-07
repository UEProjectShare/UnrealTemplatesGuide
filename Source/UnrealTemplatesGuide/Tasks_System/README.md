# UE5 Tasks System (UE::Tasks) 任务系统深度解析

> 源码版本: UE 5.7  
> 源码路径: `Engine/Source/Runtime/Core/Public/Tasks/Task.h`  
> 源码路径: `Engine/Source/Runtime/Core/Public/Tasks/Pipe.h`  
> 源码路径: `Engine/Source/Runtime/Core/Public/Tasks/TaskPrivate.h`  
> 底层任务: `Engine/Source/Runtime/Core/Public/Async/Fundamental/Task.h`

> [示例代码](https://github.com/UEProjectShare/UnrealTemplatesGuide/blob/master/Source/UnrealTemplatesGuide/Tasks_System/Tasks_System_Example.cpp)

---

## 1. 概述与设计理念

`UE::Tasks` 是 Unreal Engine 5 引入的**高层级任务并行系统**，位于 `UE::Tasks` 命名空间中。它构建在底层 `LowLevelTasks` 之上，提供了更易用、更安全的异步任务 API。

### 核心思想

- **任务 (Task)**: 一个可异步执行的工作单元，由 `Launch()` 启动
- **任务句柄 (Task Handle)**: `TTask<ResultType>` / `FTask`，用于等待和获取结果
- **先决条件 (Prerequisites)**: 任务间的依赖关系，确保执行顺序
- **管道 (Pipe)**: 保证串行执行的任务链，替代命名线程

### 与旧版 TaskGraph 的区别

| 特性 | UE::Tasks | TaskGraph |
|------|-----------|-----------|
| API 复杂度 | 简单 (Lambda + Launch) | 复杂 (需继承类) |
| 返回值支持 | `TTask<ResultType>` 自动推断 | 需手动管理 |
| 链式依赖 | `Prerequisites()` 辅助函数 | `FGraphEventRef` |
| 取消支持 | `FCancellationToken` 协作取消 | 无 |
| 串行执行 | `FPipe` | 命名线程 |
| 嵌套任务 | `AddNested()` 非阻塞 | `DontCompleteUntil()` |

### 头文件引用

```cpp
#include "Tasks/Task.h"    // FTask, TTask, Launch, FTaskEvent, Wait, WaitAny, Any, AddNested, MakeCompletedTask, FCancellationToken
#include "Tasks/Pipe.h"    // FPipe
```

---

## 2. 核心类层次结构

### 类继承关系图

```
                LowLevelTasks::FTask (底层任务, 调度器直接操作)
                         |
                         v
          UE::Tasks::Private::FTaskBase (抽象基类, 侵入式引用计数)
                         |
          +--------------+--------------+
          |              |              |
          v              v              v
  TTaskWithResult  TExecutableTaskBase  FTaskEventBase
  <ResultType>     <TaskBodyType>       (无任务体的信号任务)
          |              |
          v              v
       (结果存储)   TExecutableTask
                   (最终派生类, 小任务分配优化)

         高层级API:
  UE::Tasks::Private::FTaskHandle  (Pimpl, 持有 TRefCountPtr<FTaskBase>)
                |
         +------+------+
         |             |
         v             v
   TTask<ResultType>  FTaskEvent
   (泛型任务句柄)    (同步原语)
         |
         v
  FTask = FTaskHandle  (using 别名, Task.h 333行)
```

### 关键类型别名 (Task.h 333行)

```cpp
using FTask = Private::FTaskHandle;
```

`FTask` 是无类型任务句柄，等同于 `TTask<void>` 但两者是不同的类型。

---

## 3. FTaskHandle - 任务句柄基类

`FTaskHandle` 是 `TTask` 和 `FTaskEvent` 的公共基类，实现了 Pimpl 模式。

### 源码定义 (Task.h 29-184行)

```cpp
class FTaskHandle
{
protected:
    explicit FTaskHandle(FTaskBase* Other)
        : Pimpl(Other, /*bAddRef = */false)
    {}

public:
    FTaskHandle() = default;

    bool IsValid() const
    {
        return Pimpl.IsValid();
    }

    // 检查任务是否已完成
    bool IsCompleted() const
    {
        return !IsValid() || Pimpl->IsCompleted();
    }

    // 带超时的等待
    bool Wait(FTimespan Timeout) const
    {
        return !IsValid() || Pimpl->Wait(FTimeout{ Timeout });
    }

    // 无限等待
    bool Wait() const
    {
        if (IsValid())
        {
            Pimpl->Wait();
        }
        return true;
    }

    // 尝试撤回任务并就地执行
    bool TryRetractAndExecute()
    {
        Pimpl->TryRetractAndExecute(FTimeout::Never());
        return IsCompleted();
    }

protected:
    TRefCountPtr<FTaskBase> Pimpl;
};
```

### 关键设计要点

#### 1. IsCompleted() 语义

```cpp
bool IsCompleted() const
{
    return !IsValid() || Pimpl->IsCompleted();
}
```

**重要：** 无效的任务句柄（默认构造或移动后）视为"已完成"。

#### 2. Wait() 与 TryRetractAndExecute()

等待任务时的策略：
1. **TryRetractAndExecute()**: 尝试从调度器中撤回任务，在当前线程就地执行
2. 若撤回失败（任务已在执行），则阻塞等待

这种设计避免了线程浪费——等待线程不是空闲等待，而是尝试帮助执行任务。

---

## 4. TTask\<ResultType\> - 泛型任务句柄

### 源码定义 (Task.h 189-231行)

```cpp
template<typename ResultType>
class TTask : public Private::FTaskHandle
{
public:
    TTask() = default;

    // 等待任务完成并返回结果
    ResultType& GetResult()
    {
        check(IsValid());
        FTaskHandle::Wait();
        return static_cast<Private::TTaskWithResult<ResultType>*>(Pimpl.GetReference())->GetResult();
    }
};

// void特化版本
template<>
class TTask<void> : public Private::FTaskHandle
{
public:
    TTask() = default;

    void GetResult()
    {
        check(IsValid());
        Wait();
    }
};
```

### GetResult() 解析

```cpp
ResultType& GetResult()
{
    check(IsValid());
    FTaskHandle::Wait();  // 先等待完成
    return static_cast<Private::TTaskWithResult<ResultType>*>(
        Pimpl.GetReference())->GetResult();  // 再获取结果
}
```

1. 先调用 `Wait()` 确保任务已完成
2. 将 `Pimpl` 向下转型为 `TTaskWithResult<ResultType>*`
3. 调用其 `GetResult()` 返回结果引用

---

## 5. UE::Tasks::Launch - 启动任务

### 源码定义 (Task.h 265-302行)

```cpp
// 无先决条件版本
template<typename TaskBodyType>
TTask<TInvokeResult_T<TaskBodyType>> Launch(
    const TCHAR* DebugName,
    TaskBodyType&& TaskBody,
    ETaskPriority Priority = ETaskPriority::Normal,
    EExtendedTaskPriority ExtendedPriority = EExtendedTaskPriority::None,
    ETaskFlags Flags = ETaskFlags::None
)
{
    using FResult = TInvokeResult_T<TaskBodyType>;
    TTask<FResult> Task;
    Task.Launch(DebugName, Forward<TaskBodyType>(TaskBody), Priority, ExtendedPriority, Flags);
    return Task;
}

// 带先决条件版本
template<typename TaskBodyType, typename PrerequisitesCollectionType>
TTask<TInvokeResult_T<TaskBodyType>> Launch(
    const TCHAR* DebugName,
    TaskBodyType&& TaskBody,
    PrerequisitesCollectionType&& Prerequisites,
    ETaskPriority Priority = ETaskPriority::Normal,
    EExtendedTaskPriority ExtendedPriority = EExtendedTaskPriority::None,
    ETaskFlags Flags = ETaskFlags::None
);
```

### 内部 FTaskHandle::Launch (Task.h 110-126行)

```cpp
template<typename TaskBodyType>
void Launch(const TCHAR* DebugName, TaskBodyType&& TaskBody, ETaskPriority Priority, ...)
{
    check(!IsValid());

    using FExecutableTask = Private::TExecutableTask<std::decay_t<TaskBodyType>>;
    FExecutableTask* Task = FExecutableTask::Create(DebugName, Forward<TaskBodyType>(TaskBody), Priority, ExtendedPriority, Flags);
    // 在Launch前赋值, 支持任务内部访问自身
    *Pimpl.GetInitReference() = Task;
    Task->TryLaunch(sizeof(*Task));
}
```

### 启动流程

```
UE::Tasks::Launch(DebugName, Lambda, ...)
     |
     v
TExecutableTask::Create()       // 1. 堆上分配任务对象
     |                          //    (小任务使用专用分配器)
     v
Pimpl = Task                    // 2. 赋值给句柄(在Launch前!)
     |
     v
Task->TryLaunch(sizeof(*Task))  // 3. 尝试调度执行
     |
     v
TryUnlock()                     // 4. 减少NumLocks
     |
     +---> NumLocks == 0: Schedule() 提交到调度器
     |
     +---> NumLocks > 0: 有未完成的先决条件, 等待
```

---

## 6. FTaskEvent - 同步原语

### 源码定义 (Task.h 233-257行)

```cpp
class FTaskEvent : public Private::FTaskHandle
{
public:
    explicit FTaskEvent(const TCHAR* DebugName)
        : Private::FTaskHandle(Private::FTaskEventBase::Create(DebugName))
    {
    }

    // 所有先决条件必须在Trigger之前添加
    template<typename PrerequisitesType>
    void AddPrerequisites(const PrerequisitesType& Prerequisites)
    {
        Pimpl->AddPrerequisites(Prerequisites);
    }

    void Trigger()
    {
        if (!IsCompleted())  // 支持多次Trigger(幂等)
        {
            Pimpl->Trigger(sizeof(*Pimpl));
        }
    }
};
```

### FTaskEventBase 内部实现 (TaskPrivate.h 963-999行)

```cpp
class FTaskEventBase : public FTaskBase
{
public:
    static FTaskEventBase* Create(const TCHAR* DebugName)
    {
        return new FTaskEventBase(DebugName);
    }

    // 使用专用的无锁固定大小分配器
    static void* operator new(size_t Size)
    {
        return TaskEventBaseAllocator.Allocate();
    }

    static void operator delete(void* Ptr)
    {
        TaskEventBaseAllocator.Free(Ptr);
    }

private:
    FTaskEventBase(const TCHAR* InDebugName)
        : FTaskBase(1)  // 初始引用计数=1
    {
        Init(InDebugName, ETaskPriority::Normal, EExtendedTaskPriority::TaskEvent, ETaskFlags::None);
    }

    virtual void ExecuteTask() override final
    {
        checkNoEntry();  // 永远不会被执行, 因为没有任务体
    }
};
```

### 与 FEvent 对比

| 特性 | FTaskEvent | FEvent |
|------|-----------|--------|
| 底层实现 | 任务系统原生 | OS信号量/条件变量 |
| 作为先决条件 | 不阻塞工作线程 | 需要阻塞等待 |
| 作为嵌套任务 | 支持 | 不支持 |
| 多次触发 | 幂等 | 取决于类型 |
| 分配方式 | 专用无锁分配器 | 标准分配 |

---

## 7. FPipe - 管道串行执行

### 源码定义 (Pipe.h 28-148行)

```cpp
class FPipe
{
public:
    UE_NONCOPYABLE(FPipe);

    explicit FPipe(const TCHAR* InDebugName)
        : EmptyEventRef(MakeShared<UE::FEventCount>())
        , DebugName(InDebugName)
    {}

    ~FPipe()
    {
        check(!HasWork());
    }

    bool HasWork() const
    {
        return TaskCount.load(std::memory_order_relaxed) != 0;
    }

    bool WaitUntilEmpty(FTimespan Timeout = FTimespan::MaxValue());

    // 在管道中启动任务
    template<typename TaskBodyType>
    TTask<TInvokeResult_T<TaskBodyType>> Launch(
        const TCHAR* InDebugName, 
        TaskBodyType&& TaskBody, 
        ETaskPriority Priority = ETaskPriority::Default, ...)
    {
        using FExecutableTask = Private::TExecutableTask<std::decay_t<TaskBodyType>>;
        FExecutableTask* Task = FExecutableTask::Create(InDebugName, Forward<TaskBodyType>(TaskBody), Priority, ...);
        TaskCount.fetch_add(1, std::memory_order_acq_rel);
        Task->SetPipe(*this);
        Task->TryLaunch(sizeof(*Task));
        return TTask<FResult>{ Task };
    }

    bool IsInContext() const;

private:
    std::atomic<Private::FTaskBase*> LastTask{ nullptr };
    std::atomic<uint64> TaskCount{ 0 };
    TSharedRef<UE::FEventCount> EmptyEventRef;
    const TCHAR* const DebugName;
};
```

### 管道执行机制

```
管道内部: 原子链表 LastTask

Pipe.Launch(Task1):
  LastTask = nullptr → Task1   (无前置, 直接调度)

Pipe.Launch(Task2):
  LastTask = Task1 → Task2     (Task1成为Task2的先决条件)

Pipe.Launch(Task3):
  LastTask = Task2 → Task3     (Task2成为Task3的先决条件)

执行顺序: Task1 → Task2 → Task3 (FIFO串行)
```

### FPipe 使用场景

- **替代命名线程**: 不再需要专用线程来串行处理逻辑
- **共享资源保护**: 管道中的任务不会并发执行，无需加锁
- **动态数量**: 可以创建大量管道实例，每个保护一个资源

---

## 8. 嵌套任务 (Nested Tasks)

### 源码定义 (Task.h 504-513行)

```cpp
template<typename TaskType>
void AddNested(const TaskType& Nested)
{
    Private::FTaskBase* Parent = Private::GetCurrentTask();
    check(Parent != nullptr);  // 必须在任务执行上下文中调用
    Parent->AddNested(*Nested.Pimpl);
}
```

### AddNested 内部机制 (TaskPrivate.h 444-462行)

```cpp
void AddNested(FTaskBase& Nested)
{
    // 增加完成锁计数 (阻止父任务Close)
    uint32 PrevNumLocks = NumLocks.fetch_add(1, std::memory_order_relaxed);
    // 必须在执行阶段
    checkf(PrevNumLocks > ExecutionFlag, TEXT("nested tasks can be added only during parent's execution"));

    if (Nested.AddSubsequent(*this))
    {
        Nested.AddRef();
        Prerequisites.Push(&Nested);  // 存储以支持撤回
    }
    else  // 嵌套任务已完成
    {
        NumLocks.fetch_sub(1, std::memory_order_relaxed);
    }
}
```

### 与显式 Wait 对比

```
显式Wait (阻塞):                   嵌套任务 (非阻塞):
Launch([] {                        Launch([] {
    FTask Sub = Launch([] {...});       FTask Sub = Launch([] {...});
    Sub.Wait();  // 工作线程被占用      AddNested(Sub);  // 工作线程被释放
});                                });
```

### NumLocks 双阶段机制 (TaskPrivate.h)

```
ExecutionFlag = 0x80000000

阶段1 - 执行前 (NumLocks表示执行先决条件数):
  NumLocks = NumInitialLocks(1) + Prerequisites数量
  每个先决条件完成 → NumLocks -= 1
  NumLocks == 0 → 获得执行权限

阶段2 - 执行后 (NumLocks表示完成先决条件数):
  TrySetExecutionFlag: NumLocks = ExecutionFlag + 1
  每个嵌套任务 → NumLocks += 1
  任务体执行完毕 → NumLocks -= 1
  每个嵌套任务完成 → NumLocks -= 1
  NumLocks == ExecutionFlag → Close()
```

---

## 9. 多任务等待

### Wait() (Task.h 380-393行)

```cpp
template<typename TaskCollectionType>
bool Wait(const TaskCollectionType& Tasks, FTimespan InTimeout)
{
    return UE::Tasks::Launch(
        TEXT("Waiting Task"),
        [](){},
        UE::Tasks::Prerequisites(Tasks),
        ETaskPriority::Default,
        EExtendedTaskPriority::Inline  // 内联执行
    ).Wait(InTimeout);
}
```

创建一个以所有任务为先决条件的内联任务，等待该任务完成。

### WaitAny() (Task.h 402-450行)

```cpp
template<typename TaskCollectionType>
int32 WaitAny(const TaskCollectionType& Tasks, FTimespan Timeout)
{
    // 快速路径: 检查已完成的任务
    for (int32 Index = 0; Index < Tasks.Num(); ++Index)
    {
        if (Private::IsCompleted(Tasks[Index]))
            return Index;
    }

    // 慢速路径: 为每个任务创建监听
    struct FSharedData
    {
        UE::FManualResetEvent Event;
        std::atomic<int32> CompletedTaskIndex{ 0 };
    };

    TSharedRef<FSharedData> SharedData = MakeShared<FSharedData>();

    for (int32 Index = 0; Index < Tasks.Num(); ++Index)
    {
        Launch(UE_SOURCE_LOCATION,
            [SharedData, Index]
            {
                SharedData->CompletedTaskIndex.store(Index, std::memory_order_relaxed);
                SharedData->Event.Notify();
            },
            Prerequisites(Tasks[Index]),
            ETaskPriority::Default,
            EExtendedTaskPriority::Inline
        );
    }

    if (SharedData->Event.WaitFor(...))
        return SharedData->CompletedTaskIndex.load(std::memory_order_relaxed);

    return INDEX_NONE;
}
```

### Any() (Task.h 453-502行)

返回一个 `FTask`，在任意输入任务完成时即完成。内部使用 `FTaskEvent` + 引用计数实现。

---

## 10. FCancellationToken - 任务取消

### 源码定义 (Task.h 569-610行)

```cpp
class FCancellationToken
{
public:
    UE_NONCOPYABLE(FCancellationToken);
    FCancellationToken() = default;

    void Cancel()
    {
        bCanceled.store(true, std::memory_order_relaxed);
    }

    bool IsCanceled() const
    {
        return bCanceled.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> bCanceled{ false };
};

class FCancellationTokenScope
{
public:
    FCancellationTokenScope(FCancellationToken& CancellationToken);
    ~FCancellationTokenScope();

    static FCancellationToken* GetCurrentCancellationToken();

    static bool IsCurrentWorkCanceled()
    {
        if (FCancellationToken* Token = GetCurrentCancellationToken())
        {
            return Token->IsCanceled();
        }
        return false;
    }
};
```

### 取消规则

| 规则 | 说明 |
|------|------|
| 协作式取消 | 用户负责在任务体中检查 `IsCanceled()` |
| 不能跳过执行 | 任务仍会被调度，只能提前返回 |
| 等待仍阻塞 | 等待已取消的任务仍会等到任务完成 |
| 不影响后续 | 取消不传播到后续任务（除非共享Token） |

---

## 11. 任务优先级体系

### LowLevelTasks::ETaskPriority (Async/Fundamental/Task.h 17-28行)

```cpp
enum class ETaskPriority : int8
{
    High,                          // 高优先级前台
    Normal,                        // 普通优先级前台
    Default = Normal,
    ForegroundCount,
    BackgroundHigh = ForegroundCount,   // 高优先级后台
    BackgroundNormal,                   // 普通后台
    BackgroundLow,                      // 低优先级后台
    Count,
    Inherit,                       // 继承父任务优先级
};
```

### UE::Tasks::EExtendedTaskPriority (TaskPrivate.h 59-83行)

```cpp
enum class EExtendedTaskPriority : int8
{
    None,                          // 普通调度
    Inline,                        // 内联执行 (不进调度器)
    TaskEvent,                     // 任务事件
    GameThreadNormalPri,           // 游戏线程普通
    GameThreadHiPri,               // 游戏线程高
    RenderThreadNormalPri,         // 渲染线程普通
    RHIThreadNormalPri,            // RHI线程普通
    ...
};
```

### 优先级继承 (Async/Fundamental/Task.h 453-472行)

```cpp
inline void FTask::InheritParentData(ETaskPriority& Priority)
{
    const FTask* LocalActiveTask = FTask::GetActiveTask();
    if (LocalActiveTask != nullptr)
    {
        if (Priority == ETaskPriority::Inherit)
        {
            Priority = LocalActiveTask->GetPriority();
        }
        UserData = LocalActiveTask->GetUserData();
    }
    else
    {
        if (Priority == ETaskPriority::Inherit)
        {
            Priority = ETaskPriority::Default;
        }
        UserData = nullptr;
    }
}
```

---

## 12. 底层实现: FTaskBase

### 核心成员变量 (TaskPrivate.h)

| 成员 | 类型 | 说明 |
|------|------|------|
| `RefCount` | `std::atomic<uint32>` | 侵入式引用计数 |
| `NumLocks` | `std::atomic<uint32>` | 执行/完成锁 (双阶段) |
| `Pipe` | `FPipe*` | 所属管道 |
| `ExtendedPriority` | `EExtendedTaskPriority` | 扩展优先级 |
| `ExecutingThreadId` | `std::atomic<uint32>` | 执行线程ID |
| `Prerequisites` | `FPrerequisites` | 先决条件链表 |
| `Subsequents` | `FSubsequents` | 后续任务链表 |
| `LowLevelTask` | `LowLevelTasks::FTask` | 底层调度任务 |
| `StateChangeEvent` | `FEventCount` | 状态变更事件 |

### 任务生命周期

```
Create (堆分配)
     |
     v
Init (设置DebugName, Priority, 注册LowLevelTask)
     |
     v
AddPrerequisites (可选, 注册依赖)
     |
     v
TryLaunch → TryUnlock
     |
     +---> 有锁: 等待先决条件
     |
     +---> 无锁: Schedule (提交到调度器) 或 Inline执行
                    |
                    v
             TryExecuteTask
                    |
                    v
             ReleasePrerequisites (释放先决条件引用)
                    |
                    v
             ExecuteTask() (执行任务体)
                    |
                    v
             NumLocks -= 1 (任务体执行完毕)
                    |
                    +---> 有嵌套任务: 等待
                    |
                    +---> 无嵌套任务: Close()
                                        |
                                        v
                                   解锁所有后续任务
                                        |
                                        v
                                   清理管道引用
                                        |
                                        v
                                   Release() (释放引用)
                                        |
                                        v
                                   delete (引用归零)
```

---

## 13. 小任务分配优化

### TExecutableTask (TaskPrivate.h 922-955行)

```cpp
inline constexpr int32 SmallTaskSize = 256;
using FExecutableTaskAllocator = TLockFreeFixedSizeAllocator_TLSCache<SmallTaskSize, PLATFORM_CACHE_LINE_SIZE>;

template<typename TaskBodyType>
class alignas(PLATFORM_CACHE_LINE_SIZE) TExecutableTask final : public TExecutableTaskBase<TaskBodyType>
{
public:
    static void* operator new(size_t Size)
    {
        if (Size <= SmallTaskSize)
        {
            return SmallTaskAllocator.Allocate();  // 无锁分配
        }
        else
        {
            return FMemory::Malloc(sizeof(TExecutableTask), alignof(TExecutableTask));
        }
    }

    static void operator delete(void* Ptr, size_t Size)
    {
        Size <= SmallTaskSize ? SmallTaskAllocator.Free(Ptr) : FMemory::Free(Ptr);
    }
};
```

**优化策略：**
- 小于等于 256 字节的任务使用无锁固定大小分配器（TLS 缓存）
- 超过 256 字节的任务使用标准 `FMemory::Malloc`
- `alignas(PLATFORM_CACHE_LINE_SIZE)` 避免伪共享

---

## 14. 底层任务状态机

### LowLevelTasks::ETaskState (Async/Fundamental/Task.h 102-144行)

```
                    STORE(I)                  CAS(C)
(Init)         ──────►│    Ready    │◄──────►│ CanceledAndReady │
                       ──────────────         ──────────────────
                             │OR(L)                  │OR(L)
                             ▼                       ▼
           CAS(E)    ──────────────   CAS(C)  ──────────────────
(Expedite)◄─────────│  Scheduled  │◄──────►│    Canceled      │
           ──────────────────────────        ──────────────────
                             │OR(W)                  │OR(W)
                             ▼                       ▼
                     ──────────────          ──────────────────
                    │   Running    │        │CanceledAndRunning│
                     ──────────────          ──────────────────
                             │OR(W)                  │OR(W)
                             ▼                       ▼
                     ──────────────          ──────────────────
                    │  Completed   │        │CanceledAndCompleted│
                     ──────────────          ──────────────────
```

---

## 15. FTaskPriorityCVar - 运行时优先级配置

### 源码定义 (Task.h 515-545行)

```cpp
class FTaskPriorityCVar
{
public:
    CORE_API FTaskPriorityCVar(const TCHAR* Name, const TCHAR* Help,
        ETaskPriority DefaultPriority, EExtendedTaskPriority DefaultExtendedPriority);

    ETaskPriority GetTaskPriority() const { return Priority; }
    EExtendedTaskPriority GetExtendedTaskPriority() const { return ExtendedPriority; }

private:
    FString RawSetting;
    FString FullHelpText;
    FAutoConsoleVariableRef Variable;
    ETaskPriority Priority;
    EExtendedTaskPriority ExtendedPriority;
};
```

### 使用方式

```cpp
// 注册控制台变量
FTaskPriorityCVar CVar{ TEXT("CVarName"), TEXT("Help"),
    ETaskPriority::Normal, EExtendedTaskPriority::None };

// 使用配置的优先级
Launch(UE_SOURCE_LOCATION, [] {},
    CVar.GetTaskPriority(), CVar.GetExtendedTaskPriority()).Wait();

// 运行时通过控制台修改: CVarName "High Inline"
```

值格式为 `"TaskPriorityName [ExtendedPriorityName]"`，例如 `"High"`, `"Normal Inline"`, `"BackgroundLow None"`。

---

## 16. FTaskConcurrencyLimiter - 并发限制器

### 源码定义 (TaskConcurrencyLimiter.h)

```cpp
class FTaskConcurrencyLimiter
{
public:
    explicit FTaskConcurrencyLimiter(uint32 MaxConcurrency,
        ETaskPriority TaskPriority = ETaskPriority::Default);

    // 推入任务, TaskFunction 签名: void(uint32 Slot)
    // Slot 是 [0..MaxConcurrency) 范围内的唯一索引
    template<typename TaskFunctionType>
    void Push(const TCHAR* DebugName, TaskFunctionType&& TaskFunction);

    // 等待所有任务完成
    bool Wait(FTimespan Timeout = FTimespan::MaxValue());
};
```

### 内部实现

- 使用 `atomic_queue::AtomicQueueB` 实现无锁FIFO工作队列
- 通过 `FConcurrencySlots` 管理并发槽位
- 每个槽位为 `[0, MaxConcurrency)` 范围内的唯一索引
- 任务完成后槽位被回收，供下一个任务使用
- 支持在 `FTaskConcurrencyLimiter` 析构前任务仍在执行（通过 `TSharedFromThis` 延长生命周期）

### Slot 参数用途

```cpp
FTaskConcurrencyLimiter Limiter(4);
int32 Buffer[4] = {};  // 固定大小缓冲区

for (int32 i = 0; i < 100; ++i)
{
    Limiter.Push(UE_SOURCE_LOCATION, [&Buffer](uint32 Slot)
    {
        // Slot 保证在同一时刻唯一, 可安全索引缓冲区
        Buffer[Slot] = ComputeSomething();
    });
}
Limiter.Wait();
```

---

## 17. 进阶模式

### Fire-and-Forget (不保留句柄)

```cpp
// 不保留任务句柄, 任务会自动执行并释放
// TExecutableTask 的侵入式引用计数保证正确析构
Launch(UE_SOURCE_LOCATION, [] { DoWork(); });  // 无返回值, 不关心完成
```

### 任务句柄重置

```cpp
FTask Task = Launch(UE_SOURCE_LOCATION, [] {});
Task.Wait();
Task = {};  // 释放对底层任务对象的引用, 释放内存
// Task.IsValid() == false, Task.IsCompleted() == true
```

### 任务内部访问自身

```cpp
FTask Task;
Task.Launch(UE_SOURCE_LOCATION, [&Task]
{
    check(!Task.IsCompleted());  // 任务正在执行
    // 可以访问 Task 句柄, 因为 Pimpl 在 TryLaunch 前已赋值
});
Task.Wait();
```

关键: `FTaskHandle::Launch` 成员函数在 `TryLaunch` 之前就将 Pimpl 赋值（Task.h 110-126行），因此 Lambda 通过引用捕获的句柄在执行时已有效。

### IsAwaitable() 死锁检测

```cpp
FTask Task;
Task.Launch(UE_SOURCE_LOCATION, [&Task]
{
    check(!Task.IsAwaitable());  // 在任务内部等待自身会死锁
    // Task.Wait() would deadlock here!
});
check(Task.IsAwaitable());  // 在外部等待是安全的
Task.Wait();
```

`IsAwaitable()` 也能检测内联子任务中的间接死锁。

### Pipe 作为异步类 ("Primitive Actor" 模式)

```cpp
class FAsyncClass
{
public:
    TTask<bool> DoSomething()
    {
        return Pipe.Launch(TEXT("DoSomething()"), [this] { return DoSomethingImpl(); });
    }

    FTask DoSomethingElse()
    {
        return Pipe.Launch(TEXT("DoSomethingElse()"), [this] { DoSomethingElseImpl(); });
    }

private:
    bool DoSomethingImpl() { return false; }
    void DoSomethingElseImpl() {}

    FPipe Pipe{ UE_SOURCE_LOCATION };
};
```

所有方法通过管道调度，内部状态天然线程安全，类似 Actor 模型。

### FPipeSuspensionScope (管道挂起/恢复)

```cpp
struct FPipeSuspensionScope
{
    explicit FPipeSuspensionScope(FPipe& Pipe)
    {
        Pipe.Launch(UE_SOURCE_LOCATION, [this]
        {
            AddNested(ResumeSignal);    // 挂起: 任务不完成直到Resume
            SuspendSignal.Trigger();    // 通知外部已挂起
        });
        SuspendSignal.Wait();
    }

    ~FPipeSuspensionScope()
    {
        ResumeSignal.Trigger();  // RAII恢复
    }

    FTaskEvent SuspendSignal{ UE_SOURCE_LOCATION };
    FTaskEvent ResumeSignal{ UE_SOURCE_LOCATION };
};

// 使用
FPipe Pipe{ UE_SOURCE_LOCATION };
{
    FPipeSuspensionScope Suspension(Pipe);
    // 管道已挂起, 新任务排队但不执行
    Pipe.Launch(UE_SOURCE_LOCATION, [] {});  // 不会执行
}  // 析构恢复管道
```

### 带先决条件的管道任务不阻塞管道

```cpp
FPipe Pipe{ UE_SOURCE_LOCATION };
FTaskEvent Prereq{ UE_SOURCE_LOCATION };

FTask Task1 = Pipe.Launch(UE_SOURCE_LOCATION, [] {}, Prereq);  // 被阻塞
FTask Task2 = Pipe.Launch(UE_SOURCE_LOCATION, [] {});           // 不被阻塞!
Task2.Wait();  // Task2 可以先完成

Prereq.Trigger();
Task1.Wait();
Pipe.WaitUntilEmpty();
```

### 深度撤回 (Deep Retraction)

当所有工作线程忙碌时，`Wait()` 会递归撤回先决条件和嵌套任务，在当前线程按正确顺序执行整个任务链，避免死锁。

```
P11, P12 → P21 ─┐
            P22 ─┤
                 └→ Task ─→ N11 → N21, N22
                            N12

Task.Wait() 深度撤回:
  1. 撤回 P11, P12 → 执行
  2. 撤回 P21 → 执行
  3. 撤回 P22 → 执行
  4. 执行 Task → 创建 N11, N12
  5. 撤回 N11 → 执行 → 创建 N21, N22 → 执行
  6. 撤回 N12 → 执行
  7. 全部完成
```

### FTaskEvent.AddPrerequisites 作为合并点

```cpp
TArray<FTask> Tasks;
for (int32 i = 0; i < 100; ++i)
{
    Tasks.Add(Launch(UE_SOURCE_LOCATION, [] {}));
}

FTaskEvent Joiner{ UE_SOURCE_LOCATION };
Joiner.AddPrerequisites(Tasks);
Joiner.Trigger();
Joiner.Wait();  // 等待所有任务完成
```

---

## 18. 最佳实践与注意事项

### 优先使用 UE::Tasks 而非旧版 API

```cpp
// ❌ 旧版: 使用 Async() + TFuture
TFuture<int32> Future = Async(EAsyncExecution::ThreadPool, []() -> int32 { return 42; });
int32 Result = Future.Get();  // 阻塞

// ✅ 新版: 使用 UE::Tasks::Launch()
UE::Tasks::TTask<int32> Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, []() -> int32 { return 42; });
int32 Result = Task.GetResult();  // 阻塞, 但会尝试撤回就地执行
```

### 避免在GameThread上阻塞等待

```cpp
// ❌ 危险: 阻塞主线程
void AMyActor::BeginPlay()
{
    FTask Task = Launch(UE_SOURCE_LOCATION, [] { ... });
    Task.Wait();  // 阻塞GameThread!
}

// ✅ 推荐: 使用嵌套任务或先决条件
```

### 管道的生命周期

```cpp
// ❌ 错误: 管道在任务完成前销毁
{
    FPipe Pipe(UE_SOURCE_LOCATION);
    Pipe.Launch(UE_SOURCE_LOCATION, [] { ... });
}  // ~FPipe() check(!HasWork()) 断言!

// ✅ 正确: 等待管道清空
{
    FPipe Pipe(UE_SOURCE_LOCATION);
    Pipe.Launch(UE_SOURCE_LOCATION, [] { ... });
    Pipe.WaitUntilEmpty();
}
```

### 嵌套任务 vs 显式等待

```cpp
// ❌ 阻塞工作线程
Launch(UE_SOURCE_LOCATION, []
{
    FTask Sub = Launch(UE_SOURCE_LOCATION, [] { ... });
    Sub.Wait();  // 工作线程被占用, 浪费资源
});

// ✅ 非阻塞, 工作线程可以执行其他任务
Launch(UE_SOURCE_LOCATION, []
{
    FTask Sub = Launch(UE_SOURCE_LOCATION, [] { ... });
    AddNested(Sub);  // 父任务不阻塞, 但会等子任务完成
});
```

### CancellationToken 的正确使用

```cpp
FCancellationToken Token;
FTask Task = Launch(UE_SOURCE_LOCATION, [&Token]
{
    for (int32 i = 0; i < 1000; ++i)
    {
        if (Token.IsCanceled())
        {
            // 做必要的清理工作
            return;
        }
        // 正常工作...
    }
});

// 在需要时取消
Token.Cancel();
Task.Wait();  // 仍需等待任务完成(提前退出)
```

### 命名线程任务

```cpp
// 在游戏线程上执行任务 (例如操作UObject)
FTask GTTask = Launch(UE_SOURCE_LOCATION,
    [] { check(IsInGameThread()); /* 安全操作UObject */ },
    ETaskPriority::Default,
    EExtendedTaskPriority::GameThreadNormalPri
);
GTTask.Wait();
```

### IsAwaitable 检查

```cpp
// 在等待前检查是否安全, 避免死锁
if (Task.IsAwaitable())
{
    Task.Wait();
}
else
{
    // 使用 AddNested 或其他非阻塞方式
}
```

---

## 参考

- **Task.h 源码路径**: `Engine/Source/Runtime/Core/Public/Tasks/Task.h`
- **Pipe.h 源码路径**: `Engine/Source/Runtime/Core/Public/Tasks/Pipe.h`
- **TaskPrivate.h 源码路径**: `Engine/Source/Runtime/Core/Public/Tasks/TaskPrivate.h`
- **TaskConcurrencyLimiter.h 源码路径**: `Engine/Source/Runtime/Core/Public/Tasks/TaskConcurrencyLimiter.h`
- **LowLevelTask 源码路径**: `Engine/Source/Runtime/Core/Public/Async/Fundamental/Task.h`
- **官方测试用例**: `Engine/Source/Runtime/Core/Tests/Tasks/TasksTest.cpp`
- **示例代码**: 参见同目录下的 `Tasks_System_Example.cpp`
