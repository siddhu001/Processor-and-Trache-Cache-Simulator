lui $t1,0x1001
move $t2,$t1
addi $1,$0,40
addi $2,$0,37
mult $2,$1
mflo $t3
sw $t3,($t1)
lw $t4,($t1)
sb $t4,4($t1)
lb $t5,($t1)
sw $t5,($t1)