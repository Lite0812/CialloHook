.386
.model flat, c
.code

PUBLIC litepak_asm_secure_bzero
PUBLIC litepak_asm_xor32
PUBLIC litepak_asm_fingerprint_mix

litepak_asm_secure_bzero PROC p:DWORD, n:DWORD
    push edi
    mov edi, p
    mov ecx, n
    test edi, edi
    jz done
    test ecx, ecx
    jz done
    xor eax, eax
    rep stosb
done:
    pop edi
    ret
litepak_asm_secure_bzero ENDP

litepak_asm_xor32 PROC share_a:DWORD, share_b:DWORD, outp:DWORD
    push esi
    push edi
    push ebx
    mov esi, share_a
    mov ebx, share_b
    mov edi, outp
    mov ecx, 8
xor_loop:
    mov eax, DWORD PTR [esi]
    xor eax, DWORD PTR [ebx]
    mov DWORD PTR [edi], eax
    add esi, 4
    add ebx, 4
    add edi, 4
    dec ecx
    jnz xor_loop
    pop ebx
    pop edi
    pop esi
    ret
litepak_asm_xor32 ENDP

litepak_asm_fingerprint_mix PROC fn:DWORD, window:DWORD, seed:DWORD
    push esi
    mov esi, fn
    mov ecx, window
    mov eax, seed
    test esi, esi
    jz mix_done
    test ecx, ecx
    jz mix_done
mix_loop:
    movzx edx, BYTE PTR [esi]
    add eax, edx
    rol eax, 5
    xor eax, 09E3779B9h
    inc esi
    dec ecx
    jnz mix_loop
mix_done:
    pop esi
    ret
litepak_asm_fingerprint_mix ENDP

END
