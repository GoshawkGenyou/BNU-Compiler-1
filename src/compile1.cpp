#include <iostream>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>

// Symbol Table to manage variable declarations and offsets
class SymbolTable {
public:
    std::unordered_map<std::string, int> table;

    bool add_variable(const std::string& var_name, int& stack_offset) {
        if (table.find(var_name) != table.end()) {
            return false;
        }
        stack_offset -= 4;  // Increment for the next variable
        table[var_name] = stack_offset;
        return true;
    }

    int get_offset(const std::string& var_name) {
        return table.count(var_name) ? table[var_name] : -1;
    }
};

std::regex var_decl_regex(R"(int\s+(\w+)\s*(?:=\s*(\d+))?;)");
std::regex assign_regex(R"((\w+)\s*=\s*(\d+);)");
std::regex op_regex(R"((\w+)\s*=\s*(.+);)");
std::regex return_regex(R"(return\s*(\w+)?\s*;)");
std::stack<std::string> free_temps;

std::string process_expression(const std::string& expr, SymbolTable& symbol_table, 
                               std::ostream& outFile, int& temp_var_count, 
                               int& stack_offset, std::stack<std::string>& free_temps) {
    std::stack<int> paren_stack;
    std::unordered_map<int, int> matching_paren;
    std::string processed_expr = expr;

    // Check for mismatched parentheses
    for (size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] == '(') {
            paren_stack.push(i);
        } else if (expr[i] == ')') {
            if (!paren_stack.empty()) {
                matching_paren[paren_stack.top()] = i;
                paren_stack.pop();
            } else {
                outFile << "# Error: Mismatched parentheses.\n";
                return "";
            }
        }
    }

    if (!paren_stack.empty()) {
        outFile << "# Error: Unmatched opening parenthesis.\n";
        return "";
    }

    // Replace sub-expressions (e.g., (a + b)) with temp variables
    std::regex sub_expr_regex(R"(\(([^()]+)\))");
    std::smatch match;
    std::unordered_map<std::string, std::string> subexpr_replacements;
    std::vector<std::string> used_temps;

    // Replace sub-expressions with temporary variables
    while (std::regex_search(processed_expr, match, sub_expr_regex)) {
        std::string inner_expr = match[1].str();
        std::string temp_var;

        // Get a temporary variable to store the result of the sub-expression
        if (!free_temps.empty()) {
            temp_var = free_temps.top();
            free_temps.pop();
        } else {
            temp_var = "temp" + std::to_string(temp_var_count++);
            outFile << "# Saved temp var " << temp_var << " for parenthesis purposes\n";
            if (!symbol_table.add_variable(temp_var, stack_offset)) {
                outFile << "# Error: Failed to register temporary variable: " << temp_var << "\n";
                return "";
            }
        }

        subexpr_replacements[inner_expr] = temp_var;
        used_temps.push_back(temp_var);
        processed_expr.replace(match.position(), match.length(), temp_var);
    }

    // Process the subexpressions
    for (const auto& [subexpr, temp_var] : subexpr_replacements) {
        std::smatch op_match;
        std::regex op_extract(R"((\w+)\s*([+\-*/])\s*(\w+))");

        // Process operations like a + b, a - b, etc.
        if (std::regex_match(subexpr, op_match, op_extract)) {
            std::string operand1 = op_match[1];
            std::string op = op_match[2];
            std::string operand2 = op_match[3];

            // Look up the offsets of the operands in the symbol table
            int offset1 = symbol_table.get_offset(operand1);
            int offset2 = symbol_table.get_offset(operand2);
            int temp_offset = symbol_table.get_offset(temp_var);

            if (offset1 == -1 || offset2 == -1 || temp_offset == -1) {
                outFile << "# Error: Undefined variable in subexpression.\n";
                return "";
            }

            // Load the operands into registers
            outFile << "# Compute: " << temp_var << " = " << operand1 << " " << op << " " << operand2 << "\n";
            outFile << "lw $t0, " << offset1 << "($fp)\n";  // Load operand1
            outFile << "lw $t1, " << offset2 << "($fp)\n";  // Load operand2

            // Perform the arithmetic operation
            if (op == "+") outFile << "add $t2, $t0, $t1\n";
            else if (op == "-") outFile << "sub $t2, $t0, $t1\n";
            else if (op == "*") outFile << "mul $t2, $t0, $t1\n";
            else if (op == "/") {
                outFile << "div $t0, $t1\n";
                outFile << "mflo $t2\n";  // Store result in $t2 after division
            }

            // Store the result in the temp variable's location
            outFile << "sw $t2, " << temp_offset << "($fp)\n";
        }
    }

    // Restore the temporary variables for future use
    for (const std::string& temp : used_temps) {
        free_temps.push(temp);
    }

    // Return the processed expression with temporary variables replaced
    return processed_expr;
}

