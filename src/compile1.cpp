#include <iostream>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

// Function to process a line of C++ code
void process_line(const std::string& line, int& stack_offset, std::ofstream& outFile) {
    std::regex var_decl_regex(R"(int\s+(\w+)\s*(?:=\s*(\d+))?;)");
    std::regex assign_regex(R"((\w+)\s*=\s*(\d+);)");
    std::smatch matches;

    // Check for variable declaration
    if (std::regex_match(line, matches, var_decl_regex)) {
        std::string var_name = matches[1];
        std::string value_str = matches[2];
        int value = value_str.empty() ? 0 : std::stoi(value_str);

        outFile << "# Variable Declaration: " << var_name << ", Value: " << value << "\n";
        outFile << "addiu $sp, $sp, -4  # Allocate space for " << var_name << "\n";
        if (value != 0) {
            outFile << "li $t0, " << value << "       # Load value " << value << "\n";
            outFile << "sw $t0, " << stack_offset << "($sp)  # Store value in " << var_name << "\n";
        }
        stack_offset += 4;
    }
    // Check for assignment
    else if (std::regex_match(line, matches, assign_regex)) {
        std::string var_name = matches[1];
        int value = std::stoi(matches[2]);

        outFile << "# Assignment: " << var_name << " = " << value << "\n";
        outFile << "li $t0, " << value << "       # Load value " << value << "\n";
        outFile << "sw $t0, " << stack_offset - 4 << "($sp)  # Store value in " << var_name << "\n";
    }
}

int main() {
    // Open a file to write the assembly code
    std::ofstream outFile("output.s");

    if (!outFile) {
        std::cerr << "Error opening file for writing!" << std::endl;
        return 1;
    }
    
    // Write the default MIPS setup at the beginning
    outFile << ".globl main\n";
    outFile << "main:\n";
    outFile << "move $fp, $sp\n";  // Move frame pointer to stack pointer
    outFile << "addiu $sp, $sp, -0x100  # Allocate initial space for stack\n";

    std::vector<std::string> code = {
        "int a = 5;",
        "int b;",
        "int c = 10;",
        "a = 20;"
    };

    int stack_offset = 0;
    for (const auto& line : code) {
        process_line(line, stack_offset, outFile);
    }

    // Close the file after processing
    outFile.close();

    return 0;
}

