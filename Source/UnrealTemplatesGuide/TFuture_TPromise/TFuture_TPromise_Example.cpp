// Fill out your copyright notice in the Description page of Project Settings.

#include "TFuture_TPromise_Example.h"
#include "Async/Async.h"

ATFuture_TPromise_Example::ATFuture_TPromise_Example()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATFuture_TPromise_Example::BeginPlay()
{
	Super::BeginPlay();
	
	UE_LOG(LogTemp, Warning, TEXT("========== TFuture Examples Start =========="));
	
	// 运行各种示例
	Example_BasicPromiseFuture();
	Example_GetVsConsume();
	Example_ThenChaining();
	Example_NextChaining();
	Example_SharedFuture();
	Example_VoidFuture();
	Example_WithAsync();
	Example_NonBlockingCallback();
	
	UE_LOG(LogTemp, Warning, TEXT("========== TFuture Examples End =========="));
}

void ATFuture_TPromise_Example::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// ============================================================================
// 示例1: 基础Promise-Future模式
// ============================================================================
void ATFuture_TPromise_Example::Example_BasicPromiseFuture()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 1] Basic Promise-Future Pattern"));
	
	/*
	 * Promise-Future模式核心流程:
	 * 
	 *   [生产者线程]                    [消费者线程]
	 *        |                              |
	 *   创建 TPromise                       |
	 *        |                              |
	 *   GetFuture() -----> TFuture -------> |
	 *        |                              |
	 *   ... 异步工作 ...              Future.Get() 阻塞等待
	 *        |                              |
	 *   SetValue(结果)                      |
	 *        |                              |
	 *   触发 CompletionEvent -------> 唤醒并返回结果
	 */
	
	// 步骤1: 创建Promise
	TPromise<int32> Promise;
	
	// 步骤2: 获取关联的Future (只能调用一次)
	TFuture<int32> Future = Promise.GetFuture();
	
	// 步骤3: 在另一个线程设置结果
	// 注意: Promise必须移动到Lambda中,因为Promise是move-only类型
	Async(EAsyncExecution::Thread, [Promise = MoveTemp(Promise)]() mutable
	{
		// 模拟耗时操作
		FPlatformProcess::Sleep(0.05f);
		
		// 设置结果 - 这会触发CompletionEvent,唤醒等待的线程
		Promise.SetValue(42);
		
		UE_LOG(LogTemp, Log, TEXT("  [Producer] Value set: 42"));
	});
	
	// 步骤4: 等待并获取结果 (会阻塞直到SetValue被调用)
	int32 Result = Future.Get();
	UE_LOG(LogTemp, Log, TEXT("  [Consumer] Got result: %d"), Result);
}

// ============================================================================
// 示例2: Get vs Consume
// ============================================================================
void ATFuture_TPromise_Example::Example_GetVsConsume()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 2] Get() vs Consume()"));
	
	/*
	 * Get() 和 Consume() 的区别:
	 * 
	 * Get():
	 *   - 返回 const 引用
	 *   - Future 保持有效
	 *   - 可以多次调用
	 *   - 与 std::future 不同!
	 * 
	 * Consume():
	 *   - 返回值 (移动语义)
	 *   - Future 变为无效
	 *   - 只能调用一次
	 *   - 等同于 std::future::get()
	 */
	
	TPromise<FString> Promise;
	TFuture<FString> Future = Promise.GetFuture();
	
	// 立即设置值
	Promise.SetValue(TEXT("Hello, TFuture!"));
	
	// Get() - 返回const引用,Future保持有效
	const FString& Ref1 = Future.Get();
	const FString& Ref2 = Future.Get();  // 可以再次调用
	UE_LOG(LogTemp, Log, TEXT("  After Get(): \"%s\", IsValid=%d"), *Ref1, Future.IsValid());
	
	// Consume() - 移动值出来,Future变为无效
	FString MovedValue = Future.Consume();
	UE_LOG(LogTemp, Log, TEXT("  After Consume(): \"%s\", IsValid=%d"), *MovedValue, Future.IsValid());
	
	// 此时 Future.Get() 会触发断言,因为Future已无效
}

