// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tasks/Task.h"
#include "Tasks/Pipe.h"
#include "Tasks/TaskConcurrencyLimiter.h"
#include "Tasks_System_Example.generated.h"

UCLASS()
class UNREALTEMPLATESGUIDE_API ATasks_System_Example : public AActor
{
	GENERATED_BODY()

public:
	ATasks_System_Example();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	// ========================================================================
	// 基础示例 (示例 1-10)
	// ========================================================================
	
	/** 示例1: 基础任务启动 (Launch) */
	void Example_BasicLaunch();
	
	/** 示例2: 带返回值的任务 */
	void Example_TaskWithResult();
	
	/** 示例3: 任务先决条件 (Prerequisites) */
	void Example_Prerequisites();
	
	/** 示例4: FTaskEvent 同步原语 */
	void Example_TaskEvent();
	
	/** 示例5: 嵌套任务 (Nested Tasks) */
	void Example_NestedTasks();
	
	/** 示例6: FPipe 管道串行执行 */
	void Example_Pipe();
	
	/** 示例7: 等待多个任务 (Wait / WaitAny / Any) */
	void Example_WaitMultipleTasks();
	
	/** 示例8: 任务取消 (FCancellationToken) */
	void Example_CancellationToken();

	/** 示例9: 任务优先级 */
	void Example_TaskPriority();

	/** 示例10: MakeCompletedTask 立即完成的任务 */
	void Example_MakeCompletedTask();

	// ========================================================================
	// 进阶示例 (示例 11-20, 来自官方 TasksTest.cpp 的补充用例)
	// ========================================================================

	/** 示例11: Fire-and-Forget / Mutable Lambda / 任务句柄重置 */
	void Example_FireAndForget_MutableLambda_Reset();

	/** 示例12: 任务内部访问自身 (Task.Launch 成员函数) */
	void Example_AccessTaskFromInside();

	/** 示例13: IsAwaitable() 死锁检测 */
	void Example_IsAwaitable();

	/** 示例14: Pipe 作为异步类 ("Primitive Actor" 模式) */
	void Example_PipeAsAsyncClass();

	/** 示例15: FPipeSuspensionScope (Pipe 挂起/恢复) */
	void Example_PipeSuspension();

	/** 示例16: 命名线程任务 (Named Thread Task) */
	void Example_NamedThreadTask();

	/** 示例17: FTaskPriorityCVar (控制台变量配置优先级) */
	void Example_TaskPriorityCVar();

	/** 示例18: FTaskConcurrencyLimiter (并发限制器) */
	void Example_TaskConcurrencyLimiter();

	/** 示例19: 带先决条件的管道任务不阻塞管道 / Move-Only 结果类型 */
	void Example_PipedPrereqAndMoveOnlyResult();

	/** 示例20: 深度撤回 (Deep Retraction) / Wait 带超时 */
	void Example_DeepRetraction_WaitTimeout();
};
