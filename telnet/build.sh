#!/bin/sh
# Build SymTerm with the SymbOS C Compiler (https://github.com/danielgaskell/scc).
# Set SCC to the folder containing SCC's bin/ directory.
SCC=${SCC:-$HOME/scc}
set -e
cd "$(dirname "$0")"
"$SCC/bin/cc" symterm.c render.s -lnet -h 512 -o symterm.exe -N "SymTerm" \
    -G icons/icon4.sgx -g icons/icon16.sgx -M
