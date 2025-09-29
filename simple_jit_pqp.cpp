#include <iostream>
#include <vector>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <cstdio>

using namespace std;

#define REGISTERS_NUM 16
#define MEMORY_SIZE 256
#define INSTRUCTION_SIZE 4
#define SIZE_CODE 1024
#define PAGE_SIZE 4096

using JitFunc = uintptr_t (*)(int32_t *, uint32_t *, uint8_t *, uint32_t *);

struct Machine_x86
{
    vector<int32_t> registers;
    vector<uint8_t> memory;
    bool compare[3];
    uint32_t save_bool;
    vector<uint32_t> instruction_counts;
    vector<bool> not_interpreted;

    uint8_t *executable_code;
    uintptr_t code_base;

    Machine_x86() : registers(REGISTERS_NUM, 0),
                    memory(MEMORY_SIZE, 0),
                    compare{false, false, false},
                    save_bool(0),
                    instruction_counts(REGISTERS_NUM, 0),
                    not_interpreted(MEMORY_SIZE, true)
    {
        executable_code = (uint8_t *)mmap(nullptr, PAGE_SIZE,
                                          PROT_READ | PROT_WRITE | PROT_EXEC,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        code_base = (uintptr_t)executable_code;

        memset(executable_code, 0x90, PAGE_SIZE);
        executable_code[PAGE_SIZE - 1] = 0xC3;

        for (uint32_t i = 0; i < SIZE_CODE; i += 16)
        {
            executable_code[i] = 0x48;
            executable_code[i + 1] = 0x8D;
            executable_code[i + 2] = 0x05;
            executable_code[i + 3] = 0x00;
            executable_code[i + 4] = 0x00;
            executable_code[i + 5] = 0x00;
            executable_code[i + 6] = 0x00;
            executable_code[i + 7] = 0xC3;
        }
    }

    ~Machine_x86()
    {
        munmap(executable_code, PAGE_SIZE);
    }
};

int main(int argc, char *argv[])
{
    FILE *input = fopen(argv[1], "r");
    Machine_x86 vm;

    uint16_t pos = 0;
    uint16_t hex_value;
    while (fscanf(input, "%hx", &hex_value) == 1 && pos < MEMORY_SIZE)
    {
        vm.memory[pos++] = (uint8_t)hex_value;
    }
    fclose(input);

    FILE *output = fopen(argv[2], "w");

    uint16_t pc = 0;
    while (pc < pos)
    {
        if (vm.not_interpreted[pc])
        {
            uint8_t opcode = vm.memory[pc];
            uint32_t index = pc * 4;

            vm.not_interpreted[pc] = false;

            switch (opcode)
            {
            case 0x00: // mov rx, i16 (10 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                int32_t i32 = (int16_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));

                fprintf(output, "0x%04X->MOV_R%d=0x%08X\n", pc, (int)rx, (uint32_t)i32);

                rx = rx * 4;
                // mov dword ptr [rdi + rx], i32 (7 bytes)
                vm.executable_code[index++] = 0xC7;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                vm.executable_code[index++] = (i32 >> 0) & 0xFF;
                vm.executable_code[index++] = (i32 >> 8) & 0xFF;
                vm.executable_code[index++] = (i32 >> 16) & 0xFF;
                vm.executable_code[index++] = (i32 >> 24) & 0xFF;
                // inc dword ptr [rsi] (3 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x06;
                break;
            }

            case 0x01: // mov rx, ry (14 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;

                fprintf(output, "0x%04X->MOV_R%d=R%d=0x%08X\n", pc, (int)rx, (int)ry, vm.registers[ry]);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // mov dword ptr [rdi + rx], eax (3 bytes)
                vm.executable_code[index++] = 0x89;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 8] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x08;

                break;
            }

            case 0x02: // mov rx, [ry] (11 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                uint32_t address = vm.registers[ry];

                fprintf(output, "0x%04X->MOV_R%d=MEM[0x%02X,0x%02X,0x%02X,0x%02X]=[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                        pc, (int)rx, address, address + 1, address + 2, address + 3,
                        (int)vm.memory[address], (int)vm.memory[address + 1],
                        (int)vm.memory[address + 2], (int)vm.memory[address + 3]);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // mov eax, dword ptr [rdx + rax] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x04;
                vm.executable_code[index++] = 0x02;
                // mov dword ptr [rdi + rx], eax (3 bytes)
                vm.executable_code[index++] = 0x89;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 16] (4 bytes) - incrementa count[2] (2*8)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x10;

