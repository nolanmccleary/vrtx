#!/bin/bash
set -e
ELF=${1:-build/qlonq.elf}
RESET_ADDR=0xffff04f4

ocd() {
    local cmd="$1"
    exec 3<>/dev/tcp/localhost/4444
    printf '%s\r\n' "$cmd" >&3
    sleep 0.3
    cat <&3 &
    local cat_pid=$!
    sleep 0.3
    kill $cat_pid 2>/dev/null
    exec 3>&-
}

ocd_read() {
    local cmd="$1"
    exec 3<>/dev/tcp/localhost/4444
    printf '%s\r\n' "$cmd" >&3
    sleep 0.4
    local out
    out=$(cat <&3 2>/dev/null)
    exec 3>&-
    echo "$out"
}

echo "==> Halting CPU"
ocd "halt"
sleep 0.5

echo "==> Verifying halt"
ocd_read "reg cpsr"

echo "==> Disabling MMU/caches (SCTLR=0)"
ocd "arm mcr 15 0 1 0 0 0x00000000"
sleep 0.3

echo "==> Verifying SCTLR=0"
ocd_read "arm mrc 15 0 1 0 0"

echo "==> Invalidating I-cache and TLBs"
ocd "arm mcr 15 0 7 5 0 0"
ocd "arm mcr 15 0 8 7 0 0"
sleep 0.3

echo "==> Loading ELF: $ELF"
ocd "load_image $ELF"
sleep 3

echo "==> Resuming at _reset ($RESET_ADDR)"
ocd "resume $RESET_ADDR"
sleep 0.2

echo "==> Sanity check: halting immediately to verify PC in OCRAM"
ocd "halt"
sleep 0.3
ocd_read "reg pc"
ocd_read "arm mrc 15 0 1 0 0"

echo "==> Resuming for real"
ocd "resume $RESET_ADDR"
sleep 0.5

echo "==> Done. CPU running."
