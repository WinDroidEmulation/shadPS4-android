# shadPS4-android Actions 构建修复（完整版）

## 修复包含两次提交的所有修改

本压缩包只包含 **5 个被修改的源文件** + 1 个 .patch + 1 个 README，目录结构与原仓库保持一致，可以直接覆盖到你的本地仓库根目录（或在已 clone 的仓库上 `git apply`）。

## 修改文件清单

```
shadPS4-android-fix/
├── .github/
│   └── workflows/
│       └── android-build.yml          # GitHub Actions 升级到 v5，host-protoc 缓存 key 提升 v1→v2
├── src/
│   ├── imgui/
│   │   └── renderer/
│   │       └── CMakeLists.txt         # 交叉编译时使用 HOST_FONT_EMBED_EXECUTABLE
│   └── video_core/
│       └── host_shaders/
│           └── CMakeLists.txt          # 交叉编译时使用 HOST_GLSLANG_EXECUTABLE
├── externals/
│   ├── discord-rpc/
│   │   └── src/
│   │       └── CMakeLists.txt         # 修复 -lpthread 找不到（关键修复，本轮新加）
│   └── zstd/
│       └── build/
│           └── cmake/
│               └── CMakeModules/
│                   └── ZstdDependencies.cmake  # Android 下不传 -lpthread（防御性修复）
├── shadPS4-android-fix.patch          # 一次性应用所有修复的 git patch
└── README.md                          # 本说明文件
```

## 问题与修复对照

### 第一次：exit code 126（Run #6，commit 7368e129 之后）
**症状**：`Process completed with exit code 126`，在 "Build native libraries" 步骤。
**根因**：交叉编译时，主 CMake 工程里的 `add_custom_command` 在 x86_64 宿主机上尝试运行 ARM64 二进制：
- `src/imgui/renderer/CMakeLists.txt` 的 `Dear_ImGui_FontEmbed` target 被编译为 ARM64，然后被宿主机执行 → ENOEXEC → 126。
- `src/video_core/host_shaders/CMakeLists.txt` 在交叉编译时既找不到 `glslang-standalone` target（glslang 子项目里的 `if (IOS OR ANDROID) set(ENABLE_GLSLANG_BINARIES OFF)` 关掉了），也没有消费 `7368e129` 新增的 `HOST_FONT_EMBED_EXECUTABLE` / `HOST_GLSLANG_EXECUTABLE`。

**修复**：
- `src/imgui/renderer/CMakeLists.txt`：当 `CMAKE_CROSSCOMPILING AND HOST_FONT_EMBED_EXECUTABLE` 时，直接用宿主机可执行文件，跳过构建 ARM64 版 Dear_ImGui_FontEmbed。
- `src/video_core/host_shaders/CMakeLists.txt`：当 `CMAKE_CROSSCOMPILING AND HOST_GLSLANG_EXECUTABLE` 时，优先用宿主机路径。
- `.github/workflows/android-build.yml`：actions 全部升级到 v5，消除 Node.js 20 deprecation 警告；host-protoc 缓存 key `v1` → `v2`，丢弃在加入新宿主工具前生成的陈旧缓存。

### 第二次：ld.lld: error: unable to find library -lpthread（Run #7，commit ca2728d9 之后）
**症状**：上述问题修复后构建能继续 25 分钟，但在最终链接阶段报：
```
ld.lld: error: unable to find library -lpthread
clang++: error: linker command failed with exit code 1 (use -v to see invocation)
ninja: build stopped: subcommand failed
```
**根因**：Android 自 API 21 起 pthread 已合并到 libc，NDK sysroot 不再提供 `libpthread.a`。任何显式 `-lpthread` 都会让链接器找不到。仓库里有 2 处直接产生 `-lpthread`：

1. **`externals/discord-rpc/src/CMakeLists.txt:74`**
   `target_link_libraries(discord-rpc PUBLIC pthread)` —— CMake 把 `pthread` 当库名翻译成 `-lpthread`，discord-rpc 是 PUBLIC 链接，所以最终 `libmain.so` 也会带 `-lpthread`，链接失败。