void process_line(const std::string& line, SymbolTable& symbol_table, 
                  int& stack_offset, std::ofstream& outFile, 
                  int& temp_var_count, std::stack<std::string>& free_temps) {
	
	std::smatch matches;
	
	if (std::regex_match(line, matches, var_decl_regex)) {
        std::string var_name = matches[1];
        int value = matches[2].matched ? std::stoi(matches[2].str()) : 0;

        // Add the variable to the symbol table with the current stack offset
        if (!symbol_table.add_variable(var_name, stack_offset)) {
            outFile << "# Error: Variable '" << var_name << "' already declared.\n";
            return;
        }
        
        // Output the initialization with the frame pointer
        outFile << "sw $zero, " << stack_offset << "($fp)  # " << var_name << " (int)\n";

        // If the variable has an initial value, load the value into a temporary register and store it
        if (value != 0) {
            outFile << "li $t0, " << value << "\n";  // Load immediate value into $t0
            outFile << "sw $t0, " << stack_offset << "($fp)  # Store " << var_name << " with value\n";
        }
    }
    
    // Assignment
    else if (std::regex_match(line, matches, assign_regex)) {
        std::string var_name = matches[1];
        int value = std::stoi(matches[2]);

        int offset = symbol_table.get_offset(var_name);
        if (offset == -1) {
            outFile << "# Error: Variable '" << var_name << "' not declared.\n";
            return;
        }
        outFile << "# Assignment: " << var_name << " = " << value << "\n";
        outFile << "li $t0, " << value << "\n";
        outFile << "sw $t0, " << offset << "($sp)\n";
    }
    
    // Return statement
    else if (std::regex_match(line, matches, return_regex)) {
        if (matches[1].matched) {
            std::string var_name = matches[1];
            int offset = symbol_table.get_offset(var_name);
            if (offset == -1) {
                outFile << "# Error: Variable '" << var_name << "' not declared.\n";
                return;
            }
            outFile << "# Return: " << var_name << "\n";
            outFile << "lw $v0, " << offset << "($sp)\n";
        } else {
            outFile << "# Return: void\n";
            outFile << "move $v0, $zero\n";
        }
    }
    
	// Arithmetic operations
	else if (line.find('=') != std::string::npos) {
		size_t eq_pos = line.find('=');
		std::string var_name = line.substr(0, eq_pos);
		var_name.erase(var_name.find_last_not_of(" ")+1); // Trim spaces
		std::string expr = line.substr(eq_pos + 1);
		expr.erase(0, expr.find_first_not_of(" ")); // Trim spaces
		expr.pop_back(); // Remove ending semicolon

		// Process the expression and get the result register
		std::string processed_expr = process_expression(expr, symbol_table, outFile, temp_var_count, stack_offset, free_temps);
		if (processed_expr.empty()) {
		    return;  // If there's an error in processing the expression, return early
		}

		// Get the offset for the variable where the result will be stored
		int offset = symbol_table.get_offset(var_name);
		if (offset == -1) {
		    outFile << "# Error: Variable '" << var_name << "' not declared.\n";
		    return;
		}

		// Store the result of the expression into the variable's location
		outFile << "sw " << processed_expr << ", " << offset << "($sp)   # Store result in " << var_name << "\n";
	}
    
}

int main() {
    std::ifstream input_file("input.c");
    if (!input_file.is_open()) {
        std::cerr << "Error: Could not open file.\n";
        return 1;
    }

    std::ofstream outFile("output.s");
    if (!outFile) {
        std::cerr << "Error opening file for writing!\n";
        return 1;
    }

    // Write the default MIPS setup at the beginning
    outFile << ".text # Default items for MIPS\n";
    outFile << ".globl main\n";
    outFile << "main:\n";
    outFile << "move $fp, $sp\n";
    outFile << "addiu $sp, $sp, -0x100\n";

    int stack_offset = 0, temp_var_count = 0;
    SymbolTable symbol_table;

    for (std::string line; std::getline(input_file, line); ) {
        process_line(line, symbol_table, stack_offset, outFile, temp_var_count, free_temps);
    }
    outFile << "move $a0, $v0 # 将返回值（$v0）复制到$a0，作为打印整数的系统调用的参数\n";
	outFile << "li $v0, 1 # 设置系统调用号为 1，即打印整数\n";
	outFile << "syscall # 系统调用\n";
	outFile << "li $v0, 10 # 设置系统调用号为 10，即退出程序\n";
	outFile << "syscall # 系统调用\n";
	
    // Close the file after processing
    input_file.close();
    outFile.close();
    return 0;
}