                break;
            }

            case 0x03: // mov [rx], ry (11 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                uint32_t address = vm.registers[rx];
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
                // mov eax, dword ptr [rdi + rx] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // mov ecx, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x4F;
                vm.executable_code[index++] = ry;
                // mov dword ptr [rdx + rax], ecx (3 bytes) - agora rdx é memory
                vm.executable_code[index++] = 0x89;
                vm.executable_code[index++] = 0x0C;
                vm.executable_code[index++] = 0x02;
                // inc dword ptr [rsi + 24] (4 bytes) - incrementa count[3] (3*8)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x18;

                break;
            }

            case 0x04: // cmp rx, ry (14 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t val_rx = vm.registers[rx];
                int32_t val_ry = vm.registers[ry];

                vm.compare[0] = val_rx > val_ry;
                vm.compare[1] = val_rx < val_ry;
                vm.compare[2] = val_rx == val_ry;

                fprintf(output, "0x%04X->CMP_R%d<=>R%d(G=%d,L=%d,E=%d)\n",
                        pc, (int)rx, (int)ry, vm.compare[0], vm.compare[1], vm.compare[2]);

                rx = rx * 4;
                ry = ry * 4;

                // mov eax, dword ptr [rdi + rx] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // cmp eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x3B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // pushf (1 byte)
                vm.executable_code[index++] = 0x9C;
                // pop rax (1 byte)
                vm.executable_code[index++] = 0x58;
                // mov dword ptr [rcx], eax (2 bytes)
                vm.executable_code[index++] = 0x89;
                vm.executable_code[index++] = 0x01;
                // inc qword ptr [rsi + 32] (4 bytes) - incrementa count[4] (4*8)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x20;

                break;
            }

            case 0x05: // jmp i16 (9 bytes)
            {
                int32_t offset = (int16_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                uint32_t target_pc = pc + INSTRUCTION_SIZE + offset;

                fprintf(output, "0x%04X->JMP_0x%04X\n", pc, (uint16_t)target_pc);

                // inc counter (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x28;

                if (target_pc >= MEMORY_SIZE)
                {
                    // mov eax, target_pc (5 bytes)
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;
                    // ret (1 byte)
                    vm.executable_code[index++] = 0xC3;
                }
                else
                {
                    // jmp rel32 normal (5 bytes)
                    int32_t jump_code = (target_pc - pc) * 4 - 9;
                    vm.executable_code[index++] = 0xE9;
                    vm.executable_code[index++] = (jump_code >> 0) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 24) & 0xFF;
                }

                break;
            }

            case 0x06: // jg i16 (14 bytes)
            {
                int32_t offset = (int16_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                uint32_t target_pc = pc + INSTRUCTION_SIZE + offset;
                int32_t jump_code = (target_pc - pc) * 4 - 14;

                fprintf(output, "0x%04X->JG_0x%04X\n", pc, (uint16_t)target_pc);

                // inc counter + restore flags (8 bytes) // inc qword ptr [rsi + 48]
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x30;
                vm.executable_code[index++] = 0x8B; // mov eax, [rcx]
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0x50; // push rax
                vm.executable_code[index++] = 0x9D; // popf

                if (target_pc >= MEMORY_SIZE)
                {
                    // jle +6 (pula mov+ret se condição falsa) (2 bytes)
                    vm.executable_code[index++] = 0x7E; // jle rel8 (+6)
                    vm.executable_code[index++] = 0x06;

                    // mov eax, target_pc (5 bytes) - só executa se jg for verdadeiro
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;

                    // ret (1 byte) - retorna target_pc se condição verdadeira
                    vm.executable_code[index++] = 0xC3;
                }
                else
                {
                    // Jump condicional nativo normal
                    // jg rel32 (6 bytes)
                    vm.executable_code[index++] = 0x0F;
                    vm.executable_code[index++] = 0x8F;
                    vm.executable_code[index++] = (jump_code >> 0) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 24) & 0xFF;
                }
                break;
            }

            case 0x07: // jl i16 (14 bytes)
            {
                int32_t offset = (int16_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));

                uint32_t target_pc = pc + INSTRUCTION_SIZE + offset;

                fprintf(output, "0x%04X->JL_0x%04X\n", pc, (uint16_t)target_pc);

                // inc counter + restore flags (8 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x38;
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0x50;
                vm.executable_code[index++] = 0x9D;

                if (target_pc >= MEMORY_SIZE)
                {
                    // jge +6 (pula mov+ret se condição falsa) (2 bytes)
                    vm.executable_code[index++] = 0x7D; // jge rel8 (+6)
                    vm.executable_code[index++] = 0x06;

                    // mov eax, target_pc (5 bytes) - só executa se jl for verdadeiro
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;

                    // ret (1 byte)
                    vm.executable_code[index++] = 0xC3;
                }
                else
                {
                    // jl rel32 (6 bytes)
                    int32_t jump_code = (target_pc - pc) * 4 - 14;
                    vm.executable_code[index++] = 0x0F;
                    vm.executable_code[index++] = 0x8C;
                    vm.executable_code[index++] = (jump_code >> 0) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 24) & 0xFF;
                }

                break;
            }

            case 0x08: // je i16 (14 bytes)
            {
                int32_t offset = (int16_t)(vm.memory[pc + 2] | (vm.memory[pc + 3] << 8));
                uint32_t target_pc = pc + INSTRUCTION_SIZE + offset;

                fprintf(output, "0x%04X->JE_0x%04X\n", pc, (uint16_t)target_pc);

                // inc counter + restore flags (8 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x40;
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0x50;
                vm.executable_code[index++] = 0x9D;

                if (target_pc >= MEMORY_SIZE)
                {
                    // mov eax, target_pc (5 bytes) - só executa se je for verdadeiro
                    vm.executable_code[index++] = 0xB8;
                    vm.executable_code[index++] = (target_pc >> 0) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 8) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 16) & 0xFF;
                    vm.executable_code[index++] = (target_pc >> 24) & 0xFF;

                    // ret (1 byte)
                    vm.executable_code[index++] = 0xC3;
                }
                else
                {
                    // je rel32 (6 bytes)
                    int32_t jump_code = (target_pc - pc) * 4 - 14;
                    vm.executable_code[index++] = 0x0F;
                    vm.executable_code[index++] = 0x84;
                    vm.executable_code[index++] = (jump_code >> 0) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 8) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 16) & 0xFF;
                    vm.executable_code[index++] = (jump_code >> 24) & 0xFF;
                }
                break;
            }

            case 0x09: // add rx, ry (10 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] + vm.registers[ry];

                fprintf(output, "0x%04X->ADD_R%d+=R%d=0x%08X+0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // add dword ptr [rdi + rx], eax (3 bytes)
                vm.executable_code[index++] = 0x01;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 72] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;

                break;
            }

            case 0x0A: // sub rx, ry (10 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] - vm.registers[ry];

                fprintf(output, "0x%04X->SUB_R%d-=R%d=0x%08X-0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // sub dword ptr [rdi + rx], eax (3 bytes)
                vm.executable_code[index++] = 0x29;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 80] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x50;

                break;
            }

            case 0x0B: // and rx, ry (10 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] & vm.registers[ry];

                fprintf(output, "0x%04X->AND_R%d&=R%d=0x%08X&0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // and dword ptr [rdi + rx], eax (3 bytes)
                vm.executable_code[index++] = 0x21;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 88] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x58;
                break;
            }

            case 0x0C: // or rx, ry (10 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] | vm.registers[ry];

                fprintf(output, "0x%04X->OR_R%d|=R%d=0x%08X|0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // or dword ptr [rdi + rx], eax (3 bytes)
                vm.executable_code[index++] = 0x09;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 96] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x60;
                break;
            }

            case 0x0D: // xor rx, ry (10 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t ry = vm.memory[pc + 1] & 0x0F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] ^ vm.registers[ry];

                fprintf(output, "0x%04X->XOR_R%d^=R%d=0x%08X^0x%08X=0x%08X\n",
                        pc, (int)rx, (int)ry, temp_rx, vm.registers[ry], temp);

                rx = rx * 4;
                ry = ry * 4;
                // mov eax, dword ptr [rdi + ry] (3 bytes)
                vm.executable_code[index++] = 0x8B;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = ry;
                // xor dword ptr [rdi + rx], eax (3 bytes)
                vm.executable_code[index++] = 0x31;
                vm.executable_code[index++] = 0x47;
                vm.executable_code[index++] = rx;
                // inc dword ptr [rsi + 104] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x68;

                break;
            }

            case 0x0E: // sal rx, i5 (8 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t shift_left = vm.memory[pc + 3] & 0x1F;
                int32_t temp_rx = vm.registers[rx];
                int32_t temp = vm.registers[rx] << shift_left;

                fprintf(output, "0x%04X->SAL_R%d<<=%d=0x%08X<<%d=0x%08X\n",
                        pc, (int)rx, (int)shift_left, temp_rx, (int)shift_left, temp);

                rx = rx * 4;
                // shl dword ptr [rdi + rx], shift_left (4 bytes)
                vm.executable_code[index++] = 0xC1;
                vm.executable_code[index++] = 0x67;
                vm.executable_code[index++] = rx;
                vm.executable_code[index++] = shift_left;
                // inc dword ptr [rsi + 112] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x70;

                break;
            }

            case 0x0F: // sar rx, i5 (8 bytes)
            {
                uint8_t rx = vm.memory[pc + 1] >> 4;
                uint8_t shift_right = vm.memory[pc + 3] & 0x1F;
                int32_t signed_val = vm.registers[rx];
                int32_t temp_rx = vm.registers[rx];
                signed_val >>= shift_right;

                fprintf(output, "0x%04X->SAR_R%d>>=%d=0x%08X>>%d=0x%08X\n",
                        pc, (int)rx, (int)shift_right, temp_rx, (int)shift_right, signed_val);

                rx = rx * 4;
                // sar dword ptr [rdi + rx], shift_right (4 bytes)
                vm.executable_code[index++] = 0xC1;
                vm.executable_code[index++] = 0x7F;
                vm.executable_code[index++] = rx;
                vm.executable_code[index++] = shift_right;
                // inc dword ptr [rsi + 120] (4 bytes)
                vm.executable_code[index++] = 0xFF;
                vm.executable_code[index++] = 0x46;
                vm.executable_code[index++] = 0x78;

                break;
            }

            default:
            {
                pc = 256;
                break;
            }
            }
        }

        uint8_t *jit_addr = vm.executable_code + (pc * 4);
        JitFunc func = (JitFunc)jit_addr;
        uintptr_t result = func(&vm.registers[0], &vm.instruction_counts[0], &vm.memory[0], &vm.save_bool);

        if (result >= vm.code_base && result < vm.code_base + SIZE_CODE)
        {
            pc = (result - vm.code_base) / 4;
            pc--;
        }
        else
        {
            pc = (uint32_t)result;
            break;
        }
    }

    fprintf(output, "0x%04X->EXIT\n", (uint16_t)pc);
    fprintf(output, "[");
    for (int i = 0; i < 15; i++)
    {
        fprintf(output, "%02X:%u,", i, vm.instruction_counts[i]);
    }
    fprintf(output, "0F:%u]\n", vm.instruction_counts[15]);
    fprintf(output, "[");
    for (size_t i = 0; i < REGISTERS_NUM - 1; ++i)
    {
        fprintf(output, "R%u=0x%08X,", (unsigned int)i, vm.registers[i]);
    }
    fprintf(output, "R15=0x%08X]", vm.registers[15]);

    fclose(output);

    return 0;
}