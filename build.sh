#!/bin/bash

SourceFile="splay.c"
OutputFile="splay"

CompileFlags=" \
    -g \
    -Wall -Wextra -Wpedantic -Werror \
    -Wno-unused-variable \
    -Wno-unused-function \
    -Wno-unused-parameter \
    -Wno-unused-but-set-variable \
    -o $OutputFile"

LinkFlags=" \
    -fuse-ld=lld \
    -Wl,-lasound"

clang $CompileFlags $SourceFile $LinkFlags

