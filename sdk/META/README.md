# sdk/META —— 本目录不进仓库

这里原本放 MTK 的 META SDK：

- `include/` —— `METACalibrationLibrary.h` 等公开头文件（编译期需要）
- `bin/` —— `METACalibrationLibrary.dll`（145 MB）、`MetaCore.dll`、`MetaApp.dll` 等运行库

这两部分是 MTK 的机密与专有内容，**不在本仓库中分发**。要自己构建/运行，需要从 MTK 获取
META SDK（或 `META_DLL_Customer_*` 发行包）后放到：

```
sdk/META/include/    <- 头文件（放在上层目录，保持 METACalibrationLibrary/Interface/... 的结构）
sdk/META/bin/        <- 运行库 DLL（6 个，约 181 MB）
```

放好之后：

```bat
python tools\gen_tables.py --src-dir "<META_DLL 源码树>"   :: 生成 src/gen/*.inc
build.bat D:\Qt5\5.14.2\msvc2017 clean
build\bin\METAQuickCheck.exe --selftest
```

没有 SDK 时，`CMakeLists.txt` 会以明确的错误信息停下，不会产生半个可执行文件。
