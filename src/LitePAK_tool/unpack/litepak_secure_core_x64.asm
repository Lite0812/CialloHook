OPTION CASEMAP:NONE

.code

PUBLIC litepak_asm_secure_bzero
PUBLIC litepak_asm_xor32
PUBLIC litepak_asm_fingerprint_mix

litepak_asm_secure_bzero PROC
    test rcx, rcx
    jz done
    test rdx, rdx
    jz done
zero_loop:
    mov byte ptr [rcx], 0
    inc rcx
    dec rdx
    jnz zero_loop
done:
    ret
litepak_asm_secure_bzero ENDP

litepak_asm_xor32 PROC
    mov rax, qword ptr [rcx]
    xor rax, qword ptr [rdx]
    mov qword ptr [r8], rax
    mov rax, qword ptr [rcx + 8]
    xor rax, qword ptr [rdx + 8]
    mov qword ptr [r8 + 8], rax
    mov rax, qword ptr [rcx + 16]
    xor rax, qword ptr [rdx + 16]
    mov qword ptr [r8 + 16], rax
    mov rax, qword ptr [rcx + 24]
    xor rax, qword ptr [rdx + 24]
    mov qword ptr [r8 + 24], rax
    ret
litepak_asm_xor32 ENDP

litepak_asm_fingerprint_mix PROC
    mov eax, r8d
    test rcx, rcx
    jz mix_done
    test rdx, rdx
    jz mix_done
mix_loop:
    movzx r9d, byte ptr [rcx]
    add eax, r9d
    rol eax, 5
    xor eax, 09E3779B9h
    inc rcx
    dec rdx
    jnz mix_loop
mix_done:
    ret
litepak_asm_fingerprint_mix ENDP

END
