.text
.globl main
main:
    move $fp, $sp          # Set frame pointer
    addiu $sp, $sp, -12    # Allocate 12 bytes on the stack

    sw $zero, -4($fp)      # a (int)
    sw $zero, -8($fp)      # b (int)
    sw $zero, -12($fp)     # d (int)

    # Assignment: a = 1
    li $t0, 1
    sw $t0, -4($fp)

    # Assignment: b = 2
    li $t0, 2
    sw $t0, -8($fp)

    # Load a and b
    lw $t0, -4($fp)
    lw $t1, -8($fp)

    # Compute: d = a + b
    add $t0, $t0, $t1      # $t0 = a + b
    sw $t0, -12($fp)       # Store result in d

    # Load d into $v0 for return
    lw $v0, -12($fp)

    # Print d
    move $a0, $v0          # Copy $v0 to $a0 (argument for syscall)
    li $v0, 1              # Set syscall to print integer
    syscall                # Execute syscall

    # Exit program
    li $v0, 10             # Set syscall to exit
    syscall                # Execute syscall
