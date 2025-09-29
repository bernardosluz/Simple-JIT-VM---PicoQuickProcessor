#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define REGISTERS_NUM 16
#define MEMORY_SIZE 256
#define INTERPRETED_SIZE 64
#define INSTRUCTION_SIZE 4
#define SIZE_CODE 1024

typedef uintptr_t (*JitFunc)(int32_t *, uint32_t *, uint8_t *, uint32_t *);

struct Machine_x86
{
    int32_t registers[REGISTERS_NUM];
    uint8_t memory[MEMORY_SIZE];
    uint32_t save_bool;
    uint32_t instruction_counts[REGISTERS_NUM];
    bool not_interpreted[INTERPRETED_SIZE];

    uint8_t *executable_code;
    uintptr_t code_base;
};

int main(int argc, char *argv[])
{
    FILE *input = fopen(argv[1], "r");
    struct Machine_x86 vm = {0};
    long page_size = sysconf(_SC_PAGESIZE);
    memset(vm.not_interpreted, true, INTERPRETED_SIZE);

    vm.executable_code = (uint8_t *)mmap(NULL, page_size,
                                         PROT_READ | PROT_WRITE | PROT_EXEC,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    memset(vm.executable_code, 0x90, page_size);
    vm.code_base = (uintptr_t)vm.executable_code;
    vm.executable_code[page_size - 1] = 0xC3;

    for (uint32_t i = 0; i < SIZE_CODE; i += 16)
    {
        // lea rax, [rip+0] - endereço atual
        vm.executable_code[i] = 0x48;
        vm.executable_code[i + 1] = 0x8D;
        vm.executable_code[i + 2] = 0x05;
        vm.executable_code[i + 3] = 0x00;
        vm.executable_code[i + 4] = 0x00;
        vm.executable_code[i + 5] = 0x00;
        vm.executable_code[i + 6] = 0x00;
        // ret (1 byte)
        vm.executable_code[i + 7] = 0xC3;
    }

    // mov eax, target_pc
    vm.executable_code[SIZE_CODE] = 0xB8;
    vm.executable_code[SIZE_CODE + 1] = 0x00;
    vm.executable_code[SIZE_CODE + 2] = 0x01;
    vm.executable_code[SIZE_CODE + 3] = 0x00;
    vm.executable_code[SIZE_CODE + 4] = 0x00;
    // ret (1 byte)
    vm.executable_code[SIZE_CODE + 5] = 0xC3;

    uint16_t pos = 0;
    uint8_t hex_value;
    while (fscanf(input, "%hhx", &hex_value) == 1)
    {
        vm.memory[pos++] = hex_value;
    }
    fclose(input);

    FILE *output = fopen(argv[2], "w");
    uint8_t pc = 0;
    uint8_t opcode = 0;
    uint32_t index = 0;
    uint32_t temp_pc = 0;
    while (pc < pos)
    {
        if (vm.not_interpreted[pc / 4])
        {
            vm.not_interpreted[pc / 4] = false;
            opcode = vm.memory[pc];
            index = pc * 4;
            switch (opcode)
            {
            case 0x00: // mov rx, i16
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                int32_t i32 = (int16_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                fprintf(output, "0x%04X->MOV_R%d=0x%08X\n", pc, (int)rx, (int32_t)i32);
                rx = rx * 4;
                // mov dword ptr [rdi + rx], i32
                vm.executable_code[index++] = 0xC7;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                vm.executable_code[index++] = (i32 >> 0) & 0xFF;
                vm.executable_code[index++] = (i32 >> 8) & 0xFF;
                vm.executable_code[index++] = (i32 >> 16) & 0xFF;
                vm.executable_code[index++] = (i32 >> 24) & 0xFF;
                // inc dword ptr [rsi]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index] = 0x06;

                break;
            }

            case 0x01: // mov rx, ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                fprintf(output, "0x%04X->MOV_R%d=R%d=0x%08X\n", pc, (int)rx, (int)ry, vm.registers[ry]);
                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // mov dword ptr [rdi + rx], eax
                vm.executable_code[index++] = 0x89;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 8]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x04;
                break;
            }

            case 0x02: // mov rx, [ry]
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                uint8_t address = vm.registers[ry];
                fprintf(output, "0x%04X->MOV_R%d=MEM[0x%02X,0x%02X,0x%02X,0x%02X]=[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                        pc, (int)rx, address, address + 1, address + 2, address + 3,
                        (int)vm.memory[address], (int)vm.memory[address + 1],
                        (int)vm.memory[address + 2], (int)vm.memory[address + 3]);
                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // movzx eax, al  ; Trunca o ENDEREÇO para 8 bits
                vm.executable_code[index++] = 0x0F;
                vm.executable_code[index++] = 0xB6;
                vm.executable_code[index++] = 0xC0;
                // mov eax, dword ptr [rdx + rax]  ; Acessa memória com endereço truncado
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x04;
                vm.executable_code[index++] = 0x02;
                // mov dword ptr [rdi + rx], eax
                vm.executable_code[index++] = 0x89;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 16]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x08;
                break;
            }

            case 0x03: // mov [rx], ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                uint8_t address = vm.registers[rx];
                int32_t value = vm.registers[ry];
                uint8_t temp1 = (value & 0x000000FF);
                uint8_t temp2 = (value & 0x0000FF00) >> 8;
                uint8_t temp3 = (value & 0x00FF0000) >> 16;
                uint8_t temp4 = (value & 0xFF000000) >> 24;
                fprintf(output, "0x%04X->MOV_MEM[0x%02X,0x%02X,0x%02X,0x%02X]=R%d=[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                        pc, address, address + 1, address + 2, address + 3, (int)ry,
                        (int)temp1, (int)temp2, (int)temp3, (int)temp4);
                rx = rx * 4;
                ry = ry * 4;

                // push rbx
                vm.executable_code[index++] = 0x53;
                // mov eax, dword ptr [rdi + rx]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // mov ebx, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x5F;
                vm.executable_code[index++] = ry;
                // mov dword ptr [rdx + rax], ebx
                vm.executable_code[index++] = 0x89;
                vm.executable_code[index++] = 0x1C;
                vm.executable_code[index++] = 0x02;
                // pop rbx
                vm.executable_code[index++] = 0x5B;
                // inc dword ptr [rsi + 24]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x0C;
                break;
            }

            case 0x04: // cmp rx, ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t val_rx = vm.registers[rx];
                int32_t val_ry = vm.registers[ry];
                bool g_flag = val_rx > val_ry;
                bool l_flag = val_rx < val_ry;
                bool e_flag = val_rx == val_ry;
                fprintf(output, "0x%04X->CMP_R%d<=>R%d(G=%d,L=%d,E=%d)\n",
                        pc, rx, ry, g_flag, l_flag, e_flag);
                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + rx]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // cmp eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x3B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // lahf  (1 byte)
                vm.executable_code[index++] = 0x9F;
                // mov al, ah
                vm.executable_code[index++] = 0x8A;
                vm.executable_code[index++] = 0xC4;
                // mov byte ptr [rcx], al
                vm.executable_code[index++] = 0x88;
                vm.executable_code[index++] = 0x01;
                // inc counter
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x10;

                break;
            }

            case 0x05: // jmp i16
            {
                int32_t offset = (int32_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                uint16_t target_pc = pc + INSTRUCTION_SIZE + offset;
                fprintf(output, "0x%04X->JMP_0x%04X\n", pc, target_pc);
                // inc counter
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x14;

                if (target_pc >= MEMORY_SIZE)
                {
                    // mov eax, target_pc
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;
                    // ret (1 byte)
                    vm.executable_code[index] = 0xC3;
                }
                else
                {
                    // jmp rel32 normal
                    int32_t jump_code = (target_pc - pc) * 4 - 8;
                    vm.executable_code[index++] = 0xE9;
                    vm.executable_code[index++] = (jump_code) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index] = (jump_code >> 24) & 0xFF;
                }

                break;
            }

            case 0x06: // jg i16 /
            {
                int32_t offset = (int32_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                uint16_t target_pc = pc + INSTRUCTION_SIZE + offset;
                fprintf(output, "0x%04X->JG_0x%04X\n", pc, target_pc);
                // inc counter
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x18;
                // test byte ptr [rcx], 0xC0 - testa ZF e SF
                vm.executable_code[index++] = 0xF6;
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0xC0;

                if (target_pc >= MEMORY_SIZE)
                {
                    // jnz +6
                    vm.executable_code[index++] = 0x75;
                    vm.executable_code[index++] = 0x06;
                    // mov eax, target_pc
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;
                    // ret (1 byte)
                    vm.executable_code[index] = 0xC3;
                }
                else
                {
                    int32_t jump_code = (target_pc - pc) * 4 - 12;
                    // jz rel32
                    vm.executable_code[index++] = 0x0F;
                    vm.executable_code[index++] = 0x84;
                    vm.executable_code[index++] = (jump_code >> 0) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index] = (jump_code >> 24) & 0xFF;
                }

                break;
            }

            case 0x07: // jl i16 /
            {
                int32_t offset = (int32_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                uint16_t target_pc = pc + INSTRUCTION_SIZE + offset;
                fprintf(output, "0x%04X->JL_0x%04X\n", pc, target_pc);
                // inc counter
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x1C;
                // test byte ptr [rcx], 0x80 - testa apenas SF
                vm.executable_code[index++] = 0xF6;
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0x80;

                if (target_pc >= MEMORY_SIZE)
                {
                    // jz +6
                    vm.executable_code[index++] = 0x74;
                    vm.executable_code[index++] = 0x06;
                    // mov eax, target_pc
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;
                    // ret (1 byte)
                    vm.executable_code[index] = 0xC3;
                }
                else
                {
                    int32_t jump_code = (target_pc - pc) * 4 - 12;
                    // jnz rel32
                    vm.executable_code[index++] = 0x0F;
                    vm.executable_code[index++] = 0x85;
                    vm.executable_code[index++] = (jump_code >> 0) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index] = (jump_code >> 24) & 0xFF;
                }
                break;
            }

            case 0x08: // je i16 /
            {
                int32_t offset = (int32_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                uint16_t target_pc = pc + INSTRUCTION_SIZE + offset;
                fprintf(output, "0x%04X->JE_0x%04X\n", pc, target_pc);
                // inc counter
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x20;
                // test byte ptr [rcx], 0x40 - testa apenas ZF
                vm.executable_code[index++] = 0xF6;
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0x40;

                if (target_pc >= MEMORY_SIZE)
                {
                    // jz +6
                    vm.executable_code[index++] = 0x74;
                    vm.executable_code[index++] = 0x06;
                    // mov eax, target_pc
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;
                    // ret (1 byte)
                    vm.executable_code[index] = 0xC3;
                }
                else
                {
                    int32_t jump_code = (target_pc - pc) * 4 - 12;
                    // jnz rel32
                    vm.executable_code[index++] = 0x0F;
                    vm.executable_code[index++] = 0x85;
                    vm.executable_code[index++] = (jump_code >> 0) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index] = (jump_code >> 24) & 0xFF;
                }
                break;
            }

            case 0x09: // add rx, ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] + vm.registers[ry];
                fprintf(output, "0x%04X->ADD_R%d+=R%d=0x%08X+0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);
                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // add dword ptr [rdi + rx], eax
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 72]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x24;
                break;
            }

            case 0x0A: // sub rx, ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] - vm.registers[ry];
                fprintf(output, "0x%04X->SUB_R%d-=R%d=0x%08X-0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);
                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // sub dword ptr [rdi + rx], eax
                vm.executable_code[index++] = 0x29;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 80]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x28;
                break;
            }

            case 0x0B: // and rx, ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] & vm.registers[ry];
                fprintf(output, "0x%04X->AND_R%d&=R%d=0x%08X&0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // and dword ptr [rdi + rx], eax
                vm.executable_code[index++] = 0x21;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 88]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x2C;
                break;
            }

            case 0x0C: // or rx, ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] | vm.registers[ry];

                fprintf(output, "0x%04X->OR_R%d|=R%d=0x%08X|0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // or dword ptr [rdi + rx], eax
                vm.executable_code[index++] = 0x09;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 96]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x30;
                break;
            }

            case 0x0D: // xor rx, ry
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] ^ vm.registers[ry];

                fprintf(output, "0x%04X->XOR_R%d^=R%d=0x%08X^0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry]
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // xor dword ptr [rdi + rx], eax
                vm.executable_code[index++] = 0x31;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 104]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index] = 0x34;
                break;
            }

            case 0x0E: // sal rx, i5
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t shift_left = vm.memory[pc + 3] & 0x1F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] << shift_left;

                fprintf(output, "0x%04X->SAL_R%d<<=%d=0x%08X<<%d=0x%08X\n",
                        pc, (int)rx, (int)shift_left, temp_rx, (int)shift_left, temp);

                rx = rx * 4;
                // shl dword ptr [rdi + rx], shift_left
                vm.executable_code[index++] = 0xC1;
                vm.executable_code[index++] = 0x67;
                vm.executable_code[index++] = rx;
                vm.executable_code[index++] = shift_left;
                // inc dword ptr [rsi + 112]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x38;
                vm.executable_code[index] = 0x90;
                break;
            }

            case 0x0F: // sar rx, i5
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t shift_right = vm.memory[pc + 3] & 0x1F;
                int32_t signed_val = vm.registers[rx];
                int32_t temp_rx = vm.registers[rx];
                signed_val >>= shift_right;

                fprintf(output, "0x%04X->SAR_R%d>>=%d=0x%08X>>%d=0x%08X\n",
                        pc, (int)rx, (int)shift_right, temp_rx, (int)shift_right, signed_val);

                rx = rx * 4;
                // sar dword ptr [rdi + rx], shift_right
                vm.executable_code[index++] = 0xC1;
                vm.executable_code[index++] = 0x7F;
                vm.executable_code[index++] = rx;
                vm.executable_code[index++] = shift_right;
                // inc dword ptr [rsi + 120]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x3C;
                vm.executable_code[index] = 0x90;
                break;
            }

            default:
            {
                break;
            }
            }
        }
        uint8_t *jit_addr = vm.executable_code + (pc * 4);
        JitFunc func = (JitFunc)jit_addr;
        uintptr_t result = func(&vm.registers[0], &vm.instruction_counts[0], &vm.memory[0], &vm.save_bool);
        if (result >= vm.code_base && result < vm.code_base + SIZE_CODE)
        {
            temp_pc = (uint32_t)(result - vm.code_base) / 4;
            temp_pc--;
        }
        else
        {
            temp_pc = (uint32_t)result;
            if (temp_pc >= MEMORY_SIZE)
            {
                break;
            }
        }
        pc = temp_pc;
    }

    fprintf(output, "0x%04X->EXIT\n", (uint16_t)temp_pc);
    fprintf(output, "[");
    for (int i = 0; i < 15; i++)
    {
        fprintf(output, "%02X:%u,", i, vm.instruction_counts[i]);
    }
    fprintf(output, "0F:%u]\n", vm.instruction_counts[15]);
    fprintf(output, "[");
    for (int i = 0; i < REGISTERS_NUM - 1; i++)
    {
        fprintf(output, "R%u=0x%08X,", i, vm.registers[i]);
    }
    fprintf(output, "R15=0x%08X]", vm.registers[15]);

    fclose(output);

    munmap(vm.executable_code, page_size);

    return 0;
}