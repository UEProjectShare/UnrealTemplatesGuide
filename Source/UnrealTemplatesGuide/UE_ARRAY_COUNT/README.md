# UE_ARRAY_COUNT

## 定义位置

`Engine/Source/Runtime/Core/Public/Templates/UnrealTemplate.h`

## 功能说明

编译期计算 C 风格静态数组的元素个数，比 `sizeof(arr)/sizeof(arr[0])` 更安全。

## 源码实现

```cpp
#ifdef __clang__
    template <typename T UE_REQUIRES(__is_array(T))>
    auto UEArrayCountHelper(T& t) -> char(&)[sizeof(t) / sizeof(t[0]) + 1];
#else
    template <typename T, uint32 N>
    char (&UEArrayCountHelper(const T (&)[N]))[N + 1];
#endif

#define UE_ARRAY_COUNT(array) (sizeof(UEArrayCountHelper(array)) - 1)
```

## 实现原理

1. `UEArrayCountHelper` 是一个模板函数，接受数组引用，返回 `char[N+1]` 的引用
2. `sizeof(char[N+1]) = N+1`，减去 1 后得到数组元素个数 N
3. Clang 编译器使用 `UE_REQUIRES(__is_array(T))` 约束确保只接受数组类型
4. MSVC 使用模板参数推导 `T(&)[N]` 只匹配数组，指针无法匹配会编译报错

---

## MSVC 版本语法解析

```cpp
template <typename T, uint32 N>
char (&UEArrayCountHelper(const T (&)[N]))[N + 1];
```

这是一个复杂的函数声明，从内向外理解：

### 参数部分

```
const T (&)[N]
```

- 这是一个"数组引用"参数
- 表示引用一个包含 N 个 T 类型元素的数组
- 例如: `int arr[4]` 传入时，`T=int`, `N=4`

#### 返回值部分

```
char (&...)[N + 1]
```

- 返回值是对 `char[N+1]` 数组的引用
- 例如: `N=4` 时，返回 `char(&)[5]`，`sizeof` 得到 5

#### 调用流程示例

```cpp
int arr[4] = {1, 2, 3, 4};

// 编译器自动推导 T = int, N = 4
UEArrayCountHelper(arr);        // 返回 char(&)[5] 的引用

sizeof(UEArrayCountHelper(arr)) // = sizeof(char[5]) = 5
sizeof(...) - 1                 // = 5 - 1 = 4 ✓
```

### 为什么指针会编译失败

```cpp
int* ptr = arr;
UEArrayCountHelper(ptr);  // 编译错误！

// 原因: int* 无法匹配 const T(&)[N] 这个模式
// 模板要求参数必须是"数组引用"，指针不是数组
```

### 等价的易读写法

```cpp
// 使用 using 别名改写：
template <typename T, uint32 N>
using CharArrayRef = char (&)[N + 1];

template <typename T, uint32 N>
CharArrayRef<T, N> UEArrayCountHelper(const T (&)[N]);
```

**核心思想**：利用模板参数推导获取数组大小 N，通过返回类型的 sizeof 把 N 传递出去。

---

## Clang 版本语法解析

```cpp
template <typename T UE_REQUIRES(__is_array(T))>
auto UEArrayCountHelper(T& t) -> char(&)[sizeof(t) / sizeof(t[0]) + 1];
```

### UE_REQUIRES 宏展开

`UE_REQUIRES` 根据编译器版本有两种实现：

**C++20 之前 或 特定 Clang 版本有 bug 时，使用 SFINAE：**

```cpp
#define UE_REQUIRES(...) , std::enable_if_t<(__VA_ARGS__), int> = 0

// 展开后：
template <typename T, std::enable_if_t<(__is_array(T)), int> = 0>
```

**C++20 及以上，使用 concepts：**

```cpp
#define UE_REQUIRES(...) > requires (!!(__VA_ARGS__)) && ...

// 展开后：
template <typename T> requires (!!(__is_array(T))) && ...
```

### 参数部分

```
T& t
```

- T 是数组类型（如 `int[4]`）
- `T&` 就是数组引用（如 `int(&)[4]`）
- 比 MSVC 版本的 `const T(&)[N]` 写法更简洁

### 返回值部分（尾置返回类型）

```cpp
auto ... -> char(&)[sizeof(t) / sizeof(t[0]) + 1]
```

| 表达式 | 说明 |
|--------|------|
| `sizeof(t)` | 整个数组的字节大小，如 `int[4]` = 16 字节 |
| `sizeof(t[0])` | 单个元素的字节大小，如 `int` = 4 字节 |
| `sizeof(t) / sizeof(t[0])` | 元素个数 = 4 |
| `+ 1` | 返回 `char(&)[5]` |

### 调用流程示例

```cpp
int arr[4] = {1, 2, 3, 4};

// T 推导为 int[4]
// __is_array(int[4]) = true，约束通过
// sizeof(t) = 16, sizeof(t[0]) = 4
// 返回 char(&)[16/4 + 1] = char(&)[5]
// sizeof(...) - 1 = 4 ✓
```

