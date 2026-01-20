// Fill out your copyright notice in the Description page of Project Settings.

#include "UE_ARRAY_COUNT_Example.h"

//=============================================================================
// 示例数据：结构体数组 - 常用于配置表
//=============================================================================
struct FTestStruct
{
	const TCHAR* NamePostFix;
};

static constexpr FTestStruct TestStruct[] = 
{
	{ TEXT("_Functions"), },
	{ TEXT("_InitOnly"), },
	{ TEXT("_LifetimeConditionals"), },
	{ TEXT("_State"), },
};

//=============================================================================
// 示例数据：基础类型数组
//=============================================================================
static const int32 DamageMultipliers[] = { 1, 2, 4, 8, 16 };

//=============================================================================
// 示例数据：字符串数组 - 常用于枚举转字符串
//=============================================================================
static const TCHAR* WeaponTypeNames[] = 
{
	TEXT("Sword"),
	TEXT("Bow"),
	TEXT("Staff"),
	TEXT("Dagger"),
};

AUE_ARRAY_COUNT_Example::AUE_ARRAY_COUNT_Example()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AUE_ARRAY_COUNT_Example::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("========== UE_ARRAY_COUNT 示例 =========="));
	
	//=========================================================================
	// 用法1: 基本计数
	//=========================================================================
	constexpr uint32 StructCount = UE_ARRAY_COUNT(TestStruct);
	constexpr uint32 IntCount = UE_ARRAY_COUNT(DamageMultipliers);
	constexpr uint32 StringCount = UE_ARRAY_COUNT(WeaponTypeNames);
	
	UE_LOG(LogTemp, Log, TEXT("[基本计数]"));
	UE_LOG(LogTemp, Log, TEXT("  TestStruct 元素个数: %d"), StructCount);
	UE_LOG(LogTemp, Log, TEXT("  DamageMultipliers 元素个数: %d"), IntCount);
	UE_LOG(LogTemp, Log, TEXT("  WeaponTypeNames 元素个数: %d"), StringCount);
	
	//=========================================================================
	// 用法2: 安全遍历静态数组
	//=========================================================================
	UE_LOG(LogTemp, Log, TEXT("[遍历数组]"));
	for (uint32 i = 0; i < UE_ARRAY_COUNT(TestStruct); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("  TestStruct[%d]: %s"), i, TestStruct[i].NamePostFix);
	}
	
	//=========================================================================
	// 用法3: 用于范围检查（防止数组越界）
	//=========================================================================
	UE_LOG(LogTemp, Log, TEXT("[范围检查]"));
	int32 WeaponIndex = 2;
	if (WeaponIndex >= 0 && WeaponIndex < static_cast<int32>(UE_ARRAY_COUNT(WeaponTypeNames)))
	{
		UE_LOG(LogTemp, Log, TEXT("  武器类型[%d]: %s"), WeaponIndex, WeaponTypeNames[WeaponIndex]);
	}
	
	//=========================================================================
	// 用法4: 编译期常量 - 可用于模板参数或静态断言
	//=========================================================================
	constexpr uint32 ExpectedCount = 4;
	static_assert(UE_ARRAY_COUNT(TestStruct) == ExpectedCount, "TestStruct count mismatch!");
	static_assert(UE_ARRAY_COUNT(WeaponTypeNames) == ExpectedCount, "WeaponTypeNames count mismatch!");
	UE_LOG(LogTemp, Log, TEXT("[静态断言] 编译期验证通过，数组元素个数符合预期"));
	
	//=========================================================================
	// 错误示例（取消注释会编译失败 - 这正是 UE_ARRAY_COUNT 的安全之处）
	//=========================================================================
	// const TCHAR** PointerToArray = WeaponTypeNames;
	// uint32 WrongCount = UE_ARRAY_COUNT(PointerToArray);  // 编译错误！指针不是数组
	
	// 对比：sizeof 方式对指针会给出错误结果（能编译但结果错误）
	// const TCHAR** Ptr = WeaponTypeNames;
	// size_t WrongResult = sizeof(Ptr) / sizeof(Ptr[0]);  // 结果是 1，而非 4
	
	UE_LOG(LogTemp, Log, TEXT("============================================"));
}

void AUE_ARRAY_COUNT_Example::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

