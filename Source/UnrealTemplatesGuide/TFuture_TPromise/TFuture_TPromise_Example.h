// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "TFuture_TPromise_Example.generated.h"

UCLASS()
class UNREALTEMPLATESGUIDE_API ATFuture_TPromise_Example : public AActor
{
	GENERATED_BODY()

public:
	ATFuture_TPromise_Example();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	// ========================================================================
	// 示例方法
	// ========================================================================
	
	/** 示例1: 基础Promise-Future模式 */
	void Example_BasicPromiseFuture();
	
	/** 示例2: Get vs Consume */
	void Example_GetVsConsume();
	
	/** 示例3: Then链式调用 */
	void Example_ThenChaining();
	
	/** 示例4: Next简化链式调用 */
	void Example_NextChaining();
	
	/** 示例5: TSharedFuture共享 */
	void Example_SharedFuture();
	
	/** 示例6: void类型Future */
	void Example_VoidFuture();
	
	/** 示例7: 与Async配合 */
	void Example_WithAsync();
	
	/** 示例8: 非阻塞完成回调 */
	void Example_NonBlockingCallback();

private:
	// 用于演示的Future成员
	TFuture<int32> PendingFuture;
	TSharedFuture<FString> SharedResult;
};