// ============================================================================
// 示例3: Then链式调用
// ============================================================================
void ATFuture_TPromise_Example::Example_ThenChaining()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 3] Then() Chaining"));
	
	/*
	 * Then() 链式调用机制完整分析:
	 *
	 * 执行流程详解:
	 *
	 * ==================== 阶段1: 链式构建 ====================
	 *
	 * 步骤1.1: Promise.GetFuture()
	 *   - 创建 TPromise<int32> Promise
	 *   - 创建 TFuture<int32> Future1
	 *   - Future1.State = TSharedPtr<TFutureState<int32>>
	 *
	 * 步骤1.2: 第一个Then()调用
	 *   - Then([](TFuture<int32>)->FString)
	 *   - 内部操作:
	 *     a. 创建 TPromise<FString> Promise2
	 *     b. 获取 TFuture<FString> Future2
	 *     c. 创建 Callback1:
	 *        [Promise2, Lambda1, Future1.State] {
	 *          SetPromiseValue(Promise2, Lambda1, TFuture<int32>(Future1.State))
	 *        }
	 *     d. 将 Future1.State 移动到 MovedState
	 *     e. MovedState->SetContinuation(Callback1)
	 *   - Future1.State = nullptr (Future1失效)
	 *   - 返回 Future2 (状态: 未完成)
	 *
	 * 步骤1.3: 第二个Then()调用
	 *   - Then([](TFuture<FString>)->int32)
	 *   - 内部操作:
	 *     a. 创建 TPromise<int32> Promise3
	 *     b. 获取 TFuture<int32> Future3
	 *     c. 创建 Callback2:
	 *        [Promise3, Lambda2, Future2.State] {
	 *          SetPromiseValue(Promise3, Lambda2, TFuture<FString>(Future2.State))
	 *        }
	 *     d. 将 Future2.State 移动到 MovedState
	 *     e. MovedState->SetContinuation(Callback2)
	 *   - Future2.State = nullptr (Future2失效)
	 *   - 返回 Future3 (状态: 未完成)
	 *
	 *   此时状态:
	 *   - Promise: 未设置值
	 *   - State1: 未完成, 保存 Callback1
	 *   - State2: 未完成, 保存 Callback2
	 *   - Future3: 最终Future, 状态未完成
	 *
	 * ==================== 阶段2: 触发执行 ====================
	 *
	 * 步骤2.1: Promise.SetValue(12345)
	 *   - Promise.SetValue() 调用 State1->EmplaceValue(12345)
	 *   - State1.MarkComplete() 被调用:
	 *     a. 取出 State1 的 CompletionCallback = Callback1
	 *     b. 标记 bComplete = true
	 *     c. Trigger() 唤醒等待线程
	 *     d. 执行 Callback1()
	 *
	 * ==================== 阶段3: Callback1 执行 ====================
	 *
	 * 步骤3.1: Callback1 执行
	 *   [Promise2, Lambda1, State1] {
	 *     SetPromiseValue(Promise2, Lambda1, TFuture<int32>(State1))
	 *   }
	 *
	 * 步骤3.2: SetPromiseValue() 内部
	 *   - 传入: Promise2, Lambda1, TFuture<int32>(State1)
	 *   - TFuture<int32>(State1) 的状态已完成，值为 12345
	 *   - 调用: Promise2.SetValue(Lambda1(MoveTemp(IntFuture)))
	 *
	 * 步骤3.3: Lambda1 执行
	 *   [](TFuture<int32> IntFuture) -> FString {
	 *     int32 Value = IntFuture.Consume();  // Value = 12345
	 *     FString Result = FString::Printf(TEXT("Number: %d"), Value);
	 *     UE_LOG(LogTemp, Log, TEXT("  [Then 1] %d -> \"%s\""), Value, *Result);
	 *     return Result;  // 返回 "Number: 12345"
	 *   }
	 *   - 输出: [Then 1] 12345 -> "Number: 12345"
	 *   - 返回: "Number: 12345"
	 *
	 * 步骤3.4: Promise2.SetValue("Number: 12345")
	 *   - 调用 State2->EmplaceValue("Number: 12345")
	 *   - State2.MarkComplete() 被调用:
	 *     a. 取出 State2 的 CompletionCallback = Callback2
	 *     b. 标记 bComplete = true
	 *     c. Trigger() 唤醒等待线程
	 *     d. 执行 Callback2()
	 *
	 * ==================== 阶段4: Callback2 执行 ====================
	 *
	 * 步骤4.1: Callback2 执行
	 *   [Promise3, Lambda2, State2] {
	 *     SetPromiseValue(Promise3, Lambda2, TFuture<FString>(State2))
	 *   }
	 *
	 * 步骤4.2: SetPromiseValue() 内部
	 *   - 传入: Promise3, Lambda2, TFuture<FString>(State2)
	 *   - TFuture<FString>(State2) 的状态已完成，值为 "Number: 12345"
	 *   - 调用: Promise3.SetValue(Lambda2(MoveTemp(StrFuture)))
	 *
	 * 步骤4.3: Lambda2 执行
	 *   [](TFuture<FString> StrFuture) -> int32 {
	 *     FString Str = StrFuture.Consume();  // Str = "Number: 12345"
	 *     int32 Len = Str.Len();               // Len = 14
	 *     UE_LOG(LogTemp, Log, TEXT("  [Then 2] \"%s\" -> %d"), *Str, Len);
	 *     return Len;  // 返回 14
	 *   }
	 *   - 输出: [Then 2] "Number: 12345" -> 14
	 *   - 返回: 14
	 *
	 * 步骤4.4: Promise3.SetValue(14)
	 *   - 调用 State3->EmplaceValue(14)
	 *   - State3.MarkComplete() 被调用:
	 *     a. State3 没有 CompletionCallback (这是最后一个Future)
	 *     b. 标记 bComplete = true
	 *     c. Trigger() 唤醒等待线程
	 *   - Future3 的状态变为已完成，值为 14
	 *
	 * ==================== 阶段5: 获取最终结果 ====================
	 *
	 * 步骤5.1: FinalFuture.Get()
	 *   - Future3 已经完成
	 *   - 直接返回值: 14
	 *
	 * 步骤5.2: UE_LOG 输出
	 *   - 输出: Final result: 14
	 *
	 * ==================== 执行时序总结 ====================
	 *
	 * [主线程]
	 *   1. Promise.GetFuture() → Future1
	 *   2. Future1.Then(Lambda1) → Future2 (设置Callback1到State1)
	 *   3. Future2.Then(Lambda2) → Future3 (设置Callback2到State2)
	 *   4. Promise.SetValue(12345)
	 *       ↓
	 *   5. State1.MarkComplete()
	 *       ↓ 触发
	 *   6. Callback1 执行
	 *       ↓ 调用
	 *   7. Lambda1: 12345 → "Number: 12345"
	 *       ↓ Promise2.SetValue()
	 *   8. State2.MarkComplete()
	 *       ↓ 触发
	 *   9. Callback2 执行
	 *       ↓ 调用
	 *  10. Lambda2: "Number: 12345" → 14
	 *       ↓ Promise3.SetValue()
	 *  11. State3.MarkComplete() → Future3 完成
	 *       ↓
	 *  12. FinalFuture.Get() → 14
	 *
	 * 输出顺序:
	 *   [Then 1] 12345 -> "Number: 12345"
	 *   [Then 2] "Number: 12345" -> 14
	 *   Final result: 14
	 *
	 * ==================== 关键设计点 ====================
	 *
	 * 1. 链式调用通过创建新的 Promise-Future 对实现
	 * 2. 每个 Then() 调用会消耗前一个 Future 的 State
	 * 3. 前一个 Future 完成时，自动触发下一个回调
	 * 4. 整个链是同步执行的（在同一个线程中）
	 * 5. 如果需要异步执行，需要在回调中使用 Async()
	 */
	
	TPromise<int32> Promise;
	
	// 链式转换: int32 -> FString -> int32
	TFuture<int32> FinalFuture = Promise.GetFuture()
		// 第一个Then: int32 -> FString
		.Then([](TFuture<int32> IntFuture) -> FString
		{
			int32 Value = IntFuture.Consume();
			FString Result = FString::Printf(TEXT("Number: %d"), Value);
			UE_LOG(LogTemp, Log, TEXT("  [Then 1] %d -> \"%s\""), Value, *Result);
			return Result;
		})
		// 第二个Then: FString -> int32
		.Then([](TFuture<FString> StrFuture) -> int32
		{
			FString Str = StrFuture.Consume();
			int32 Len = Str.Len();
			UE_LOG(LogTemp, Log, TEXT("  [Then 2] \"%s\" -> %d"), *Str, Len);
			return Len;
		});
	
	// 设置初始值 - 触发整个链式执行
	Promise.SetValue(12345);
	
	// 获取最终结果
	int32 FinalResult = FinalFuture.Get();
	UE_LOG(LogTemp, Log, TEXT("  Final result: %d"), FinalResult);
}

