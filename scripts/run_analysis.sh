#!/usr/bin/env bash

set -u

LLVM_VERSION="${LLVM_VERSION:-19}"
CXX="clang++-${LLVM_VERSION}"
TOOL="./build/refactor_tool"

REPORT_DIR="reports"
ARTIFACTS_DIR="analysis_artifacts"

mkdir -p "${REPORT_DIR}"
mkdir -p "${ARTIFACTS_DIR}"

if [[ ! -x "${TOOL}" ]]; then
  echo "refactor_tool not found: ${TOOL}"
  echo "Run project build first:"
  echo "cmake -S . -B build -DCT_Clang_INSTALL_DIR=/usr/lib/llvm-${LLVM_VERSION}"
  echo "cmake --build build"
  exit 1
fi

echo "== ASAN before refactoring =="

cp tests/tests_data/leak_example.cpp "${ARTIFACTS_DIR}/leak_before.cpp"

"${CXX}" -std=c++17 -O0 -g \
  -fsanitize=address \
  -fno-omit-frame-pointer \
  "${ARTIFACTS_DIR}/leak_before.cpp" \
  -o "${ARTIFACTS_DIR}/leak_before"

ASAN_OPTIONS=detect_leaks=1:new_delete_type_mismatch=0 \
  "${ARTIFACTS_DIR}/leak_before" \
  > "${REPORT_DIR}/asan_before.txt" 2>&1 || true

echo "ASAN before report: ${REPORT_DIR}/asan_before.txt"

echo "== ASAN after refactoring =="

cp tests/tests_data/leak_example.cpp "${ARTIFACTS_DIR}/leak_after.cpp"

"${TOOL}" "${ARTIFACTS_DIR}/leak_after.cpp" -- -std=c++17 \
  > "${REPORT_DIR}/refactor_leak.log" 2>&1

"${CXX}" -std=c++17 -O0 -g \
  -fsanitize=address \
  -fno-omit-frame-pointer \
  "${ARTIFACTS_DIR}/leak_after.cpp" \
  -o "${ARTIFACTS_DIR}/leak_after"

ASAN_OPTIONS=detect_leaks=1 \
  "${ARTIFACTS_DIR}/leak_after" \
  > "${REPORT_DIR}/asan_after.txt" 2>&1

echo "ASAN after report: ${REPORT_DIR}/asan_after.txt"

echo "== perf before refactoring =="

cp tests/tests_data/perf_example.cpp "${ARTIFACTS_DIR}/perf_before.cpp"

"${CXX}" -std=c++17 -O2 -g \
  -fno-omit-frame-pointer \
  "${ARTIFACTS_DIR}/perf_before.cpp" \
  -o "${ARTIFACTS_DIR}/perf_before"

perf stat \
  -o "${REPORT_DIR}/perf_before.txt" \
  "${ARTIFACTS_DIR}/perf_before"

echo "perf before report: ${REPORT_DIR}/perf_before.txt"

echo "== perf after refactoring =="

cp tests/tests_data/perf_example.cpp "${ARTIFACTS_DIR}/perf_after.cpp"

"${TOOL}" "${ARTIFACTS_DIR}/perf_after.cpp" -- -std=c++17 \
  > "${REPORT_DIR}/refactor_perf.log" 2>&1

"${CXX}" -std=c++17 -O2 -g \
  -fno-omit-frame-pointer \
  "${ARTIFACTS_DIR}/perf_after.cpp" \
  -o "${ARTIFACTS_DIR}/perf_after"

perf stat \
  -o "${REPORT_DIR}/perf_after.txt" \
  "${ARTIFACTS_DIR}/perf_after"

echo "perf after report: ${REPORT_DIR}/perf_after.txt"

echo "== Done =="
echo "Generated reports:"
echo "  ${REPORT_DIR}/asan_before.txt"
echo "  ${REPORT_DIR}/asan_after.txt"
echo "  ${REPORT_DIR}/perf_before.txt"
echo "  ${REPORT_DIR}/perf_after.txt"
