@echo off
rem Build SymTerm with the SymbOS C Compiler. Run from SCC's bin folder or put it on PATH.
cc symterm.c render.s -lnet -h 512 -o symterm.exe -N "SymTerm" -G icons\icon4.sgx -g icons\icon16.sgx