// ============================================================================
// 示例4: Next简化链式调用
// ============================================================================
void ATFuture_TPromise_Example::Example_NextChaining()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 4] Next() Simplified Chaining"));
	
	/*
	 * Next() vs Then() 对比:
	 * 
	 * Then():
	 *   - 回调签名: ReturnType Func(TFuture<T>)
	 *   - 需要手动 Consume() 或 Get()
	 *   - 更灵活
	 * 
	 * Next():
	 *   - 回调签名: ReturnType Func(T)
	 *   - 自动 Consume() 并传递值
	 *   - 更简洁
	 *
	 * ==================== Next() 内部实现原理 ====================
	 *
	 * 源码实现 (Future.h:699-723):
	 * 
	 * template<typename Func>
	 * auto Next(Func Continuation)
	 * {
	 *     return this->Then([Continuation = MoveTemp(Continuation)](TFuture<ResultType> Self) mutable
	 *     {
	 *         if constexpr (std::is_void_v<ResultType>)
	 *         {
	 *             Self.Consume();
	 *             return Continuation();
	 *         }
	 *         else
	 *         {
	 *             // 关键：自动 Consume() 并传递裸值！
	 *             return Continuation(Self.Consume());
	 *         }
	 *     });
	 * }
	 *
	 * Next() 本质上是 Then() 的语法糖，内部自动处理 Consume()
	 *
	 * ==================== Next() 链式调用完整执行流程 ====================
	 *
	 * 执行流程详解:
	 *
	 * ==================== 阶段1: 链式构建 ====================
	 *
	 * 步骤1.1: Promise.GetFuture()
	 *   - 创建 TPromise<int32> Promise
	 *   - 创建 TFuture<int32> Future1
	 *   - Future1.State = TSharedPtr<TFutureState<int32>>
	 *
	 * 步骤1.2: 第一个Next()调用
	 *   .Next([](int32 Value) -> int32 { return Value * 2; })
	 *   
	 *   内部转换过程（Next() → Then()）:
	 *   a. Next() 内部创建包装 Lambda (Lambda1_Wrapper):
	 *      [](TFuture<int32> Self) -> int32 {
	 *          // 自动 Consume() 并传递裸值！
	 *          return UserLambda1(Self.Consume());
	 *      }
	 *   b. UserLambda1 是用户传入的: [](int32 Value) -> int32 { return Value * 2; }
	 *   c. 调用 Then(Lambda1_Wrapper)
	 *
	 *   Then() 内部操作:
	 *   d. 创建 TPromise<int32> Promise2
	 *   e. 获取 TFuture<int32> Future2
	 *   f. 创建 Callback1:
	 *      [Promise2, Lambda1_Wrapper, Future1.State] {
	 *        SetPromiseValue(Promise2, Lambda1_Wrapper, TFuture<int32>(Future1.State))
	 *      }
	 *   g. 将 Future1.State 移动到 MovedState
	 *   h. MovedState->SetContinuation(Callback1)
	 *   i. Future1.State = nullptr (Future1失效)
	 *   j. 返回 Future2 (状态: 未完成)
	 *
	 * 步骤1.3: 第二个Next()调用
	 *   .Next([](int32 Value) -> FString {
	 *        return FString::Printf(TEXT("Result=%d"), Value);
	 *      })
	 *   
	 *   内部转换过程（Next() → Then()）:
	 *   a. Next() 内部创建包装 Lambda (Lambda2_Wrapper):
	 *      [](TFuture<int32> Self) -> FString {
	 *          // 自动 Consume() 并传递裸值！
	 *          return UserLambda2(Self.Consume());
	 *      }
	 *   b. UserLambda2 是用户传入的: [](int32 Value) -> FString { ... }
	 *   c. 调用 Then(Lambda2_Wrapper)
	 *
	 *   Then() 内部操作:
	 *   d. 创建 TPromise<FString> Promise3
	 *   e. 获取 TFuture<FString> Future3 = ResultFuture
	 *   f. 创建 Callback2:
	 *      [Promise3, Lambda2_Wrapper, Future2.State] {
	 *        SetPromiseValue(Promise3, Lambda2_Wrapper, TFuture<int32>(Future2.State))
	 *      }
	 *   g. 将 Future2.State 移动到 MovedState
	 *   h. MovedState->SetContinuation(Callback2)
	 *   i. Future2.State = nullptr (Future2失效)
	 *   j. 返回 Future3 = ResultFuture (状态: 未完成)
	 *
	 *   此时状态:
	 *   - Promise: 未设置值
	 *   - State1: 未完成, 保存 Callback1
	 *   - State2: 未完成, 保存 Callback2
	 *   - ResultFuture: 最终Future, 状态未完成
	 *
	 * ==================== 阶段2: 触发执行 ====================
	 *
	 * 步骤2.1: Promise.SetValue(21)
	 *   - Promise.SetValue() 调用 State1->EmplaceValue(21)
	 *   - State1.MarkComplete() 被调用:
	 *     a. 取出 State1 的 CompletionCallback = Callback1
	 *     b. 标记 bComplete = true
	 *     c. Trigger() 唤醒等待线程
	 *     d. 执行 Callback1()
	 *
	 * ==================== 阶段3: Callback1 执行 ====================
	 *
	 * 步骤3.1: Callback1 执行
	 *   [Promise2, Lambda1_Wrapper, State1] {
	 *     SetPromiseValue(Promise2, Lambda1_Wrapper, TFuture<int32>(State1))
	 *   }
	 *
	 * 步骤3.2: SetPromiseValue() 内部
	 *   - 传入: Promise2, Lambda1_Wrapper, TFuture<int32>(State1)
	 *   - TFuture<int32>(State1) 的状态已完成，值为 21
	 *   - 调用: Promise2.SetValue(Lambda1_Wrapper(MoveTemp(IntFuture)))
	 *
	 * 步骤3.3: Lambda1_Wrapper 执行 (关键：自动 Consume)
	 *   [](TFuture<int32> Self) -> int32 {
	 *       return UserLambda1(Self.Consume());
	 *   }
	 *   - Self.Consume() 调用: 从 State1 获取值 21
	 *   - 返回 UserLambda1(21)
	 *
	 * 步骤3.4: UserLambda1 执行（用户传入的 Lambda）
	 *   [](int32 Value) -> int32 {
	 *     UE_LOG(LogTemp, Log, TEXT("  [Next 1] Received: %d"), Value);
	 *     return Value * 2;  // 加倍
	 *   }
	 *   - Value = 21
	 *   - 输出: [Next 1] Received: 21
	 *   - 返回: 21 * 2 = 42
	 *
	 * 步骤3.5: Promise2.SetValue(42)
	 *   - 调用 State2->EmplaceValue(42)
	 *   - State2.MarkComplete() 被调用:
	 *     a. 取出 State2 的 CompletionCallback = Callback2
	 *     b. 标记 bComplete = true
	 *     c. Trigger() 唤醒等待线程
	 *     d. 执行 Callback2()
	 *
	 * ==================== 阶段4: Callback2 执行 ====================
	 *
	 * 步骤4.1: Callback2 执行
	 *   [Promise3, Lambda2_Wrapper, State2] {
	 *     SetPromiseValue(Promise3, Lambda2_Wrapper, TFuture<int32>(State2))
	 *   }
	 *
	 * 步骤4.2: SetPromiseValue() 内部
	 *   - 传入: Promise3, Lambda2_Wrapper, TFuture<int32>(State2)
	 *   - TFuture<int32>(State2) 的状态已完成，值为 42
	 *   - 调用: Promise3.SetValue(Lambda2_Wrapper(MoveTemp(IntFuture)))
	 *
	 * 步骤4.3: Lambda2_Wrapper 执行 (关键：自动 Consume)
	 *   [](TFuture<int32> Self) -> FString {
	 *       return UserLambda2(Self.Consume());
	 *   }
	 *   - Self.Consume() 调用: 从 State2 获取值 42
	 *   - 返回 UserLambda2(42)
	 *
	 * 步骤4.4: UserLambda2 执行（用户传入的 Lambda）
	 *   [](int32 Value) -> FString {
	 *     UE_LOG(LogTemp, Log, TEXT("  [Next 2] Doubled: %d"), Value);
	 *     return FString::Printf(TEXT("Result=%d"), Value);
	 *   }
	 *   - Value = 42
	 *   - 输出: [Next 2] Doubled: 42
	 *   - 返回: "Result=42"
	 *
	 * 步骤4.5: Promise3.SetValue("Result=42")
	 *   - 调用 State3->EmplaceValue("Result=42")
	 *   - State3.MarkComplete() 被调用:
	 *     a. State3 没有 CompletionCallback (这是最后一个Future)
	 *     b. 标记 bComplete = true
	 *     c. Trigger() 唤醒等待线程
	 *   - ResultFuture 的状态变为已完成，值为 "Result=42"
	 *
	 * ==================== 阶段5: 获取最终结果 ====================
	 *
	 * 步骤5.1: ResultFuture.Consume()
	 *   - ResultFuture 已经完成
	 *   - 直接返回值: "Result=42"
	 *
	 * 步骤5.2: UE_LOG 输出
	 *   - 输出: Final: Result=42
	 *
	 * ==================== 执行时序总结 ====================
	 *
	 * [主线程]
	 *   1. Promise.GetFuture() → Future1
	 *   2. Future1.Next(UserLambda1) 
	 *      → 内部: Next() 创建 Lambda1_Wrapper
	 *      → 内部: Then(Lambda1_Wrapper) → Future2 (设置Callback1到State1)
	 *   3. Future2.Next(UserLambda2)
	 *      → 内部: Next() 创建 Lambda2_Wrapper
	 *      → 内部: Then(Lambda2_Wrapper) → Future3 (设置Callback2到State2)
	 *   4. Promise.SetValue(21)
	 *       ↓
	 *   5. State1.MarkComplete()
	 *       ↓ 触发
	 *   6. Callback1 执行
	 *       ↓ 调用
	 *   7. Lambda1_Wrapper: Self.Consume() → 21
	 *       ↓ 调用
	 *   8. UserLambda1: 21 → 42
	 *       ↓ Promise2.SetValue()
	 *   9. State2.MarkComplete()
	 *       ↓ 触发
	 *  10. Callback2 执行
	 *       ↓ 调用
	 *  11. Lambda2_Wrapper: Self.Consume() → 42
	 *       ↓ 调用
	 *  12. UserLambda2: 42 → "Result=42"
	 *       ↓ Promise3.SetValue()
	 *  13. State3.MarkComplete() → ResultFuture 完成
	 *       ↓
	 *  14. ResultFuture.Consume() → "Result=42"
	 *
	 * 输出顺序:
	 *   [Next 1] Received: 21
	 *   [Next 2] Doubled: 42
	 *   Final: Result=42
	 *
	 * ==================== Next() 的关键优势 ====================
	 *
	 * 对比 Then() 和 Next():
	 *
	 * 使用 Then():
	 *   .Then([](TFuture<int32> F) -> int32 {
	 *       int32 Value = F.Consume();  // 手动 Consume()
	 *       return Value * 2;
	 *   })
	 *
	 * 使用 Next():
	 *   .Next([](int32 Value) -> int32 {  // 自动 Consume()
	 *       return Value * 2;
	 *   })
	 *
	 * Next() 的优势:
	 * 1. 更简洁：不需要手动 Consume()
	 * 2. 更直观：回调直接接收值，而不是 TFuture 包装
	 * 3. 更安全：自动处理 Consume 逻辑，避免错误
	 *
	 * 适用场景:
	 * - Next(): 简单的值转换场景（推荐）
	 * - Then(): 需要访问 Future 其他方法或更复杂逻辑的场景
	 */
	
	TPromise<int32> Promise;
	
	// Next自动Consume并传递裸值
	TFuture<FString> ResultFuture = Promise.GetFuture()
		.Next([](int32 Value) -> int32
		{
			UE_LOG(LogTemp, Log, TEXT("  [Next 1] Received: %d"), Value);
			return Value * 2;  // 加倍
		})
		.Next([](int32 Value) -> FString
		{
			UE_LOG(LogTemp, Log, TEXT("  [Next 2] Doubled: %d"), Value);
			return FString::Printf(TEXT("Result=%d"), Value);
		});
	
	// 设置值触发执行
	Promise.SetValue(21);
	
	FString FinalStr = ResultFuture.Consume();
	UE_LOG(LogTemp, Log, TEXT("  Final: %s"), *FinalStr);
}

