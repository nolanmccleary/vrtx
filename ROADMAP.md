# QLONQ OS Roadmap

## M1 — SDRAM Online
- Verify read/write via OpenOCD `mdw`/`mww` at `0x00000000`
- Update linker script: heap → SDRAM, OCRAM stays for vectors/stacks/critical path
- Walking pattern memory test to confirm stability

## M2 — UART Console
- Init HPS UART0 (`0xFFC02000`): baud rate, FIFO, clock enable
- `putchar` / `puts` / minimal `printf`
- Do this before the scheduler

## M3 — Preemptive Scheduler
- TCB struct: saved PC, SP, registers, state
- Per-task stacks from SDRAM heap
- Context switch in existing timer IRQ handler: save current task, pick next, restore
- Round-robin ready queue to start
- `task_create`, idle task
- **Proof**: two tasks writing to UART at different rates

## M4 — MMU
- Flat 1:1 physical→virtual map initially
- Guard pages, stack overflow detection
- Kernel/user split groundwork
- Full demand paging can come later — just get the hardware page walker running

## M5 — Ethernet Driver
- HPS EMAC0 (`0xFF700000`), Synopsys GMAC (DWC_gmac)
- PHY init over MDIO, link-up detection
- TX/RX DMA descriptor rings, interrupt-driven
- **Proof**: send a raw Ethernet frame, capture on laptop with Wireshark

## M6 — Custom Network Stack
Build bottom-up, each layer testable independently.

### M6a — ARP
- Parse inbound ARP requests, send replies
- Maintain a small ARP cache (MAC ↔ IP)
- ~100 lines

### M6b — IP
- IPv4 header parse/construct
- Checksum
- Demux to upper layers
- ~200 lines

### M6c — ICMP
- Echo reply (ping)
- **Proof**: `ping` from laptop gets a response

### M6d — UDP
- Header parse/construct, checksum
- Socket-like bind/send/recv API
- **Proof**: `netcat -u` from laptop reaches board

### M6e — Custom Shell Protocol over UDP
- Simple packet format: `[cmd_len][cmd_str]` → `[status][output]`
- Board receives command, executes, sends response
- Small client tool on laptop (Python or C, ~50 lines)
- **Proof**: type a command on laptop, see output

## M7 — SDMMC Driver
- HPS SDMMC controller (`0xFF704000`)
- Card init sequence: CMD0 → ACMD41 → CMD2 → CMD3
- Clock config, voltage negotiation
- DMA block read/write
- **Proof**: read first 512 bytes of SD card, matches known partition table

## M8 — Filesystem
Choose one:

**Option A — FAT32** (SD card already has one, interoperable with laptop)
- Partition table parse (MBR)
- FAT32 BPB, cluster chain walk
- Directory read, file read
- Write support
- ~800-1200 lines total

**Option B — Custom FS** (simpler)
- Fixed-size inodes, flat or shallow directory structure
- Append-only or simple block allocator
- Faster to implement, not interoperable
- This one preferred for now

### M8 sub-goals
- `ls` over network shell
- `cat` a file over network shell
- Write a file from network shell, power cycle, read it back

## M9 — Encryption (Optional)
- ChaCha20-Poly1305 is self-contained and far simpler than anything in SSH
- Shared key to start (no handshake), add a simple key exchange later if needed
- WireGuard's noise protocol is a clean reference

---

## Critical Path to "SSH into and do something"
```
M1 → M2 → M3 → M5 → M6 → M7 → M8
```
M4 (MMU) slots between M3 and M5 for safety, or after M8 if going fast.
M9 is purely optional for a local dev network.
