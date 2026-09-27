# shadPS4 Android 移植工程说明

本文档描述将 shadPS4（PS4 模拟器）移植到 Android 平台的工程化改造：构建体系、目录结构、
能力边界、CI 集成与后续路线图。目标是让贡献者能够一键构建 APK，并为引入 ARM64 CPU
执行后端打好工程基础。

移植基线：上游 `main` 分支（v0.18.x，本仓库快照），45 个 submodule 中的 43 个已
物化内联到仓库中（无 `.gitmodules`，克隆即完整可构建）。两个 **macOS 专用** 依赖
被裁剪以保持仓库精简：`externals/mesa-kosmickrisp`（Apple 专用 Mesa Vulkan 驱动，
约 415 MB）与 `externals/vulkan-loader`（约 9 MB）——二者仅在
`externals/CMakeLists.txt` 的 `if(APPLE)` 分支中被引用，Android / Linux / Windows
构建不涉及。如需在 macOS 上构建桌面版，请从上游
`shadps4-emu/shadPS4` 恢复这两个 submodule。

---

## 目录

1. [能力边界（必读）](#1-能力边界必读)
2. [总体架构](#2-总体架构)
3. [目录结构](#3-目录结构)
4. [本地构建](#4-本地构建)
5. [CI 构建（仅手动触发）](#5-ci-构建仅手动触发)
6. [ARM64 执行后端路线图](#6-arm64-执行后端路线图)
7. [与上游的改动清单](#7-与上游的改动清单)
8. [设备要求与运行行为](#8-设备要求与运行行为)
9. [已知问题与限制](#9-已知问题与限制)
10. [TODO 与风险跟踪](#10-todo-与风险跟踪)

---

## 1. 能力边界（必读）

上游 shadPS4 采用 **x86-64 直接执行架构**：加载器把 PS4 的 x86-64 代码段映射进
宿主进程地址空间后，`Module::Start` 直接跳转执行客户机指令，借助 Zydis/xbyak
（`src/core/cpu_patches.cpp`）做的只是**同架构指令修补**（Windows 红区保护、TLS
重定向等），并不存在跨架构翻译层。上游能在 Apple Silicon 上运行，靠的是 macOS
的 Rosetta 2 在**操作系统层面**翻译 x86-64 二进制——Android 没有等价机制。

因此本移植的能力边界如下：

| 能力 | 状态 | 说明 |
|------|------|------|
| 编译产出 APK | ✅ 可用 | arm64-v8a，Release/Debug |
| 安装启动 App | ✅ 可用 | SDL3 + Dear ImGui Big Picture UI |
| 手柄/触屏输入 | ✅ 可用 | SDL3 输入栈（含 Android 游戏手柄） |
| Vulkan 渲染 UI | ✅ 可用 | 依赖设备 Vulkan 驱动（Android 10+ 基本标配） |
| 音频输出 | ✅ 可用 | OpenAL-Soft OpenSL ES 后端 |
| 日志 | ✅ 可用 | spdlog → logcat（tag `shadPS4`），同时写文件 |
| 浏览/管理游戏库 | ✅ 可用 | Big Picture 库界面（列目录、读取 param.sfo 等） |
| **启动 PS4 游戏** | ❌ **暂不可用** | 需 x86-64 → ARM64 翻译后端（见第 6 节） |

> 启动游戏时会在 UI 上弹出明确提示并拒绝进入执行路径（见 `src/emulator.cpp` 中
> `ARCH_ARM64` 守卫），而不是跳进未知指令崩溃。

## 2. 总体架构

```
┌───────────────────────────────────────────────┐
│ APK (com.shadps4.emu)                         │
│  ┌─────────────────────────────────────────┐  │
│  │ Shadps4Activity (Java, 继承 SDLActivity)│  │
│  │  · Os.setenv("SHADPS4_DATA_DIR", …)     │  │
│  │  · 存储权限引导                          │  │
│  └───────────────┬─────────────────────────┘  │
│                  │ System.load                │
│  ┌───────────────▼─────────────────────────┐  │
│  │ libSDL3.so   (源码内联构建, 共享库)      │  │
│  │ libmain.so   (模拟器核心, 共享库)        │  │
│  │   · SDL_main 入口 → Big Picture 同进程   │  │
│  │   · path_util ← SHADPS4_DATA_DIR         │  │
│  │   · spdlog → logcat sink                 │  │
│  │   · ARM64 信号上下文 (ESR 解析)          │  │
│  │   · uuid ← /dev/urandom                  │  │
│  └─────────────────────────────────────────┘  │
└───────────────────────────────────────────────┘
```

关键决策：

- **前端选择**：Qt GUI 已被上游拆分到独立的 QTLauncher 仓库；仓内自带的前端是
  **SDL3 + Dear ImGui Big Picture**，天然适合 Android（SDLActivity 标准加载流程），
  本移植零改动复用，仅让无参启动时默认进入 Big Picture 同进程模式。
- **目标形态**：桌面平台仍是可执行文件 `shadps4`；Android 下同一套源码编成
  `libmain.so`（`SHADPS4_TARGET` CMake 变量控制），由 SDLActivity 按序加载
  `libSDL3.so → libmain.so`。
- **依赖内联**：45 个 submodule 全部物化进仓库，构建不依赖 GitHub 网络状态
  （仅 FFmpeg 源码包与宿主工具链除外，均带缓存）。
- **bionic 适配**：libuuid 不存在 → `/dev/urandom` 生成 v4 UUID；ESR 不在
  `mcontext_t` 里 → 解析信号帧保留区；ASLA/PulseAudio 不存在 → OpenSL ES。

## 3. 目录结构

```
├── android/                      # 新增：Android 工程
│   ├── build_native.sh           # 原生构建总驱动（五步，可分步跳过）
│   ├── cmake/CMakeLists.txt      # 宿主 protoc 包装构建（abseil + protobuf）
│   ├── settings.gradle.kts / build.gradle.kts / gradle.properties
│   ├── gradle/wrapper/           # Gradle 8.9 wrapper（含 jar）
│   ├── gradlew / gradlew.bat
│   └── app/
│       ├── build.gradle.kts      # AGP 8.7.3, minSdk 29, arm64-v8a, Secrets 签名
│       ├── proguard-rules.pro
│       └── src/main/
│           ├── AndroidManifest.xml
│           ├── java/com/shadps4/emu/Shadps4Activity.java
│           └── res/…             # 主题/字符串/自适应图标（纯 framework，无 AndroidX）
├── scripts/push-to-github.sh     # 新增：一键建仓推送（见第 5 节）
├── .github/workflows/android-build.yml   # 新增：仅 workflow_dispatch
├── .github/workflows/build.yml           # 已改为仅手动触发
├── CMakeLists.txt                # 目标变量化 + Android 守卫（见第 7 节）
├── externals/CMakeLists.txt      # SDL3 共享化 / OpenSL / Tracy / protobuf 交叉
├── externals/ffmpeg-core/CMakeLists.txt  # FFMPEG_ANDROID_PREFIX 接入
└── ANDROID_PORT.md               # 本文档
```

## 4. 本地构建

前置条件：

- Android SDK（`platform-tools`、`platforms;android-35`、`build-tools;35.0.0`）
- NDK `27.0.12077973`（其它 27.x 会自动回退使用并告警）
- JDK 17、CMake ≥ 3.24、Ninja、curl、tar（支持 xz）
- 约 12 GB 磁盘（构建树 + FFmpeg + 缓存）

一键构建：

```sh
# 1. 原生部分：宿主 protoc → FFmpeg 7.1.1 交叉编译 → 模拟器 CMake → jniLibs
bash android/build_native.sh Release

# 2. APK
cd android && ./gradlew assembleRelease
```

SDK/NDK 探测顺序：`SHADPS4_ANDROID_SDK` → `ANDROID_HOME` → `ANDROID_SDK_ROOT`
→ `~/Android/Sdk` → GitHub Actions 路径。可用环境变量覆盖（详见脚本头部注释）：

| 变量 | 默认 | 说明 |
|------|------|------|
| `SHADPS4_NDK_VERSION` | `27.0.12077973` | 优先使用的 NDK 版本 |
| `SHADPS4_ABI` | `arm64-v8a` | 目前仅支持 arm64 |
| `SHADPS4_PLATFORM` | `android-29` | minSdk 对应的平台级 |
| `FFMPEG_VERSION` | `7.1.1` | FFmpeg 源码版本 |
| `FFMPEG_SRC_DIR` | 自动下载 | 指向已解压的 FFmpeg 源码目录 |
| `SHADPS4_SKIP_HOSTPROTOC` | `0` | 跳过宿主 protoc（需已存在） |
| `SHADPS4_SKIP_FFMPEG` | `0` | 跳过 FFmpeg（需已安装到 prefix） |
| `SHADPS4_SKIP_CMAKE` | `0` | 跳过模拟器 CMake 构建 |
| `SHADPS4_SKIP_JNILIBS` | `0` | 跳过 jniLibs 暂存 |
| `SHADPS4_JOBS` | CPU 数 | 并行编译任务数 |

FFmpeg 裁剪策略：优先"全内部编解码器"配置（等价上游 vcpkg 特性集，四类必需解码器
H264/HEVC/AAC/MP3 天然在内）；若 configure 失败自动降级为最小集（显式启用上述
四个解码器 + mov/mpegts/mp3/aac 解复用 + file/pipe 协议）。两种配置均不引入任何
外部依赖（`--disable-autodetect`），保证产物 hermetic。

## 5. CI 构建（仅手动触发）

`.github/workflows/android-build.yml`：

- **触发方式：仅 `workflow_dispatch`**（Actions 页面手动 Run workflow），可选择
  Release / Debug。
- 首次构建约 40–80 分钟（宿主 protoc ~5 min、FFmpeg ~10 min、主体 ~30-60 min）；
  `ffmpeg-install` 与 `host-protoc` 有 Actions 缓存，重跑约 30–50 分钟。
- **签名**：设置以下 Repository Secrets 后自动签出正式 Release 包，否则产出
  `app-release-unsigned.apk`（可手动 `apksigner` 签名）：

| Secret | 内容 |
|--------|------|
| `ANDROID_KEYSTORE_BASE64` | keystore 文件的 base64（`base64 -w0 release.keystore`） |
| `ANDROID_KEYSTORE_PASSWORD` | keystore 口令 |
| `ANDROID_KEY_ALIAS` | key 别名 |
| `ANDROID_KEY_PASSWORD` | key 口令 |

- 产物命名 `shadPS4-android-<type>-<sha>.apk`，附 SHA256 校验文件，保留 30 天。

另：上游原有 `build.yml`（桌面平台 CI）的 `push` / `pull_request` 自动触发已
**移除**，同样只保留手动触发，避免 fork 后每次推送烧配额。需要桌面 CI 时在
Actions 页手动运行即可。

**建仓推送**：使用 `scripts/push-to-github.sh <GitHub用户名> [仓库名]`，脚本会
初始化 git、全量提交并推送到 `git@github.com:<用户名>/<仓库>.git`（需先配置
SSH key 或改脚本用 HTTPS + PAT）。首次推送后到 Actions 页手动运行 Android Build。

## 6. ARM64 执行后端路线图

在模拟器里跑 PS4 游戏，必须把 x86-64 客户机指令翻译成 ARM64。参考主流方案
（Fex/Rosetta 风格），建议分三步走，每步都是可独立交付的里程碑：

### 阶段一：解释器（正确性优先）

- 新增 `src/core/cpu_interpreter/`：x86-64 → ARM64 解释执行器，按基本块解码
  （可复用仓内 Zydis 做解码器——它是纯 C 库，ARM64 主机上可正常编译运行）。
- `Module::Start` 在 `ARCH_ARM64` 下改为进入解释循环，而不是直接跳转。
- 目标：能跑起游戏内最初的初始化代码路径（配合 linker 的符号解析），性能不敏感。
- 验收：某个 homebrew demo（如 ELF 形式的测试程序）在 ARM64 上完成初始化打印。

### 阶段二：基本块 JIT（可用性门槛）

- 引入 ARM64 汇编器生成器（候选：`dynarmic`（需适配）、`vixl`、或 LLVM AArch64
  后端），把 Zydis 解码出的 x86-64 基本块翻成 ARM64 代码块放入 RWX 区域。
- 关键配套：GPR/XMM 映射约定、标志位惰性求值、guest 内存访问走显式基地址
  （为 SIGSEGV 兜底处理保留路径——`signal_context.cpp` 的 ARM64 分支已就位）。
- 目标：2D/轻量 3D 游戏可进入主菜单。

### 阶段三：优化（性能）

- 基本块链接、内联 guest 内存访问 + 快速路径、TLB 风格地址缓存、SLEEF 换
  NEON 数学库、多线程编译后台块。
- 长期：AOT 缓存落盘（配合本仓库已有的 `ShaderDir` 缓存目录结构）。

> 估算量级：阶段一约 2–4 人月；阶段二再 4–8 人月（x86-64 语义面大：SSE/AVX、
> 标志位、分段、TLS、原子指令）。建议先在 x86-64 宿主上以"解释模式跑同架构"
        验证框架，再平移到 ARM64。

## 7. 与上游的改动清单

保持"最小侵入、全部可评审"原则，改动分四类：

**CMake（3 个文件）**

- `CMakeLists.txt`：
  - `SHADPS4_TARGET` 变量化（Android=`main` 共享库，其余=`shadps4` 可执行文件），
    目标定义与全部 `target_*` 引用随之参数化；
  - `PROTOC_HOST_EXECUTABLE`：交叉编译时用宿主 protoc 生成 shadnet 源码；
  - 非 x86-64 架构从 `CORE` 源列表剔除 `src/core/cpu_patches.cpp`（xbyak 仅
    支持 x86，上游靠 Rosetta 才在 ARM Mac 上编译过它）；调用点上游本就有
    `ARCH_X86_64` 守卫，无需改动；
  - Android 链接块：`log`（logcat）+ `android` + 16 KB 页对齐
    （`-Wl,-z,max-page-size=16384`）。
- `externals/CMakeLists.txt`：SDL3 共享化（`SDL_SHARED ON`）；OpenAL-Soft 增加
  Android 分支（OpenSL ES，关 ALSA/Pulse）；Tracy 在 Android 上强制关闭；
  protobuf 交叉编译时 `BUILD_PROTOC_BINARIES OFF`（protoc 由宿主侧提供）。
- `externals/ffmpeg-core/CMakeLists.txt`：新增 Android 分支，从
  `FFMPEG_ANDROID_PREFIX`（build_native.sh 交叉编译产物）链接静态库并取头文件。

**源码（6 个文件，均有 `__ANDROID__` / `ARCH_ARM64` 守卫，不影响桌面平台）**

| 文件 | 改动 |
|------|------|
| `src/main.cpp` | `SDL_main` 入口；无参启动默认 Big Picture 同进程（Android 无控制台） |
| `src/common/path_util.cpp` | `SHADPS4_DATA_DIR` 环境变量作为用户数据根目录（注意在 `__linux__` 之前判断） |
| `src/common/logging/log.cpp` | 控制台 sink 换 spdlog `android_sink_mt`（tag `shadPS4`） |
| `src/common/signal_context.cpp` | ARM64：`pc` 取 RIP；ESR 从信号帧保留区解析（bionic/glibc 均无 `esr` 字段）；WnR 位判写错误、EC 0x20/0x21 判执行错误 |
| `src/emulator.cpp` | `ARCH_ARM64` 下 `Run()` 优雅拒绝启动游戏（弹窗 + 日志），避免跳入 x86-64 代码段崩溃 |
| `src/core/libraries/kernel/kernel.cpp` | `sceKernelUuidCreate` 在 bionic 下用 `/dev/urandom` 生成 v4 UUID（无 libuuid） |

**CI（2 个文件）**

- 新增 `.github/workflows/android-build.yml`（仅 `workflow_dispatch`）；
- `.github/workflows/build.yml` 移除 `push`/`pull_request` 触发器。

**其它**

- `README.md` 增加 Android 小节；新增本文件、`android/` 工程、
  `scripts/push-to-github.sh`、`.gitignore` 追加条目。

## 8. 设备要求与运行行为

- **最低**：Android 10（API 29）、arm64-v8a、Vulkan 1.1。
- **推荐**：Android 13+，骁龙 8 系 / 天玑旗舰（未来 JIT 阶段的性能余量），16 KB
  页设备（Pixel 2024+）已做链接对齐兼容。
- 首次启动会引导一次"所有文件访问"权限（用于扫描共享存储上的游戏库；游戏放在
  应用专属目录则不需要）。之后直接进入 Big Picture UI。
- 数据目录：`/storage/emulated/0/Android/data/com.shadps4.emu/files/`
  （`user/`、`log/`、`shader/` 等子目录由 path_util 创建）。
- 日志：`adb logcat -s shadPS4` 实时查看；文件日志在 `<data>/user/log/`。

## 9. 已知问题与限制

1. **游戏不可运行**（核心限制，见第 1、6 节）。
2. `hwinfo`（CPU/GPU 探测）与 `libusb` 的 Android 编译路径未在真机矩阵上全量
   验证，若 CI 报错优先检查这两个 externals（处理见第 10 节）。
3. `LibAtrac9` 在上游即以纯 C 源编译，理论可用，但 ATRAC9 码流尚未在 Android
   上回归测试。
4. Big Picture 的部分设置项（如更新器）在移动端语义不适配，属 UI 层小问题。
5. Vulkan 驱动碎片化：个别设备的 Vulkan 1.1 实现缺扩展时，UI 可能回退异常，
   需按机型收集 `vulkaninfo` 排查。
6. 16 KB 页对齐已通过链接选项处理（`-Wl,-z,max-page-size=16384`），但未在
   16 KB 页真机上验证（截至文档编写）。

## 10. TODO 与风险跟踪

优先级从高到低：

- [ ] **P0**：CI 全量跑通（首次真实构建会暴露本轮未预见的编译错误，按日志逐个
      修：重点关注 hwinfo 的 `/proc` 访问宏、libusb 的 log 依赖、ffmpeg-core
      头文件版本匹配）。
- [ ] **P0**：真机安装冒烟测试：启动 → Big Picture UI 可导航 → 日志落盘。
- [ ] **P1**：解释器骨架（第 6 节阶段一）：`cpu_interpreter` 目录 + 基本块
      解码循环 + `Module::Start` ARM64 分支接入。
- [ ] `P1`：把 `SHADPS4_DATA_DIR` 同步进 Big Picture 的"打开数据目录"类 UI
      提示文案。
- [ ] **P2**：jniLibs 产物在 Gradle 侧的 v7a 过滤与 ` splits` 配置审查。
- [ ] **P2**：ATrac9 / AJM 音频链路真机回归。
- [ ] **P3**：16 KB 页真机验证（Pixel 8/9 开发者预览）。
- [ ] **P3**：Android TV / 手柄Only 模式（leanback launcher intent）。
