# Boost Libraries – Trimmed Down for shadPS4

This is a subset of Boost v1.82.0 generated using the [bcp](https://www.boost.org/doc/libs/1_82_0/tools/bcp/doc/html/index.html) tool.  
To see which Boost modules are included, check the [`boost_libs.txt`](./boost_libs.txt) file.  
This repository is intended for use with the shadPS4 emulator.

---

## Updating This Repo

To update the Boost version or add a new library, follow these steps:

You will have to use PowerShell if you are on Windows.

1. **Download Boost and extract the package.**  
   You can clone the Boost repo and `cd` into it:
   ```sh
   git clone https://github.com/boostorg/boost
   cd boost
   ```

2. **Checkout a specific Boost version and initialize submodules.**  
   For Boost 1.82.0:
   ```sh
   git checkout boost-1.82.0
   git submodule update --init --recursive
   ```

3. **Build the `bcp` tool:**  
   - **Windows:**
     ```powershell
     .\ootstrap.bat
     .\b2 toolscp
     ```
   - **Linux/macOS:**
     ```sh
     ./bootstrap.sh
     ./b2 tools/bcp
     ```

4. **Store the Boost directory in a variable:**  
   - **Windows:**
     ```powershell
     $boost_dir = $pwd
     ```
   - **Linux/macOS:**
     ```sh
     export boost_dir=$(pwd)
     ```

5. **Add `bcp` to your PATH:**  
   (The correct output path is printed by `b2` during the build. The following are just examples—do not include the `bcp` executable name, only the directory containing it.)
   - **Windows:**
     ```powershell
     $env:Path += ";$boost_dir\bin.v2\tools\bcp\msvc-14.3\release\link-static\threading-multi"
     ```
   - **Linux/macOS:**
     ```sh
     export PATH="$boost_dir/bin.v2/tools/bcp/gcc-15/release/link-static:$PATH"
     ```

6. **Change to this repository’s directory** (typically `shadPS4/ext-boost`).  
   Make sure you stay in the same terminal session so you don’t lose your environment variables.

7. **Remove the existing Boost `boost` and `libs` directories.**  
   *(Only necessary when upgrading Boost, in case the new version removes any files.)*

8. **If updating Boost, update the Boost version in this README to keep things consistent.**

9. **Edit `boost_libs.txt`** to add or remove libraries/headers as needed.

10. **Run the build script to create the new trimmed-down Boost distribution:**
    - **Windows:**
      ```bat
      .\build.cmd $boost_dir
      ```
    - **Linux/macOS:**
      ```sh
      ./build.sh $boost_dir
      ```

11. **You may need to update `CMakeLists.txt`** to adjust include directories.

12. **Add/remove all changed files in git and commit your changes.**

---
