```bash
rm -rf build build-asan analysis_artifacts reports

cmake -S . -B build \
  -DCT_Clang_INSTALL_DIR=/usr/lib/llvm-19

cmake --build build -j

./check_refactor.sh

./build/refactor_tool_tests

cmake -S . -B build-asan \
  -DCT_Clang_INSTALL_DIR=/usr/lib/llvm-19 \
  -DENABLE_ASAN=ON \
  -DCMAKE_BUILD_TYPE=Debug 

cmake --build build-asan -j

./build-asan/refactor_tool_tests
./scripts/run_analysis.sh
```

## Отчеты:
```text
reports/asan_before.txt
reports/asan_after.txt
reports/perf_before.txt
reports/perf_after.txt
refactor_tool_changes.log
```