// ============================================================================
// 示例5: TSharedFuture共享
// ============================================================================
void ATFuture_TPromise_Example::Example_SharedFuture()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 5] TSharedFuture - Shared Waiting"));
	
	/*
	 * TFuture vs TSharedFuture:
	 * 
	 * TFuture:
	 *   - 独占所有权 (move-only)
	 *   - 支持 Consume(), GetMutable(), Then(), Next()
	 *   - 只能有一个等待者
	 * 
	 * TSharedFuture:
	 *   - 共享所有权 (copyable)
	 *   - 只支持 Get() 返回const引用
	 *   - 可以有多个等待者
	 *   - 不支持链式调用
	 */
	
	TPromise<FString> Promise;
	
	// TFuture -> TSharedFuture 转换 (消耗TFuture)
	TSharedFuture<FString> Shared = Promise.GetFuture().Share();
	
	// TSharedFuture可以复制
	TSharedFuture<FString> Copy1 = Shared;
	TSharedFuture<FString> Copy2 = Shared;
	TSharedFuture<FString> Copy3 = Shared;
	
	// 异步设置值
	Async(EAsyncExecution::Thread, [Promise = MoveTemp(Promise)]() mutable
	{
		FPlatformProcess::Sleep(0.02f);
		Promise.SetValue(TEXT("Shared Value"));
	});
	
	// 多处同时等待同一个结果
	const FString& R1 = Copy1.Get();
	const FString& R2 = Copy2.Get();
	const FString& R3 = Copy3.Get();
	
	// 所有副本获取相同结果
	UE_LOG(LogTemp, Log, TEXT("  Copy1: %s"), *R1);
	UE_LOG(LogTemp, Log, TEXT("  Copy2: %s"), *R2);
	UE_LOG(LogTemp, Log, TEXT("  Copy3: %s"), *R3);
	
	// SharedFuture保持有效,可以持续访问
	check(Copy1.IsValid() && Copy2.IsValid() && Copy3.IsValid());
}

