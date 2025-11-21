#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Test program to verify Panda USM DOUBLE64 conversion
int main() {
    // Test data from Modbus Poll: Register 4 shows 1513.5334743
    // In your screenshot, register 4 (address 00004) shows value 1327.9969036...
    // and register 8 (address 00008) shows 1513.5334743...

    // Let's simulate the raw register values for 1513.5334743
    // This is a 64-bit double in big-endian format

    double test_value = 1513.5334743;
    uint64_t raw_value;
    memcpy(&raw_value, &test_value, sizeof(double));

    printf("Test value: %.7f\n", test_value);
    printf("Raw 64-bit: 0x%016llX\n", (unsigned long long)raw_value);

    // Convert to 4 Modbus registers (big-endian)
    uint16_t registers[4];
    registers[0] = (raw_value >> 48) & 0xFFFF;
    registers[1] = (raw_value >> 32) & 0xFFFF;
    registers[2] = (raw_value >> 16) & 0xFFFF;
    registers[3] = raw_value & 0xFFFF;

    printf("Modbus Registers (Big-Endian):\n");
    printf("  Reg[0]: 0x%04X\n", registers[0]);
    printf("  Reg[1]: 0x%04X\n", registers[1]);
    printf("  Reg[2]: 0x%04X\n", registers[2]);
    printf("  Reg[3]: 0x%04X\n", registers[3]);

    // Reconstruct the value (as our code does)
    uint64_t combined_value64 = ((uint64_t)registers[0] << 48) |
                               ((uint64_t)registers[1] << 32) |
                               ((uint64_t)registers[2] << 16) |
                               registers[3];

    double reconstructed;
    memcpy(&reconstructed, &combined_value64, sizeof(double));

    printf("\nReconstructed value: %.7f\n", reconstructed);
    printf("Match: %s\n", (reconstructed == test_value) ? "YES" : "NO");

    printf("\n=== Configuration for your sensor ===\n");
    printf("Sensor Type: Panda_USM\n");
    printf("Slave ID: 1\n");
    printf("Register Address: 4 (for Net Volume)\n");
    printf("Register Type: HOLDING\n");
    printf("Quantity: 4 (automatically set for Panda_USM)\n");
    printf("Data Type: (not used, Panda_USM uses DOUBLE64 internally)\n");
    printf("Scale Factor: 1.0\n");

    return 0;
}