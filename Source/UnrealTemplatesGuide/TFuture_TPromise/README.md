# UE5-TFuture TPromise 异步编程核心模板深度解析

> 源码版本: UE 5.7  
> 源码路径: `Engine/Source/Runtime/Core/Public/Async/Future.h`

> [示例代码](https://github.com/UEProjectShare/UnrealTemplatesGuide/blob/4e72ff25d53d65bcdb1bdffb1b2ca917fad23faf/Source/UnrealTemplatesGuide/TFuture_TPromise/TFuture_TPromise_Example.cpp)

---

## 1. 概述与设计理念

TFuture/TPromise 是 Unreal Engine 实现的 **Promise-Future 异步编程模式**，用于处理异步操作的结果传递。这种模式源自函数式编程，后被 C++11 标准库采纳 (`std::future`)。

### 核心思想

- **Promise**: 异步操作的"生产者"，负责设置结果
- **Future**: 异步操作的"消费者"，负责获取结果
- 两者通过 **共享状态 (Shared State)** 进行通信

### UE 实现的优势

1. 支持链式调用 (`Then`/`Next`)
2. 与 UE 内存管理系统 (`TSharedPtr`) 深度集成
3. 支持 `TSharedFuture` 允许多处等待同一结果
4. 线程安全的状态管理

---

## 2. 核心类层次结构

### 类继承关系图

```
                   FFutureState (状态基类)
                         |
                         v
              TFutureState<ResultType> (泛型状态)
                         |
         +---------------+---------------+
         |                               |
         v                               v
  TFutureBase<ResultType>         TPromise<ResultType>
         |                        (独立类,持有State)
         |
    +----+----+
    |         |
    v         v
 TFuture  TSharedFuture
```

### 前向声明 (源码 18-20 行)

```cpp
template<typename ResultType> class TFuture;
template<typename ResultType> class TSharedFuture;
template<typename ResultType> class TPromise;
```

### 内部概念检查 (源码 22-32 行)

```cpp
namespace UE::Core::Private
{
    // A concept check for a type being callable
    struct CIntCallable
    {
        template <typename Type>
        auto Requires(Type& Callable, int32 Val) -> decltype(
            Callable(Val)
        );
    };
}
```

> 这个 Concept 用于检测回调函数是否接受 `int` 参数，用于 `TFuture<void>::Next` 的废弃兼容。

---

## 3. FFutureState - 状态管理基类

`FFutureState` 是所有 Future 状态的非泛型基类，负责管理：

- 完成状态标志
- 完成事件同步
- 完成回调

### 源码定义 (37-137 行)

```cpp
class FFutureState
{
public:

    /** Default constructor. */
    FFutureState() = default;

    /**
     * Create and initialize a new instance with a callback.
     *
     * @param InCompletionCallback A function that is called when the state is completed.
     */
    FFutureState(TUniqueFunction<void()>&& InCompletionCallback)
        : CompletionCallback(MoveTemp(InCompletionCallback))
    {
    }

public:

    /**
     * Checks whether the asynchronous result has been set.
     *
     * @return true if the result has been set, false otherwise.
     * @see WaitFor
     */
    bool IsComplete() const
    {
        return bComplete;
    }

    /**
     * Blocks the calling thread until the future result is available.
     *
     * @param Duration The maximum time span to wait for the future result.
     * @return true if the result is available, false otherwise.
     * @see IsComplete
     */
    bool WaitFor(const FTimespan& Duration) const
    {
        if (CompletionEvent->Wait(Duration))
        {
            return true;
        }

        return false;
    }

    /** 
     * Set a continuation to be called on completion of the promise
     * @param Continuation 
     */
    void SetContinuation(TUniqueFunction<void()>&& Continuation)
    {
        bool bShouldJustRun = IsComplete();
        if (!bShouldJustRun)
        {
            FScopeLock Lock(&Mutex);
            bShouldJustRun = IsComplete();
            if (!bShouldJustRun)
            {
                CompletionCallback = MoveTemp(Continuation);
            }
        }
        if (bShouldJustRun && Continuation)
        {
            Continuation();
        }
    }

protected:

    /** Notifies any waiting threads that the result is available. */
    void MarkComplete()
    {
        TUniqueFunction<void()> Continuation;
        {
            FScopeLock Lock(&Mutex);
            Continuation = MoveTemp(CompletionCallback);
            bComplete = true;
        }
        CompletionEvent->Trigger();

        if (Continuation)
        {
            Continuation();
        }
    }

private:
    /** Mutex used to allow proper handling of continuations */
    mutable FCriticalSection Mutex;

    /** An optional callback function that is executed the state is completed. */
    TUniqueFunction<void()> CompletionCallback;

    /** Holds an event signaling that the result is available. */
    FPooledSyncEvent CompletionEvent{ true };

    /** Whether the asynchronous result is available. */
    TAtomic<bool> bComplete{ false };
};
```

### 关键设计要点

#### 1. 双重检查锁定 (Double-Checked Locking)

`SetContinuation()` 使用双重检查来避免不必要的锁获取：

```cpp
void SetContinuation(TUniqueFunction<void()>&& Continuation)
{
    bool bShouldJustRun = IsComplete();     // 第一次检查(无锁)
    if (!bShouldJustRun)
    {
        FScopeLock Lock(&Mutex);            // 加锁
        bShouldJustRun = IsComplete();      // 第二次检查(持锁)
        if (!bShouldJustRun)
        {
            CompletionCallback = MoveTemp(Continuation);
        }
    }
    if (bShouldJustRun && Continuation)
    {
        Continuation();                      // 已完成则立即执行
    }
}
```

**为什么需要双重检查？**

- 防止在检查和加锁之间状态变化导致的竞态条件
- 避免不必要的锁获取，提升性能

#### 2. 成员变量分析

| 成员 | 类型 | 说明 |
|------|------|------|
| `Mutex` | `mutable FCriticalSection` | 互斥锁，保护状态修改 |
| `CompletionCallback` | `TUniqueFunction<void()>` | 完成时的回调函数 |
| `CompletionEvent` | `FPooledSyncEvent` | 同步事件，用于线程等待 |
| `bComplete` | `TAtomic<bool>` | 原子完成标志 |

#### 3. FPooledSyncEvent

`FPooledSyncEvent CompletionEvent{ true };`

- 参数 `true` 表示这是一个 **手动重置事件**
- 使用事件池，减少系统资源消耗

#### 4. MarkComplete() 执行顺序

```cpp
void MarkComplete()
{
    TUniqueFunction<void()> Continuation;
    {
        FScopeLock Lock(&Mutex);
        Continuation = MoveTemp(CompletionCallback);  // 1. 取出回调
        bComplete = true;                              // 2. 设置完成标志
    }
    CompletionEvent->Trigger();                        // 3. 触发事件(唤醒等待线程)

    if (Continuation)
    {
        Continuation();                                // 4. 执行回调
    }
}
```

**重要：** 先触发事件，再执行回调，确保等待线程能及时被唤醒。

---

## 4. TFutureState\<ResultType\> - 泛型状态类

`TFutureState` 继承自 `FFutureState`，添加了泛型结果存储。

### 源码定义 (143-214 行)

```cpp
template<typename ResultType>
class TFutureState
    : public FFutureState
{
public:
    using MutableResultType = typename TTypeCompatibleBytes<ResultType>::MutableGetType;
    using ConstResultType   = typename TTypeCompatibleBytes<ResultType>::ConstGetType;
    using RvalueResultType  = typename TTypeCompatibleBytes<ResultType>::RvalueGetType;

    /** Default constructor. */
    TFutureState()
        : FFutureState()
    { }

    ~TFutureState()
    {
        if (IsComplete())
        {
            Result.DestroyUnchecked();
        }
    }

    /**
     * Create and initialize a new instance with a callback.
     *
     * @param CompletionCallback A function that is called when the state is completed.
     */
    TFutureState(TUniqueFunction<void()>&& CompletionCallback)
        : FFutureState(MoveTemp(CompletionCallback))
    { }

public:

    /**
     * Gets the result (will block the calling thread until the result is available).
     *
     * @return The result value.
     * @see EmplaceResult
     */
    MutableResultType GetResult()
    {
        while (!IsComplete())
        {
            WaitFor(FTimespan::MaxValue());
        }

        return Result.GetUnchecked();
    }
    ConstResultType GetResult() const
    {
        return const_cast<TFutureState*>(this)->GetResult();
    }

    /**
     * Sets the result and notifies any waiting threads.
     *
     * @param Args The arguments to forward to the constructor of the result.
     * @see GetResult
     */
    template<typename... ArgTypes>
    void EmplaceResult(ArgTypes&&... Args)
    {
        check(!IsComplete());
        Result.EmplaceUnchecked(Forward<ArgTypes>(Args)...);
        MarkComplete();
    }

private:

    /** Holds the asynchronous result. */
    TTypeCompatibleBytes<ResultType> Result;
};
```

### 技术细节解析

#### 1. TTypeCompatibleBytes\<T\>

这是 UE 的延迟初始化存储类型，类似于 `std::optional` 但更底层：

- 预分配对齐的内存空间
- **不自动构造/析构对象**
- 需要手动调用 `EmplaceUnchecked` / `DestroyUnchecked`

#### 2. 结果类型别名

```cpp
using MutableResultType = typename TTypeCompatibleBytes<ResultType>::MutableGetType;
using ConstResultType   = typename TTypeCompatibleBytes<ResultType>::ConstGetType;
using RvalueResultType  = typename TTypeCompatibleBytes<ResultType>::RvalueGetType;
```

| 别名 | 含义 | 示例 (ResultType = FString) |
|------|------|----------------------------|
| `MutableResultType` | 可变引用 | `FString&` |
| `ConstResultType` | 常量引用 | `const FString&` |
| `RvalueResultType` | 右值引用 | `FString&&` |

#### 3. GetResult() 阻塞等待

```cpp
MutableResultType GetResult()
{
    while (!IsComplete())
    {
        WaitFor(FTimespan::MaxValue());
    }
    return Result.GetUnchecked();
}
```

**为什么使用 `while` 循环？**

处理 **虚假唤醒 (Spurious Wakeup)**：某些系统上，`Wait` 可能在没有 `Trigger` 的情况下返回。

#### 4. 析构函数手动销毁

```cpp
~TFutureState()
{
    if (IsComplete())
    {
        Result.DestroyUnchecked();
    }
}
```

只有在结果已设置时才需要析构，因为 `TTypeCompatibleBytes` 不会自动调用析构函数。

---

## 5. TFutureBase\<ResultType\> - Future基类

`TFutureBase` 是 `TFuture` 和 `TSharedFuture` 的公共基类。

### 源码定义 (222-384 行)

```cpp
template <typename ResultType>
class TFutureBase
{
protected:
    using StateType = TFutureState<ResultType>;

public:
    using MutableResultType = typename StateType::MutableResultType;
    using ConstResultType   = typename StateType::ConstResultType;
    using RvalueResultType  = typename StateType::RvalueResultType;

    /**
     * Gets the future's result.
     *
     * @return The result as a const reference, or the same reference if the future holds a reference, or void if the future holds a void.
     * @note Not equivalent to std::future::get(). The future remains valid.
     */
    ConstResultType Get() const
        requires (!std::is_object_v<ResultType>)
    {
        return this->GetState()->GetResult();
    }
    ConstResultType Get() const UE_LIFETIMEBOUND
        requires (std::is_object_v<ResultType>)
    {
        return this->GetState()->GetResult();
    }

    /**
     * Checks whether this future object has its value set.
     *
     * @return true if this future has a shared state and the value has been set, false otherwise.
     * @see IsValid
     */
    bool IsReady() const
    {
        return State.IsValid() ? State->IsComplete() : false;
    }

    /**
     * Checks whether this future object has a valid state.
     *
     * @return true if the state is valid, false otherwise.
     * @see IsReady
     */
    bool IsValid() const
    {
        return State.IsValid();
    }

    /**
     * Blocks the calling thread until the future result is available.
     *
     * Note that this method may block forever if the result is never set. Use
     * the WaitFor or WaitUntil methods to specify a maximum timeout for the wait.
     *
     * @see WaitFor, WaitUntil
     */
    void Wait() const
    {
        if (State.IsValid())
        {
            while (!WaitFor(FTimespan::MaxValue()));
        }
    }

    /**
     * Blocks the calling thread until the future result is available or the specified duration is exceeded.
     *
     * @param Duration The maximum time span to wait for the future result.
     * @return true if the result is available, false otherwise.
     * @see Wait, WaitUntil
     */
    bool WaitFor(const FTimespan& Duration) const
    {
        return State.IsValid() ? State->WaitFor(Duration) : false;
    }

    /**
     * Blocks the calling thread until the future result is available or the specified time is hit.
     *
     * @param Time The time until to wait for the future result (in UTC).
     * @return true if the result is available, false otherwise.
     * @see Wait, WaitUntil
     */
    bool WaitUntil(const FDateTime& Time) const
    {
        return WaitFor(Time - FDateTime::UtcNow());
    }

protected:

    /** Default constructor. */
    TFutureBase() = default;

    /**
     * Creates and initializes a new instance.
     *
     * @param InState The shared state to initialize with.
     */
    TFutureBase(TSharedPtr<StateType>&& InState)
        : State(MoveTemp(InState))
    {
    }
    TFutureBase(const TSharedPtr<StateType>& InState)
        : State(InState)
    {
    }

    /**
     * Gets the shared state object.
     *
     * @return The shared state object.
     */
    const TSharedPtr<StateType>& GetState() const
    {
        // if you hit this assertion then your future has an invalid state.
        // this happens if you have an uninitialized future or if you moved
        // it to another instance.
        check(State.IsValid());

        return State;
    }

    /**
     * Set a completion callback that will be called once the future completes
     *    or immediately if already completed
     *
     * @param Continuation a continuation taking an argument of type TFuture<ResultType>
     * @return nothing at the moment but could return another future to allow future chaining
     */
    template<typename Func>
    auto Then(Func Continuation);

    /**
     * Convenience wrapper for Then that
     *    set a completion callback that will be called once the future completes
     *    or immediately if already completed
     * @param Continuation a continuation taking an argument of type ResultType
     * @return nothing at the moment but could return another future to allow future chaining
     */
    template<typename Func>
    auto Next(Func Continuation);

    /**
     * Reset the future.
     *    Resetting a future removes any continuation from its shared state and invalidates it.
     *    Useful for discarding yet to be completed future cleanly.
     */
    void Reset()
    {
        if (IsValid())
        {
            this->State->SetContinuation(nullptr);
            this->State.Reset();
        }
    }

private:

    /** Holds the future's state. */
    TSharedPtr<StateType> State;
};
```

### 关键 API 解析

#### 1. Get() 的两个重载

```cpp
ConstResultType Get() const
    requires (!std::is_object_v<ResultType>)  // void 或引用类型
{
    return this->GetState()->GetResult();
}

ConstResultType Get() const UE_LIFETIMEBOUND   // 对象类型
    requires (std::is_object_v<ResultType>)
{
    return this->GetState()->GetResult();
}
```

- `requires` 是 C++20 Concepts 约束
- `UE_LIFETIMEBOUND` 标记返回值生命周期绑定到对象，帮助编译器检测悬垂引用

#### 2. 状态检查方法

| 方法 | 说明 |
|------|------|
| `IsValid()` | 检查 Future 是否持有有效状态 |
| `IsReady()` | 检查结果是否已设置 |

#### 3. 等待方法

| 方法 | 说明 |
|------|------|
| `Wait()` | 无限等待直到结果可用 |
| `WaitFor(Duration)` | 等待指定时间 |
| `WaitUntil(Time)` | 等待到指定时间点 |

#### 4. Reset() 重置

```cpp
void Reset()
{
    if (IsValid())
    {
        this->State->SetContinuation(nullptr);  // 清除回调
        this->State.Reset();                     // 释放状态
    }
}
```

用于放弃未完成的 Future，清理资源。

---

## 6. TFuture\<ResultType\> - 独占Future

`TFuture` 是独占所有权的 Future，只能移动不能复制。

### 源码定义 (390-478 行)

```cpp
template<typename ResultType>
class TFuture final
    : public TFutureBase<ResultType>
{
    template <typename>
    friend class TPromise;

    template <typename>
    friend class TFutureBase;

    using BaseType = TFutureBase<ResultType>;

    using MutableResultType = typename BaseType::MutableResultType;
    using ConstResultType   = typename BaseType::ConstResultType;
    using RvalueResultType  = typename BaseType::RvalueResultType;

public:

    /** Default constructor. */
    TFuture() = default;

    // Movable-only
    TFuture(TFuture&&) = default;
    TFuture(const TFuture&) = delete;
    TFuture& operator=(TFuture&&) = default;
    TFuture& operator=(const TFuture&) = delete;
    ~TFuture() = default;

    /**
     * Gets the future's result.
     *
     * @return The result as a non-const reference, or the same reference if the future holds a reference, or void if the future holds a void.
     * @note Not equivalent to std::future::get(). The future remains valid.
     */
    MutableResultType GetMutable()
        requires (!std::is_object_v<ResultType>)
    {
        return (MutableResultType)this->Get();
    }
    MutableResultType GetMutable() UE_LIFETIMEBOUND
        requires (std::is_object_v<ResultType>)
    {
        return (MutableResultType)this->Get();
    }

    /**
     * Consumes the future's result and invalidates the future.
     *
     * @return The result.
     * @note Equivalent to std::future::get(). Invalidates the future.
     */
    ResultType Consume()
    {
        TFuture Local(MoveTemp(*this));
        return (RvalueResultType)Local.Get();
    }

    /**
     * Moves this future's state into a shared future.
     *
     * @return The shared future object.
     */
    TSharedFuture<ResultType> Share()
    {
        return TSharedFuture<ResultType>(MoveTemp(*this));
    }

    /**
     * Expose Then functionality
     * @see TFutureBase 
     */
    using BaseType::Then;

    /**
     * Expose Next functionality
     * @see TFutureBase
     */
    using BaseType::Next;

    /**
     * Expose Reset functionality
     * @see TFutureBase
     */
    using BaseType::Reset;

private:
    // Forward constructors
    using BaseType::TFutureBase;
};
```

### Get() vs Consume() 对比

| 特性 | Get() | Consume() |
|------|-------|-----------|
| 返回类型 | `const T&` | `T` (值/移动) |
| Future 状态 | 保持有效 | 变为无效 |
| 调用次数 | 可多次调用 | 只能调用一次 |
| 等同于 | - | `std::future::get()` |

### Consume() 实现解析

```cpp
ResultType Consume()
{
    TFuture Local(MoveTemp(*this));      // 移动到局部变量
    return (RvalueResultType)Local.Get(); // 获取并返回(移动语义)
}
```

**技巧：** 通过移动到局部变量，确保原 Future 失效，同时保证返回值的正确生命周期。

### Share() 转换

```cpp
TSharedFuture<ResultType> Share()
{
    return TSharedFuture<ResultType>(MoveTemp(*this));
}
```

将独占 Future 转换为共享 Future，原 Future 失效。

---

## 7. TSharedFuture\<ResultType\> - 共享Future

`TSharedFuture` 允许多处代码等待同一个异步结果。

### 源码定义 (487-530 行)

```cpp
template<typename ResultType>
class TSharedFuture final
    : public TFutureBase<ResultType>
{
    template <typename>
    friend class TFuture;

    using BaseType = TFutureBase<ResultType>;

    using MutableResultType = typename BaseType::MutableResultType;
    using ConstResultType   = typename BaseType::ConstResultType;
    using RvalueResultType  = typename BaseType::RvalueResultType;

public:

    /** Default constructor. */
    TSharedFuture() = default;

    /**
     * Creates and initializes a new instances from a future object.
     *
     * @param Future The future object to initialize from.
     */
    TSharedFuture(TFuture<ResultType>&& Future)
        : BaseType(MoveTemp(Future))
    { }

    /**
     * Gets the future's result.
     *
     * @return The result as a const reference, or the same reference if the future holds a reference, or void if the future holds a void.
     * @note Not equivalent to std::future::get(). The future remains valid.
     */
    ConstResultType Get() const
    {
        // This forwarding function is necessary to 'cancel' the UE_LIFETIMEBOUND of the base class, as
        // other shared futures can keep the object alive.
        return BaseType::Get();
    }

private:
    // Forward constructors
    using BaseType::TFutureBase;
};
```

### TFuture vs TSharedFuture

| 特性 | TFuture | TSharedFuture |
|------|---------|---------------|
| 所有权 | 独占 | 共享 |
| 复制 | 禁止 | 允许 |
| `Consume()` | 支持 | 不支持 |
| `GetMutable()` | 支持 | 不支持 |
| `Then()`/`Next()` | 支持 | 不支持 |
| 多处等待 | 不可以 | 可以 |

### Get() 重写说明

```cpp
ConstResultType Get() const
{
    // This forwarding function is necessary to 'cancel' the UE_LIFETIMEBOUND of the base class, as
    // other shared futures can keep the object alive.
    return BaseType::Get();
}
```

**为什么要重写？**

移除 `UE_LIFETIMEBOUND` 属性，因为共享 Future 的其他副本可以保持状态存活，返回的引用不会悬垂。

---

## 8. TPromise\<ResultType\> - Promise承诺

`TPromise` 是异步结果的生产者，用于设置 Future 的值。

### 源码定义 (539-654 行)

```cpp
template<typename ResultType>
class TPromise final
{
    using StateType = TFutureState<ResultType>;

    // This is necessary because we can't form references to void for the parameter of Set().
    using SetType = std::conditional_t<std::is_void_v<ResultType>, int, ResultType>;

public:

    /** Default constructor (creates a new shared state). */
    TPromise()
        : State(MakeShared<TFutureState<ResultType>>())
    {
    }

    /**
     * Create and initialize a new instance with a callback.
     *
     * @param CompletionCallback A function that is called when the future state is completed.
     */
    TPromise(TUniqueFunction<void()>&& CompletionCallback)
        : State(MakeShared<TFutureState<ResultType>>(MoveTemp(CompletionCallback)))
    {
    }

    // Movable-only
    TPromise(TPromise&& Other) = default;
    TPromise(const TPromise& Other) = delete;
    TPromise& operator=(TPromise&& Other) = default;
    TPromise& operator=(const TPromise& Other) = delete;

    /** Destructor. */
    ~TPromise()
    {
        if (State.IsValid())
        {
            // if you hit this assertion then your promise never had its result
            // value set. broken promises are considered programming errors.
            check(State->IsComplete());
        }
    }

    /**
     * Gets a TFuture object associated with the shared state of this promise.
     *
     * @return The TFuture object.
     */
    TFuture<ResultType> GetFuture()
    {
        check(!bFutureRetrieved);
        bFutureRetrieved = true;

        return TFuture<ResultType>(this->GetState());
    }

    /**
     * Sets the promised result.
     *
     * The result must be set only once. An assertion will
     * be triggered if this method is called a second time.
     *
     * @param Result The result value to set.
     */
    inline void SetValue(const SetType& Result)
        requires(!std::is_void_v<ResultType>)
    {
        EmplaceValue(Result);
    }
    inline void SetValue(SetType&& Result)
        requires(!std::is_void_v<ResultType> && !std::is_lvalue_reference_v<ResultType>)
    {
        EmplaceValue(MoveTemp(Result));
    }
    inline void SetValue()
        requires(std::is_void_v<ResultType>)
    {
        EmplaceValue();
    }

    /**
     * Sets the promised result.
     *
     * The result must be set only once. An assertion will
     * be triggered if this method is called a second time.
     *
     * @param Args The arguments to forward to the constructor of the result.
     */
    template <typename... ArgTypes>
    void EmplaceValue(ArgTypes&&... Args)
    {
        this->GetState()->EmplaceResult(Forward<ArgTypes>(Args)...);
    }

protected:
    /**
     * Gets the shared state object.
     *
     * @return The shared state object.
     */
    const TSharedPtr<StateType>& GetState()
    {
        // if you hit this assertion then your promise has an invalid state.
        // this happens if you move the promise to another instance.
        check(State.IsValid());

        return State;
    }

private:
    /** Holds the shared state object. */
    TSharedPtr<StateType> State;

    /** Whether a future has already been retrieved from this promise. */
    bool bFutureRetrieved = false;
};
```

### 关键设计点

#### 1. SetType 类型别名

```cpp
using SetType = std::conditional_t<std::is_void_v<ResultType>, int, ResultType>;
```

**问题：** `void` 类型不能作为函数参数  
**解决：** 当 `ResultType` 为 `void` 时，使用 `int` 作为占位类型

#### 2. 析构函数检查 (Broken Promise)

```cpp
~TPromise()
{
    if (State.IsValid())
    {
        // if you hit this assertion then your promise never had its result
        // value set. broken promises are considered programming errors.
        check(State->IsComplete());
    }
}
```

**重要：** Promise 析构前必须设置结果，否则触发断言！

#### 3. GetFuture() 只能调用一次

```cpp
TFuture<ResultType> GetFuture()
{
    check(!bFutureRetrieved);
    bFutureRetrieved = true;
    return TFuture<ResultType>(this->GetState());
}
```

使用 `bFutureRetrieved` 标志防止重复获取。

#### 4. SetValue() 多个重载

```cpp
// 左值引用版本
void SetValue(const SetType& Result)
    requires(!std::is_void_v<ResultType>)

// 右值引用版本 (非左值引用类型)
void SetValue(SetType&& Result)
    requires(!std::is_void_v<ResultType> && !std::is_lvalue_reference_v<ResultType>)

// void 类型版本
void SetValue()
    requires(std::is_void_v<ResultType>)
```

### Promise 使用规则

| 规则 | 说明 |
|------|------|
| `GetFuture()` | 只能调用一次 |
| `SetValue()`/`EmplaceValue()` | 只能调用一次 |
| 析构前必须设置结果 | 否则触发断言 (Broken Promise) |
| 只能移动 | 不能复制 |

---

## 9. Then/Next 链式调用机制

UE 的 TFuture 相比 `std::future` 的重要增强是支持链式调用。

### FutureDetail 命名空间 (659-675 行)

```cpp
namespace FutureDetail
{
    /**
    * Template for setting a promise value from a continuation.
    */
    template<typename Func, typename ParamType, typename ResultType>
    inline void SetPromiseValue(TPromise<ResultType>& Promise, Func& Function, TFuture<ParamType>&& Param)
    {
        Promise.SetValue(Function(MoveTemp(Param)));
    }
    template<typename Func, typename ParamType>
    inline void SetPromiseValue(TPromise<void>& Promise, Func& Function, TFuture<ParamType>&& Param)
    {
        Function(MoveTemp(Param));
        Promise.SetValue();
    }
}
```

**两个重载的区别：**

- 普通版本：回调返回值直接设置给 Promise
- void 版本：先调用回调，再设置空值

### Then() 实现 (677-696 行)

```cpp
template<typename ResultType>
template<typename Func>
auto TFutureBase<ResultType>::Then(Func Continuation) //-> TFuture<decltype(Continuation(MoveTemp(TFuture<ResultType>())))>
{
    check(IsValid());
    using ReturnValue = decltype(Continuation(MoveTemp(TFuture<ResultType>())));

    TPromise<ReturnValue> Promise;
    TFuture<ReturnValue> FutureResult = Promise.GetFuture();
    TUniqueFunction<void()> Callback = [PromiseCapture = MoveTemp(Promise), ContinuationCapture = MoveTemp(Continuation), StateCapture = this->State]() mutable
    {
        FutureDetail::SetPromiseValue(PromiseCapture, ContinuationCapture, TFuture<ResultType>(MoveTemp(StateCapture)));
    };

    // This invalidate this future.
    TSharedPtr<StateType> MovedState = MoveTemp(this->State);
    MovedState->SetContinuation(MoveTemp(Callback));
    return FutureResult;
}
```

#### Then() 执行流程

```
1. 推断返回类型
   using ReturnValue = decltype(Continuation(MoveTemp(TFuture<ResultType>())));

2. 创建新 Promise 和 Future
   TPromise<ReturnValue> Promise;
   TFuture<ReturnValue> FutureResult = Promise.GetFuture();

3. 构建回调 Lambda (捕获 Promise、Continuation、State)
   TUniqueFunction<void()> Callback = [...] { ... };

4. 移动当前 State (使原 Future 失效)
   TSharedPtr<StateType> MovedState = MoveTemp(this->State);

5. 设置延续回调
   MovedState->SetContinuation(MoveTemp(Callback));

6. 返回新 Future
   return FutureResult;
```

### Next() 实现 (698-723 行)

```cpp
template<typename ResultType>
template<typename Func>
auto TFutureBase<ResultType>::Next(Func Continuation) //-> TFuture<decltype(Continuation(Consume()))>
{
    return this->Then([Continuation = MoveTemp(Continuation)](TFuture<ResultType> Self) mutable
    {
        if constexpr (std::is_void_v<ResultType>)
        {
            Self.Consume();
            if constexpr (TModels_V<UE::Core::Private::CIntCallable, Func>)
            {
                UE_STATIC_DEPRECATE(5.6, true, "Passing continuations to TFuture<void>::Next which take int parameters has been deprecated - please remove the parameter.");
                return Continuation(1);
            }
            else
            {
                return Continuation();
            }
        }
        else
        {
            return Continuation(Self.Consume());
        }
    });
}
```

#### Next() 的废弃兼容

```cpp
if constexpr (TModels_V<UE::Core::Private::CIntCallable, Func>)
{
    UE_STATIC_DEPRECATE(5.6, true, "Passing continuations to TFuture<void>::Next which take int parameters has been deprecated - please remove the parameter.");
    return Continuation(1);
}
```

旧版本 `TFuture<void>::Next` 的回调接受 `int` 参数，现已废弃。

### Then vs Next 对比

| 特性 | Then() | Next() |
|------|--------|--------|
| 回调签名 | `R Func(TFuture<T>)` | `R Func(T)` |
| 获取结果 | 需手动调用 `Get()`/`Consume()` | 自动 `Consume()` |
| 灵活性 | 更高 | 更简洁 |
| 适用场景 | 需要控制 Future 生命周期 | 简单链式处理 |

### 链式调用执行流程图

```
Promise.SetValue()
     |
     v
State->EmplaceResult()
     |
     v
State->MarkComplete()
     |
     +---> CompletionEvent.Trigger() ---> 唤醒 Wait() 的线程
     |
     +---> CompletionCallback() ---> 执行 Then/Next 注册的回调
                 |
                 v
           下一个 Promise.SetValue() ---> 链式传播
```

---

## 10. 辅助函数与工具

### MakeFulfilledPromise (725-732 行)

```cpp
/** Helper to create and immediately fulfill a promise */
template<typename ResultType, typename... ArgTypes>
TPromise<ResultType> MakeFulfilledPromise(ArgTypes&&... Args)
{
    TPromise<ResultType> Promise;
    Promise.EmplaceValue(Forward<ArgTypes>(Args)...);
    return Promise;
}
```

**用途：** 快速创建已完成的 Promise

```cpp
// 使用示例
TPromise<int32> Promise = MakeFulfilledPromise<int32>(42);
TFuture<int32> Future = Promise.GetFuture();
check(Future.IsReady());  // 立即就绪
```

---

## 11. 与std::future对比

| 特性 | TFuture | std::future |
|------|---------|-------------|
| 链式调用 Then/Next | ✅ 支持 | ❌ 不支持 (C++23 后支持) |
| 共享 Future | TSharedFuture | std::shared_future |
| 异常传播 | ❌ 不支持 | ✅ 支持 |
| Get() 行为 | 保持有效 | 使 Future 失效 |
| Consume() | 消费并失效 | 等同于 get() |
| 线程池集成 | Async() | std::async() |
| 内存管理 | TSharedPtr | 实现定义 |

---

## 12. 最佳实践与注意事项

### 1. 避免 Broken Promise

```cpp
// ❌ 错误：Promise 析构时未设置值
void BadExample()
{
    TPromise<int32> Promise;
    TFuture<int32> Future = Promise.GetFuture();
    // Promise 析构时会触发 check 断言!
}

// ✅ 正确：确保设置值
void GoodExample()
{
    TPromise<int32> Promise;
    TFuture<int32> Future = Promise.GetFuture();
    Promise.SetValue(42);  // 必须设置
}
```

### 2. 注意 Future 有效性

```cpp
TFuture<int32> Future = ...;

// 移动后失效
TFuture<int32> Other = MoveTemp(Future);
check(!Future.IsValid());  // Future 已失效

// Consume 后失效
int32 Value = Other.Consume();
check(!Other.IsValid());  // Other 已失效

// 使用前检查
if (Future.IsValid())
{
    int32 Result = Future.Get();
}
```

### 3. 选择正确的获取方式

| 方法 | 适用场景 |
|------|----------|
| `Get()` | 多次读取，不消费 |
| `Consume()` | 单次获取，转移所有权 |
| `GetMutable()` | 需要修改结果 |

### 4. 链式调用注意事项

```cpp
TPromise<int32> Promise;
TFuture<int32> Future = Promise.GetFuture();

// Then/Next 会使原 Future 失效
Future = Future.Next([](int32 Value) { return Value * 2; });

// 回调在 SetValue 调用线程执行，确保线程安全
Promise.SetValue(21);
```

### 5. 避免死锁

```cpp
// ❌ 危险：在持有锁时等待
{
    FScopeLock Lock(&SomeMutex);
    Future.Wait();  // 可能死锁
}

// ❌ 危险：在 GameThread 阻塞等待
void AMyActor::BeginPlay()
{
    Future.Wait();  // 阻塞主线程！
}

// ✅ 推荐：使用非阻塞回调
Future.Next([](int32 Result)
{
    // 异步处理
    AsyncTask(ENamedThreads::GameThread, [Result]()
    {
        // 回到主线程处理 UI 等
    });
});
```

### 6. 性能考虑

- `TSharedPtr` 有引用计数开销
- 频繁创建 Promise/Future 会有内存分配
- 考虑重用或池化长期运行的异步任务

---

## 参考

- **源码路径**: `Engine/Source/Runtime/Core/Public/Async/Future.h`
- **相关头文件**: `Async/Async.h`
- **示例代码**: 参见同目录下的 `TFuture_Example.cpp`