---

## SFINAE 机制详解

**SFINAE** = **S**ubstitution **F**ailure **I**s **N**ot **A**n **E**rror（替换失败不是错误）

### 工作原理

```cpp
template <typename T, std::enable_if_t<(__is_array(T)), int> = 0>
```

**条件为 true 时：**

```cpp
std::enable_if_t<true, int> = int
// 模板变成：template <typename T, int = 0>
// 结果：模板有效，参与重载决议，匹配成功
```

**条件为 false 时：**

```cpp
std::enable_if_t<false, int> → 无法实例化（没有 type 成员）
// 结果：模板被"静默移除"，不参与重载决议
// 如果没有其他匹配的重载 → 报错"找不到匹配的函数"
```

### 编译失败示例

```cpp
int arr[4] = {1, 2, 3, 4};
int* ptr = arr;

UEArrayCountHelper(arr);  // ✅ T=int[4], __is_array=true, 编译成功
UEArrayCountHelper(ptr);  // ❌ T=int*, __is_array=false, 编译失败
```

错误信息：
```
error: no matching function for call to 'UEArrayCountHelper'
note: candidate template ignored: requirement '__is_array(int *)' was not satisfied
```

### SFINAE vs 硬编译错误

| 类型 | 行为 |
|------|------|
| **SFINAE**（模板参数推导失败） | 模板被静默排除，不报错，继续找其他重载；如果没有其他重载，才报"找不到函数" |
| **硬错误**（模板体内的错误） | 直接编译失败，不会尝试其他重载 |

**UE_ARRAY_COUNT 的设计意图**：故意只提供数组版本，指针调用时 SFINAE 移除唯一的候选，导致"找不到函数"的编译错误，从而阻止误用。

---

## 两个版本对比

| 特性 | MSVC 版本 | Clang 版本 |
|------|-----------|------------|
| 获取 N | 模板参数推导 `T(&)[N]` | `sizeof(t)/sizeof(t[0])` 计算 |
| 约束方式 | 模式匹配（指针无法匹配） | `UE_REQUIRES` 显式约束 |
| 语法风格 | 传统 C++ 模板 | C++20 concepts / SFINAE |
| 可读性 | 较难理解 | 相对直观 |

---

## 为什么不用 sizeof

| 方式 | 代码 | 安全性 |
|------|------|--------|
| 传统 sizeof | `sizeof(arr)/sizeof(arr[0])` | ❌ 指针会给出错误结果（通常得到 1 或 2） |
| UE_ARRAY_COUNT | `UE_ARRAY_COUNT(arr)` | ✅ 指针会编译报错 |

---

## 适用场景

- ✅ 遍历静态数组
- ✅ 初始化固定大小的容器
- ✅ 配置表/查找表的元素计数
- ✅ 编译期静态断言验证
- ❌ 不能用于 TArray（使用 `Num()` 代替）
- ❌ 不能用于指针

---

## 注意事项

- 只能用于 C 风格静态数组，不能用于 TArray（TArray 使用 `Num()`）
- 数组必须在当前作用域可见完整定义
- 返回值是 constexpr，可用于编译期计算
- 不能用于指针，传入指针会导致编译错误（这正是其安全之处）

---

## 使用示例

```cpp
// 示例数据
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

static const int32 DamageMultipliers[] = { 1, 2, 4, 8, 16 };

static const TCHAR* WeaponTypeNames[] = 
{
    TEXT("Sword"),
    TEXT("Bow"),
    TEXT("Staff"),
    TEXT("Dagger"),
};

// 用法1: 基本计数
constexpr uint32 StructCount = UE_ARRAY_COUNT(TestStruct);     // 4
constexpr uint32 IntCount = UE_ARRAY_COUNT(DamageMultipliers); // 5
constexpr uint32 StringCount = UE_ARRAY_COUNT(WeaponTypeNames); // 4

// 用法2: 安全遍历静态数组
for (uint32 i = 0; i < UE_ARRAY_COUNT(TestStruct); ++i)
{
    UE_LOG(LogTemp, Log, TEXT("TestStruct[%d]: %s"), i, TestStruct[i].NamePostFix);
}

// 用法3: 范围检查（防止数组越界）
int32 WeaponIndex = 2;
if (WeaponIndex >= 0 && WeaponIndex < static_cast<int32>(UE_ARRAY_COUNT(WeaponTypeNames)))
{
    UE_LOG(LogTemp, Log, TEXT("武器类型: %s"), WeaponTypeNames[WeaponIndex]);
}

// 用法4: 编译期静态断言验证
constexpr uint32 ExpectedCount = 4;
static_assert(UE_ARRAY_COUNT(TestStruct) == ExpectedCount, "TestStruct count mismatch!");

// 错误示例（取消注释会编译失败）
// const TCHAR** PointerToArray = WeaponTypeNames;
// uint32 WrongCount = UE_ARRAY_COUNT(PointerToArray);  // 编译错误！指针不是数组