// ============================================================================
// 示例6: void类型Future
// ============================================================================
void ATFuture_TPromise_Example::Example_VoidFuture()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 6] TFuture<void> - Completion Signal"));
	
	/*
	 * TFuture<void> / TPromise<void>:
	 * 
	 * 用于表示"操作完成"而不需要返回值的场景
	 * 
	 * SetValue() 不需要参数:
	 *   void SetValue() requires(std::is_void_v<ResultType>)
	 *   { EmplaceValue(); }
	 */
	
	TPromise<void> Promise;
	TFuture<void> Future = Promise.GetFuture();
	
	Async(EAsyncExecution::Thread, [Promise = MoveTemp(Promise)]() mutable
	{
		UE_LOG(LogTemp, Log, TEXT("  [Async] Starting operation..."));
		FPlatformProcess::Sleep(0.03f);
		UE_LOG(LogTemp, Log, TEXT("  [Async] Operation done, signaling completion"));
		
		// void类型不需要参数
		Promise.SetValue();
	});
	
	// Wait()等待完成,不返回值
	Future.Wait();
	UE_LOG(LogTemp, Log, TEXT("  [Main] Received completion signal"));
}

// ============================================================================
// 示例7: 与Async配合
// ============================================================================
void ATFuture_TPromise_Example::Example_WithAsync()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 7] Integration with Async()"));
	
	/*
	 * Async() 函数直接返回 TFuture:
	 * 
	 * template<typename ResultType>
	 * TFuture<ResultType> Async(
	 *     EAsyncExecution Execution,
	 *     TFunction<ResultType()>&& Function
	 * );
	 * 
	 * EAsyncExecution选项:
	 *   - Thread: 新建专用线程
	 *   - ThreadPool: 使用全局线程池
	 *   - TaskGraph: 使用任务图系统
	 *   - TaskGraphMainThread: 任务图,主线程执行
	 */
	
	// Async直接返回TFuture,内部自动创建Promise
	TFuture<int32> Future = Async(EAsyncExecution::ThreadPool, []() -> int32
	{
		// 在线程池中执行计算
		int32 Sum = 0;
		for (int32 i = 1; i <= 100; ++i)
		{
			Sum += i;
		}
		UE_LOG(LogTemp, Log, TEXT("  [ThreadPool] Computed sum: %d"), Sum);
		return Sum;
	});
	
	// 链式处理结果
	Future.Next([](int32 Result)
	{
		UE_LOG(LogTemp, Log, TEXT("  [Callback] Received result: %d"), Result);
	});
	
	// 注意: 不要在这里Wait,因为Next回调可能在任意线程执行
}

