# registryListen

Windows 注册表枚举工具，递归遍历注册表五大根键，将结果以 AES-256-GCM 加密后存储到本地文件，支持与上次枚举结果对比差异。

## 功能

- 递归枚举 `HKCR`、`HKCU`、`HKLM`、`HKU`、`HKCC` 五大根键（最大深度 10 层）
- 支持解析 `REG_SZ`、`REG_EXPAND_SZ`、`REG_DWORD`、`REG_QWORD`、`REG_BINARY`、`REG_MULTI_SZ` 等值类型
- 自动检测并请求管理员权限（部分注册表路径需要管理员权限才能读取）
- 枚举结果使用 **AES-256-GCM** 加密后写入文件，密钥自动保存到 `.key` 文件
- 与上次枚举结果对比，输出新增/删除/修改的注册表键和值

## 项目结构

```
registryListen/
├── CMakeLists.txt              # CMake 构建配置
├── main.cpp                    # 入口：依赖组装与流程编排
├── output_writer.h             # 输出接口（策略模式 + 组合模式）
├── privilege_manager.h         # 权限管理模块
├── registry_enumerator.h       # 注册表枚举器（头文件）
├── registry_enumerator.cpp     # 注册表枚举器（实现）
├── encrypted_file_saver.h      # 加密文件保存器（头文件）
├── encrypted_file_saver.cpp    # 加密文件保存器（实现）
├── encrypted_file_reader.h     # 加密文件读取器（头文件）
├── encrypted_file_reader.cpp   # 加密文件读取器（实现）
├── registry_diff.h             # 差异对比模块（头文件）
└── registry_diff.cpp           # 差异对比模块（实现）
```

## 架构设计

项目采用**面向接口 + 依赖注入**的模块化架构，各模块职责单一、可独立测试和替换。

| 模块 | 文件 | 设计模式 | 职责 |
|------|------|---------|------|
| **IOutputWriter** | `output_writer.h` | 策略模式 + 组合模式 | 抽象输出目标。`ConsoleWriter`（控制台 ANSI/GBK）、`BufferWriter`（内存 UTF-8 缓冲）、`CompositeWriter`（多目标转发） |
| **PrivilegeManager** | `privilege_manager.h` | — | 管理员权限检测 + UAC 提权重启 |
| **RegistryEnumerator** | `registry_enumerator.h/.cpp` | 依赖注入 | 递归枚举注册表，通过 `IOutputWriter&` 输出结果 |
| **EncryptedFileSaver** | `encrypted_file_saver.h/.cpp` | 依赖注入 | 接收字节数据，AES-256-GCM 加密写入 `.enc` 文件，密钥保存到 `.key` 文件 |
| **EncryptedFileReader** | `encrypted_file_reader.h/.cpp` | 依赖注入 | 从 `.key` 文件读取密钥，解密 `.enc` 文件返回明文数据 |
| **RegistryDiff** | `registry_diff.h/.cpp` | 依赖注入 | 解析两次枚举结果，对比输出新增/删除/修改的注册表项 |
| **main** | `main.cpp` | 组装器 | 创建对象、注入依赖、编排主流程（7 步，~70 行） |

### 设计原则

- **依赖倒置**：高层模块（枚举器、加密器、对比器）依赖 `IOutputWriter` 接口，不依赖具体实现
- **单一职责**：每个类只做一件事
- **开闭原则**：新增输出目标（如 JSON 文件、网络发送）只需新增 `IOutputWriter` 实现，无需修改枚举逻辑
- **消除全局变量**：所有状态由对象成员管理，生命周期由 `main` 显式控制

### 可测试性

- Mock `IOutputWriter` 即可验证枚举逻辑、对比逻辑，无需实际访问注册表
- 各模块可独立进行单元测试

## 构建

### 环境要求

- Windows 操作系统
- CMake >= 4.2
- MinGW (g++) 或 MSVC，支持 C++20

### 编译

```bash
mkdir cmake-build-debug
cd cmake-build-debug
cmake .. -G "MinGW Makefiles"
cmake --build .
```

或使用 CLion 直接打开项目构建。

## 使用

直接运行编译出的 `registryListen.exe`：

1. 程序自动检测管理员权限，若非管理员则通过 UAC 提权
2. 开始枚举注册表，控制台实时输出
3. 枚举完成后，若存在上次的加密文件，自动解密并与本次结果对比差异
4. 使用随机生成的密钥对本次结果进行 AES-256-GCM 加密
5. 加密文件保存为 `registry_output.enc`，密钥保存为 `registry_output.key`（与 exe 同目录）
6. 控制台输出密钥和 nonce 的十六进制值

### 输出示例

```
========== AES-256-GCM 加密信息 ==========
密钥 (hex): A1B2C3D4E5F60718293A4B5C6D7E8F901122334455667788990AABBCCDDEEFF
Nonce (hex): 00112233445566778899AABB
==========================================
请妥善保管以上密钥信息，解密时需要用到。
```

### 差异对比示例

```
========== 与上次枚举结果对比 ==========

---------- 新增的注册表键 ----------
[+] HKEY_LOCAL_MACHINE\SOFTWARE\NewApp  (3 个值)
    InstallPath  [REG_SZ] = "C:\Program Files\NewApp"
    Version  [REG_DWORD] = 1
    ...

---------- 修改的注册表键 ----------
[~] HKEY_CURRENT_USER\Software\ExistingApp
  + NewSetting  [REG_DWORD] = 42
  - OldSetting  [REG_SZ] = "deprecated"

========== 差异汇总 ==========
新增键: 1 个 (共 3 个值)
删除键: 0 个 (共 0 个值)
修改键: 1 个
==============================
```

## 加密文件格式

### .enc 文件

```
[nonce: 12 字节][GCM 认证标签: 16 字节][AES-256-GCM 密文]
```

### .key 文件

```
<密钥 hex 64 字符>
<nonce hex 24 字符>
```

## 技术栈

- C++20
- Win32 API（注册表操作、文件 I/O）
- BCrypt API（AES-256-GCM 加解密）
- CMake 构建系统
