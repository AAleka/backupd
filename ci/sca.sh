#!/bin/bash

OUTPUT_DIR="build/sca"

make clean
mkdir -p ${OUTPUT_DIR} 2>/dev/null || true

python3 -m venv ${OUTPUT_DIR}/.venv
source ${OUTPUT_DIR}/.venv/bin/activate
pip install codechecker

CodeChecker log -b "make server" -o ${OUTPUT_DIR}/compile_commands.json
CodeChecker analyze ${OUTPUT_DIR}/compile_commands.json --analyzers clangsa clang-tidy --output ${OUTPUT_DIR}/analysis
CodeChecker parse ${OUTPUT_DIR}/analysis -o ${OUTPUT_DIR}/report --export "html"