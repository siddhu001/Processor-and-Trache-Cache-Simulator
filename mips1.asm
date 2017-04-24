lui $t1,0x1001
lui $t2,0x1011
addi $8,$8,400
loop:		
bge $1, $8, end		
lw $t3, ($t1)	
addi $t4,$t3,100
add $9, $4, $t2	
addi $t2,$v0,134
addi $t3,$v0,322	 
mult $t2, $t3		
madd $1, $8		
addi $1, $1, 4		
beq $0, $0, loop	
end:
 
