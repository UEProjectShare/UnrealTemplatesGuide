// Fill out your copyright notice in the Description page of Project Settings.

#include "TPimplPtr_Example.h"

// =====================================================
// FImpl - 内部实现的完整定义
// =====================================================
// 这些头文件只在cpp中包含，修改不会影响包含.h的文件
// #include "SomeHeavyDependency.h"  // 示例：重量级依赖
// #include "ComplexSystem.h"        // 示例：复杂系统

/**
 * FImpl结构体 - 包含所有私有实现细节
 * 
 * 优势：
 * 1. 所有依赖都隐藏在cpp中
 * 2. 可以自由添加/删除成员而不影响ABI
 * 3. 头文件保持简洁
 */
struct ATPimplPtr_Example::FImpl
{
	// =====================================================
	// 成员变量 - 这些在头文件中完全不可见
	// =====================================================
	
	/** Actor的显示名称 */
	FString DisplayName;
	
	/** 计数器 */
	int32 Counter;
	
	/** 累积时间 */
	float AccumulatedTime;
	
	/** 上次计算结果 */
	float LastCalculationResult;
	
	/** 是否已初始化 */
	bool bIsInitialized;
	
	/** 内部状态数组 */
	TArray<float> StateHistory;
	
	// =====================================================
	// 构造函数
	// =====================================================
	
	/** 默认构造 */
	FImpl()
		: DisplayName(TEXT("DefaultPimplActor"))
		, Counter(0)
		, AccumulatedTime(0.0f)
		, LastCalculationResult(0.0f)
		, bIsInitialized(false)
	{
		StateHistory.Reserve(100);
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] FImpl 默认构造完成"));
	}
	
	/** 带参数构造 */
	FImpl(const FString& InName, int32 InInitialCounter)
		: DisplayName(InName)
		, Counter(InInitialCounter)
		, AccumulatedTime(0.0f)
		, LastCalculationResult(0.0f)
		, bIsInitialized(false)
	{
		StateHistory.Reserve(100);
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] FImpl 参数构造: Name=%s, Counter=%d"), *InName, InInitialCounter);
	}
	
	/** 析构函数 */
	~FImpl()
	{
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] FImpl 析构: Name=%s, FinalCounter=%d"), *DisplayName, Counter);
	}
	
	// =====================================================
	// 内部方法
	// =====================================================
	
	/** 初始化 */
	void Initialize()
	{
		if (!bIsInitialized)
		{
			bIsInitialized = true;
			StateHistory.Add(0.0f);
			UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] FImpl 初始化完成"));
		}
	}
	
	/** 更新状态 */
	void Update(float DeltaTime)
	{
		AccumulatedTime += DeltaTime;
		
		// 每秒记录一次状态
		if (FMath::FloorToInt(AccumulatedTime) > StateHistory.Num())
		{
			StateHistory.Add(LastCalculationResult);
		}
	}
	
	/** 执行计算 */
	float Calculate(float Input)
	{
		// 示例计算：结合计数器和累积时间
		LastCalculationResult = Input * (Counter + 1) + FMath::Sin(AccumulatedTime);
		return LastCalculationResult;
	}
	
	/** 重置 */
	void Reset()
	{
		Counter = 0;
		AccumulatedTime = 0.0f;
		LastCalculationResult = 0.0f;
		StateHistory.Empty();
		StateHistory.Add(0.0f);
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] FImpl 状态已重置"));
	}
	
	/** 打印调试信息 */
	void PrintDebug() const
	{
		UE_LOG(LogTemp, Warning, TEXT("========== TPimplPtr Debug Info =========="));
		UE_LOG(LogTemp, Warning, TEXT("  DisplayName: %s"), *DisplayName);
		UE_LOG(LogTemp, Warning, TEXT("  Counter: %d"), Counter);
		UE_LOG(LogTemp, Warning, TEXT("  AccumulatedTime: %.2f"), AccumulatedTime);
		UE_LOG(LogTemp, Warning, TEXT("  LastCalculation: %.4f"), LastCalculationResult);
		UE_LOG(LogTemp, Warning, TEXT("  IsInitialized: %s"), bIsInitialized ? TEXT("Yes") : TEXT("No"));
		UE_LOG(LogTemp, Warning, TEXT("  StateHistory Count: %d"), StateHistory.Num());
		UE_LOG(LogTemp, Warning, TEXT("=========================================="));
	}
};

// =====================================================
// ATPimplPtr_Example 实现
// =====================================================

ATPimplPtr_Example::ATPimplPtr_Example()
{
	PrimaryActorTick.bCanEverTick = true;
	
	// 使用MakePimpl创建实现对象
	// MakePimpl是工厂函数，类似std::make_unique
	Impl = MakePimpl<FImpl>(TEXT("PimplExampleActor"), 0);
	
	UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] ATPimplPtr_Example 构造完成"));
}

// 析构函数必须在cpp中定义
// 因为编译器需要FImpl的完整定义才能正确调用析构
ATPimplPtr_Example::~ATPimplPtr_Example()
{
	// TPimplPtr会自动处理内存释放，无需手动操作
	UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] ATPimplPtr_Example 析构"));
}

void ATPimplPtr_Example::BeginPlay()
{
	Super::BeginPlay();
	
	// 通过箭头操作符访问实现
	if (Impl.IsValid())
	{
		Impl->Initialize();
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] BeginPlay - 实现已初始化"));
	}
	
	// 演示基本使用
	PrintDebugInfo();
}

void ATPimplPtr_Example::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// 通过指针访问内部更新
	if (Impl)  // operator bool() 检查有效性
	{
		Impl->Update(DeltaTime);
	}
}

// =====================================================
// 公共接口实现 - 委托给FImpl
// =====================================================

void ATPimplPtr_Example::SetActorDisplayName(const FString& NewName)
{
	if (Impl.IsValid())
	{
		Impl->DisplayName = NewName;
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] 设置名称: %s"), *NewName);
	}
}

FString ATPimplPtr_Example::GetActorDisplayName() const
{
	// 使用Get()获取原始指针
	if (const FImpl* RawPtr = Impl.Get())
	{
		return RawPtr->DisplayName;
	}
	return TEXT("Invalid");
}

void ATPimplPtr_Example::IncrementCounter()
{
	if (Impl)
	{
		Impl->Counter++;
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] 计数器增加到: %d"), Impl->Counter);
	}
}

int32 ATPimplPtr_Example::GetCounter() const
{
	return Impl.IsValid() ? Impl->Counter : -1;
}

void ATPimplPtr_Example::ResetState()
{
	if (Impl)
	{
		Impl->Reset();
	}
}

float ATPimplPtr_Example::PerformCalculation(float InputValue)
{
	if (Impl)
	{
		float Result = Impl->Calculate(InputValue);
		UE_LOG(LogTemp, Log, TEXT("[TPimplPtr] 计算结果: Input=%.2f, Output=%.4f"), InputValue, Result);
		return Result;
	}
	return 0.0f;
}

bool ATPimplPtr_Example::IsImplValid() const
{
	// 两种检查方式都可以
	// return Impl.IsValid();
	return static_cast<bool>(Impl);  // operator bool()
}

void ATPimplPtr_Example::PrintDebugInfo() const
{
	if (Impl)
	{
		// 使用operator*()解引用
		(*Impl).PrintDebug();
		
		// 或者使用operator->()
		// Impl->PrintDebug();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TPimplPtr] Impl 无效!"));
	}
}
