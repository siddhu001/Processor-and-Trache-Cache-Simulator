lui $t1,0x1001
lui $t2,0x1011
add $t3,$t3,5
jal make
or $t7, $t1, $t2
sll $a1,$3,10
jr $t6
make:
ori $t4, $t7,0x1001
sllv $t5, $t4, $t3
jalr $t6,$ra
