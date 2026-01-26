// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"
#include "TPimplPtr_Example.generated.h"

/**
 * ATPimplPtr_Example - 演示TPimplPtr（Pimpl惯用法）的使用
 * 
 * Pimpl（Pointer to Implementation）惯用法的优势：
 * 1. 减少编译依赖 - 修改实现不会触发头文件依赖者重新编译
 * 2. 隐藏实现细节 - 私有成员完全隐藏在cpp文件中
 * 3. 二进制兼容性 - 修改实现不改变类的内存布局
 */
UCLASS()
class UNREALTEMPLATESGUIDE_API ATPimplPtr_Example : public AActor
{
	GENERATED_BODY()

public:
	ATPimplPtr_Example();
	
	// 析构函数必须在cpp中定义，因为需要FImpl的完整定义
	virtual ~ATPimplPtr_Example();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// =====================================================
	// 公共接口 - 这些方法委托给内部实现
	// =====================================================
	
	/** 设置Actor的名称 */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	void SetActorDisplayName(const FString& NewName);
	
	/** 获取Actor的名称 */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	FString GetActorDisplayName() const;
	
	/** 增加计数器 */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	void IncrementCounter();
	
	/** 获取当前计数 */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	int32 GetCounter() const;
	
	/** 重置状态 */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	void ResetState();
	
	/** 执行内部计算（演示复杂操作） */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	float PerformCalculation(float InputValue);
	
	/** 检查实现是否有效 */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	bool IsImplValid() const;
	
	/** 打印调试信息 */
	UFUNCTION(BlueprintCallable, Category = "PimplExample")
	void PrintDebugInfo() const;

private:
	// =====================================================
	// Pimpl核心 - 前向声明 + TPimplPtr
	// =====================================================
	
	/**
	 * FImpl - 内部实现结构体
	 * 
	 * 这里只需要前向声明，完整定义在cpp文件中
	 * 这样修改FImpl的成员不会导致包含此头文件的文件重新编译
	 */
	struct FImpl;
	
	/**
	 * TPimplPtr<FImpl> - 指向实现的智能指针
	 * 
	 * 特点：
	 * - 自动管理内存
	 * - 不可拷贝（默认NoCopy模式）
	 * - 支持移动语义
	 * - 类似std::unique_ptr但更好地支持不完整类型
	 */
	TPimplPtr<FImpl> Impl;
};
