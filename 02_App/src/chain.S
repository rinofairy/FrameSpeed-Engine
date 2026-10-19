/* Position-linked DDR trampoline. No stack, GP, or CPU-RAM fetch after entry. */
.section .text
.global _chain_start
_chain_start:
 csrci mstatus, 8
 li t0, 2
 bgtu a0, t0, bad
 beqz a0, program
 li t0, 1
 beq a0, t0, demo
 li t0, 0x01500000
 li t1, 0x0047ffc0
 li t2, 0x01010000
 lw t2, 64(t2)
 j assets
demo:
 li t0, 0x01400000
 li t1, 0x0073ffc0
 li t2, 0x01010000
 lw t2, 52(t2)
assets:
 lw t3, 0(t0)
 sw t3, 0(t1)
 addi t0, t0, 4
 addi t1, t1, 4
 addi t2, t2, -4
 bnez t2, assets
program:
 li t0, 0x01100000
 slli t1, a0, 17
 add t0, t0, t1
 li t1, 0x1000
 li t2, 0x1f000
copy:
 lw t3, 0(t0)
 sw t3, 0(t1)
 addi t0, t0, 4
 addi t1, t1, 4
 addi t2, t2, -4
 bnez t2, copy
 li t0, 0x4653574d
 csrw mscratch, t0
 fence rw, rw
 .word 0x0000500f
 fence rw, rw
 .word 0x0000100f
 li t0, 0x1000
 jr t0
bad:
 j bad
