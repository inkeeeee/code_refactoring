```bash
sudo apt update

sudo apt install -y \
  llvm-19-dev \
  clang-19 \
  clang-tools-19 \
  libclang-19-dev \
  libclang-cpp19-dev \
  cmake \
  g++ \
  make \
  perf


## Сборка

```bash
rm -rf build

cmake -S . -B build \
  -DCT_Clang_INSTALL_DIR=/usr/lib/llvm-19

cmake --build build
```

## Проверка основного функционала

```bash
./check_refactor.sh
```

## Запуск unit-тестов

```bash
./build/refactor_tool_tests
```

## Ручной запуск утилиты

```bash
cp tests/tests_data/for_refactor.cpp /tmp/for_refactor.cpp

./build/src/refactor_tool /tmp/for_refactor.cpp -- -std=c++17

cat /tmp/for_refactor.cpp
```

## Просмотр лога изменений

```bash
cat refactor_tool_changes.log
```

## ASAN-сборка проекта

```bash
rm -rf build-asan

cmake -S . -B build-asan \
  -DCT_Clang_INSTALL_DIR=/usr/lib/llvm-19 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"

cmake --build build-asan
```

Запуск ASAN unit-тестов:

```bash
./build-asan/refactor_tool_tests
```

Если тестовый бинарь лежит в другом месте:

```bash
find build-asan -type f -executable -name "*test*"
```

## ASAN-проверка leak_example.cpp

### До рефакторинга

```bash
mkdir -p analysis_artifacts

cp tests/tests_data/leak_example.cpp analysis_artifacts/leak_before.cpp

clang++-19 -std=c++17 -O0 -g \
  -fsanitize=address \
  -fno-omit-frame-pointer \
  analysis_artifacts/leak_before.cpp \
  -o analysis_artifacts/leak_before

ASAN_OPTIONS=detect_leaks=1:new_delete_type_mismatch=0 \
  ./analysis_artifacts/leak_before
```

### После рефакторинга

```bash
cp tests/tests_data/leak_example.cpp analysis_artifacts/leak_after.cpp

./build/src/refactor_tool analysis_artifacts/leak_after.cpp -- -std=c++17

clang++-19 -std=c++17 -O0 -g \
  -fsanitize=address \
  -fno-omit-frame-pointer \
  analysis_artifacts/leak_after.cpp \
  -o analysis_artifacts/leak_after

ASAN_OPTIONS=detect_leaks=1 ./analysis_artifacts/leak_after
```

## perf-проверка perf_example.cpp

### До рефакторинга

```bash
mkdir -p analysis_artifacts

cp tests/tests_data/perf_example.cpp analysis_artifacts/perf_before.cpp

clang++-19 -std=c++17 -O2 -g \
  -fno-omit-frame-pointer \
  analysis_artifacts/perf_before.cpp \
  -o analysis_artifacts/perf_before

perf stat ./analysis_artifacts/perf_before
```

### После рефакторинга

```bash
cp tests/tests_data/perf_example.cpp analysis_artifacts/perf_after.cpp

./build/src/refactor_tool analysis_artifacts/perf_after.cpp -- -std=c++17

clang++-19 -std=c++17 -O2 -g \
  -fno-omit-frame-pointer \
  analysis_artifacts/perf_after.cpp \
  -o analysis_artifacts/perf_after

perf stat ./analysis_artifacts/perf_after
```

