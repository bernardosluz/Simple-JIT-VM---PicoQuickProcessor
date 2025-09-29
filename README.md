# Simple JIT VM

Um compilador Just-In-Time (JIT) simples para uma máquina virtual customizada de 16 registradores, implementado em C.

Este projeto foi desenvolvido como trabalho final para a disciplina **Interface Hardware e Software** e implementa um processador hipotético chamado **PicoQuickProcessor (PQP)**. Ele demonstra os conceitos básicos da compilação JIT ao traduzir um bytecode customizado para código de máquina x86-64 nativo em tempo de execução.

Quando uma instrução é encontrada pela primeira vez, ela é compilada para código x86-64 executável e armazenada em cache. Chamadas subsequentes para a mesma instrução executarão o código nativo diretamente, evitando a sobrecarga da interpretação.

## ⚙️ Funcionalidades

* **Compilação JIT:** Traduz o bytecode do PicoQuickProcessor para x86-64 nativo em tempo de execução.
* **Máquina Virtual:** Uma VM simples com:
    * 16 registradores de 32 bits de uso geral (R0-R15).
    * 256 bytes de memória.
* **Conjunto de Instruções:** Um conjunto customizado de 16 instruções, incluindo movimentação de dados, operações aritméticas, lógicas e saltos condicionais.

## 📜 Arquitetura do Conjunto de Instruções (ISA) - PicoQuickProcessor

A VM opera com instruções de 4 bytes de comprimento. O formato e as operações são descritos abaixo:

| Opcode | Mnemônico | Descrição |
| :----: | :------------ | :---------------------------------------------------------- |
| `0x00` | `mov rx, i16` | Move um valor imediato de 16 bits para o registrador `rx`. |
| `0x01` | `mov rx, ry` | Copia o valor do registrador `ry` para o registrador `rx`. |
| `0x02` | `mov rx, [ry]`| Carrega um valor de 4 bytes da memória (no endereço contido em `ry`) para `rx`. |
| `0x03` | `mov [rx], ry`| Armazena o valor do registrador `ry` na memória (no endereço contido em `rx`). |
| `0x04` | `cmp rx, ry` | Compara `rx` e `ry` e define as flags internas (Maior, Menor, Igual). |
| `0x05` | `jmp i16` | Salta incondicionalmente para o endereço calculado a partir de um offset de 16 bits. |
| `0x06` | `jg i16` | Salta se a flag 'Maior' (Greater) estiver definida. |
| `0x07` | `jl i16` | Salta se a flag 'Menor' (Less) estiver definida. |
| `0x08` | `je i16` | Salta se a flag 'Igual' (Equal) estiver definida. |
| `0x09` | `add rx, ry` | Adiciona o valor de `ry` a `rx`. |
| `0x0A` | `sub rx, ry` | Subtrai o valor de `ry` de `rx`. |
| `0x0B` | `and rx, ry` | Realiza um AND bit a bit entre `rx` e `ry`, armazenando o resultado em `rx`. |
| `0x0C` | `or rx, ry` | Realiza um OR bit a bit entre `rx` e `ry`, armazenando o resultado em `rx`. |
| `0x0D` | `xor rx, ry` | Realiza um XOR bit a bit entre `rx` e `ry`, armazenando o resultado em `rx`. |
| `0x0E` | `sal rx, i5` | Realiza um deslocamento aritmético para a esquerda em `rx` por um valor imediato de 5 bits. |
| `0x0F` | `sar rx, i5` | Realiza um deslocamento aritmético para a direita em `rx` por um valor imediato de 5 bits. |

<img width="880" height="738" alt="image" src="https://github.com/user-attachments/assets/39119451-e5bc-4ec6-9c37-4de6ede3ab80" />


## 🚀 Como Compilar e Executar

### Pré-requisitos
* Um compilador C (como o GCC).
* Um sistema operacional baseado em Linux (devido ao uso de `sys/mman.h` e `unistd.h`).

### Compilação
Compile o código-fonte usando o GCC:
```bash
gcc -o jit_vm main.c
```

### Execução
O programa recebe dois argumentos: o arquivo de entrada com o bytecode e o arquivo de saída para o log de execução.

```bash
./jit_vm input.txt output.txt
```

* `input.txt`: Contém os valores hexadecimais do bytecode a ser executado.
* `output.txt`: Onde o log da execução, os contadores de instruções e o estado final dos registradores serão salvos.

## 📝 Exemplo de Uso

<details>
<summary>Clique aqui para ver o arquivo de entrada de teste</summary>

```
0x00 0x00 0x0D 0x00
0x01 0x12 0x00 0x00
0x02 0x34 0x00 0x00
0x03 0x56 0x00 0x00
0x04 0x78 0x00 0x00
0x05 0x00 0x00 0x00
0x06 0x00 0xE4 0xFF
0x07 0x00 0xDF 0xFF
0x08 0x00 0x00 0x00
0x09 0x9A 0x00 0x00
0x0A 0xBC 0x00 0x00
0x0B 0xDE 0x00 0x00
0x0C 0xF0 0x00 0x00
0x0D 0x12 0x00 0x00
0x0E 0x30 0x00 0x03
0x0F 0x30 0x00 0x0F
0x05 0x00 0xBC 0x00
0x78 0x56 0x34 0x12
0x39 0x30 0x00 0x00
```
</details>

<details>
<summary>Clique aqui para ver o arquivo de saída de teste</summary>

```
0x0000->MOV_R0=0x0000000D
0x0004->MOV_R1=R2=0x00000000
0x0008->MOV_R3=MEM[0x00,0x01,0x02,0x03]=[0x00,0x00,0x0D,0x00]
0x000C->MOV_MEM[0x00,0x01,0x02,0x03]=R6=[0x00,0x00,0x00,0x00]
0x0010->CMP_R7<=>R8(G=0,L=0,E=1)
0x0014->JMP_0x0018
0x0018->JG_0x0000
0x001C->JL_0xFFFF
0x0020->JE_0x0024
0x0024->ADD_R9+=R10=0x00000000+0x00000000=0x00000000
0x0028->SUB_R11-=R12=0x00000000-0x00000000=0x00000000
0x002C->AND_R13&=R14=0x00000000&0x00000000=0x00000000
0x0030->OR_R15|=R0=0x00000000|0x0000000D=0x0000000D
0x0034->XOR_R1^=R2=0x00000000^0x00000000=0x00000000
0x0038->SAL_R3<<=3=0x000D0000<<3=0x00680000
0x003C->SAR_R3>>=15=0x00680000>>15=0x000000D0
0x0040->JMP_0x0100
0x0100->EXIT
[00:1,01:1,02:1,03:1,04:1,05:2,06:1,07:1,08:1,09:1,0A:1,0B:1,0C:1,0D:1,0E:1,0F:1]
[R0=0x0000000D,R1=0x00000000,R2=0x00000000,R3=0x000000D0,R4=0x00000000,R5=0x00000000,R6=0x00000000,R7=0x00000000,R8=0x00000000,R9=0x00000000,R10=0x00000000,R11=0x00000000,R12=0x00000000,R13=0x00000000,R14=0x00000000,R15=0x0000000D]
```
</details>