2. **`externals/zstd/build/cmake/CMakeModules/ZstdDependencies.cmake`**
   把 `CMAKE_THREAD_LIBS_INIT` 直接拷给 `THREADS_LIBS`。我们在 `externals/CMakeLists.txt` 里已经把 `ZSTD_MULTITHREAD_SUPPORT` 关掉了，所以理论上不会触发，但作为防御性修复一并修掉。

**修复**：
- `externals/discord-rpc/src/CMakeLists.txt`：把 `target_link_libraries(discord-rpc PUBLIC pthread)` 改成 `find_package(Threads REQUIRED) + target_link_libraries(discord-rpc PUBLIC Threads::Threads)`。CMake 的 `FindThreads` 在 Android 上会检测到 pthread-in-libc，`Threads::Threads` 的 `INTERFACE_LINK_LIBRARIES` 为空，因此不会产生 `-lpthread`；Linux/macOS 上仍保持原有 pthread 链接。
- `externals/zstd/build/cmake/CMakeModules/ZstdDependencies.cmake`：在 Android 下显式把 `THREADS_LIBS` 清空。

### 其他清理
- 删除 `fix.patch`（`ca2728d9` 提交误带入的 1.3 MB 的陈旧 diff，对构建无影响但是冗余）。

## 验证

### 第一步修复验证
在本地用 GNU 编译器跑了 `android/cmake` 子项目的宿主工具构建，三个 target 都成功产出：
- `protoc`
- `Dear_ImGui_FontEmbed`
- `glslang-standalone`（输出路径 `<build>/host-tools/glslang`，`<build>/host-tools/glslangValidator` 为符号链接）

### 第二步修复验证
1. CMake `FindThreads.cmake` 源码确认：在 Android 交叉编译环境下，`check_c_source_compiles("${PTHREAD_C_CXX_TEST_SOURCE}" CMAKE_HAVE_LIBC_PTHREAD)` 会通过（libc 里就有 pthread_create 等），因此 `CMAKE_THREAD_LIBS_INIT=""`，`Threads::Threads` 是空 `INTERFACE_LINK_LIBRARIES`。
2. 代码库全文搜索 `target_link_libraries.*pthread\b` / `-lpthread` 确认：除本修复的两处外，其他出现都在 SDL3 的 `CheckPTHREAD` 宏（已经有 `if(ANDROID OR SDL_PTHREADS_PRIVATE)` 守护）或各种 example/test 子目录里（不会被主构建 include）。

## 如何使用

### 方式一：直接覆盖到你的本地仓库
```
unzip shadPS4-android-fix.zip
# 把里面的 .github/、src/、externals/ 三棵子树复制到你已 clone 的 shadPS4-android 仓库根目录覆盖
cd /path/to/your/shadPS4-android
cp -r /path/to/unpacked/shadPS4-android-fix/.github .
cp -r /path/to/unpacked/shadPS4-android-fix/src .
cp -r /path/to/unpacked/shadPS4-android-fix/externals .
git add -A
git commit -m "Apply Android build fix (exit code 126 + -lpthread not found)"
git push
```

### 方式二：在已 clone 的仓库上 git apply
```
cd /path/to/your/shadPS4-android
git apply /path/to/shadPS4-android-fix.patch
git commit -am "Fix Android build: host tools in cross-compile + drop -lpthread"
git push
```

## 后续建议

1. 推到 GitHub 后在 Actions 页面手动触发一次 "Android Build" workflow（Release）验证。
2. 如果还有其他类型的报错，把 Actions 失败页面的 step 标题 + 关键错误信息发给我，继续迭代。
3. 把所有 `pthread` 类的 `target_link_libraries(... pthread)` 都换成 `Threads::Threads` 是个长期方向，可以分批改；本次只修了主构建必然用到的 `discord-rpc`。