// ============================================================================
// 示例8: 非阻塞完成回调
// ============================================================================
void ATFuture_TPromise_Example::Example_NonBlockingCallback()
{
	UE_LOG(LogTemp, Log, TEXT("[Example 8] Non-Blocking Completion Callback"));
	
	/*
	 * 避免GameThread阻塞的最佳实践:
	 * 
	 * 不推荐 (会阻塞GameThread):
	 *   Future.Wait();
	 *   Future.Get();
	 * 
	 * 推荐 (非阻塞):
	 *   Future.Next([](ResultType Result) {
	 *       // 处理结果
	 *   });
	 * 
	 * 回调执行线程:
	 *   - 如果设置回调时Future已完成: 在调用者线程立即执行
	 *   - 如果设置回调时Future未完成: 在调用SetValue的线程执行
	 * 
	 * 如需回到GameThread,使用AsyncTask:
	 *   Future.Next([](ResultType Result) {
	 *       AsyncTask(ENamedThreads::GameThread, [Result]() {
	 *           // 在GameThread处理
	 *       });
	 *   });
	 */
	
	TPromise<FString> Promise;
	TFuture<FString> Future = Promise.GetFuture();
	
	// 设置非阻塞回调
	Future.Next([](FString Result)
	{
		// 注意: 这个回调可能在任意线程执行!
		UE_LOG(LogTemp, Log, TEXT("  [Callback] Result received: %s"), *Result);
		
		// 如果需要在GameThread处理UI等操作:
		AsyncTask(ENamedThreads::GameThread, [Result]()
		{
			UE_LOG(LogTemp, Log, TEXT("  [GameThread] Processing: %s"), *Result);
		});
	});
	
	// 在另一个线程完成Promise
	Async(EAsyncExecution::Thread, [Promise = MoveTemp(Promise)]() mutable
	{
		FPlatformProcess::Sleep(0.02f);
		Promise.SetValue(TEXT("Async Result"));
	});
	
	// GameThread继续执行其他工作,不阻塞
	UE_LOG(LogTemp, Log, TEXT("  [Main] Continuing without blocking..."));
}


