#include <iostream>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>

// Symbol Table to manage variable declarations and offsets, only works with int
// TODO: Make it work for all variable sizes. Padding needed?
class SymbolTable {
private:
    int next_offset;  // Tracks the next available memory offset

public:
    std::unordered_map<std::string, int> table;

    SymbolTable() : next_offset(-4) {} // Initialize memory allocation

	// dict addition, assume always int sized (change for future multiple type variations)
    bool add_variable(const std::string& var_name) {
        if (table.find(var_name) != table.end()) {
            return false; // Variable already exists
        }
        table[var_name] = next_offset;
        next_offset -= 4; // Move stack downward (MIPS convention)
        return true;
    }

	// dict hash o(1) speed lookup
    int get_offset(const std::string& var_name) const {
        auto it = table.find(var_name);
        return (it != table.end()) ? it->second : -1;
    }
};

std::regex var_decl_regex(R"(int\s+(\w+)\s*(?:=\s*(\d+))?\s*;)");
std::regex assign_regex(R"((\w+)\s*=\s*(\d+);)");
std::regex op_regex(R"((\w+)\s*=\s*(.+);)");
std::regex return_regex(R"(return\s*(\w+)?\s*;)");
std::stack<int> free_temps;

int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    return 0;
}

bool is_operator(char c) {
    return (c == '+' || c == '-' || c == '*' || c == '/');
}

// Djikstra's Yard-shunt Algorithm (Because fuck parenthesis nesting and operator orders
std::string infix_to_postfix(const std::string& infix_expr) {
    std::stack<char> operator_stack;
    std::string postfix_expr;
    std::string token;
	
	std::cout << "Infix: " << infix_expr << std::endl;
    for (size_t i = 0; i < infix_expr.size(); ++i) {
        char c = infix_expr[i];

        if (std::isalnum(c)) {  // Operand (variable or constant)
            token += c;
            if (i + 1 < infix_expr.size() && std::isalnum(infix_expr[i + 1])) {
                continue;  // Continue building multi-character tokens
            }
            postfix_expr += token + " ";  // Append token with a space
            token.clear();
        } 
        else if (c == '(') {
            operator_stack.push(c);
        } 
        else if (c == ')') {
            while (!operator_stack.empty() && operator_stack.top() != '(') {
                postfix_expr += operator_stack.top();
                postfix_expr += " ";  // Add space for separation
                operator_stack.pop();
            }
            if (!operator_stack.empty() && operator_stack.top() == '(') {
                operator_stack.pop();  // Discard '('
            } else {
                // Mismatched parentheses
                throw std::runtime_error("Mismatched parentheses");
            }
        } 
        else if (is_operator(c)) {  // Operator
            while (!operator_stack.empty() && operator_stack.top() != '(' &&
                   precedence(operator_stack.top()) >= precedence(c)) {
                postfix_expr += operator_stack.top();
                postfix_expr += " ";  // Add space for separation
                operator_stack.pop();
            }
            operator_stack.push(c);
        }
    }

    // Pop any remaining operators from the stack
    while (!operator_stack.empty()) {
        if (operator_stack.top() == '(') {
            throw std::runtime_error("Mismatched parentheses");
        }
        postfix_expr += operator_stack.top();
        postfix_expr += " ";  // Add space for separation
        operator_stack.pop();
    }
	std::cout << "Postfix: " << postfix_expr << std::endl;
    return postfix_expr;
}

// using Djikstra's converted postfix, convert in order into MIPS
std::string convert_postfix_to_mips(const std::string& postfix_expr, SymbolTable& symbol_table, 
                                    std::ostream& outFile, int& temp_var_count) {
    std::istringstream expr_stream(postfix_expr);
    std::string token;
    std::stack<std::string> operand_stack;  // Stack to hold operands (variables or constants)

    while (expr_stream >> token) {
        if (isalnum(token[0])) {  // Operand (variable or constant)
            operand_stack.push(token);  // Push operand onto the stack
        } 
        else if (is_operator(token[0])) {  // Operator
            if (operand_stack.size() < 2) {
                throw std::runtime_error("Invalid postfix expression: Not enough operands for operator " + token);
            }

            // Pop the top two operands from the stack
            std::string operand2 = operand_stack.top();
            operand_stack.pop();
            std::string operand1 = operand_stack.top();
            operand_stack.pop();

            // Load operand1 into $t0
            if (symbol_table.get_offset(operand1) != -1) {
                outFile << "lw $t0, " << symbol_table.get_offset(operand1) << "($fp)\n";
            } else {
                outFile << "li $t0, " << operand1 << "\n";  // Load constant
            }

            // Load operand2 into $t1
            if (symbol_table.get_offset(operand2) != -1) {
                outFile << "lw $t1, " << symbol_table.get_offset(operand2) << "($fp)\n";
            } else {
                outFile << "li $t1, " << operand2 << "\n";  // Load constant
            }

            // Perform the operation and store the result in $t0
            outFile << "# Compute: $t0 = " << operand1 << " " << token << " " << operand2 << "\n";
            if (token == "+") outFile << "add $t0, $t0, $t1\n";
            else if (token == "-") outFile << "sub $t0, $t0, $t1\n";
            else if (token == "*") outFile << "mul $t0, $t0, $t1\n";
            else if (token == "/") {
                outFile << "div $t0, $t1\n";
                outFile << "mflo $t0\n";  // Store quotient in $t0
            }

            // Push the result (in $t0) back onto the stack as a temporary operand
            operand_stack.push("$t0");
        }
    }

    // The final result is in $t0
    if (operand_stack.size() != 1) {
    	outFile << "# Compiler Error: Invalid postfix expression!\n";
        throw std::runtime_error("Invalid postfix expression: Too many operands remaining");
    }

    return operand_stack.top();  // Return the final result (e.g., "$t0")
}

void process_line(const std::string& line, SymbolTable& symbol_table, 
                  std::ofstream& outFile, int& temp_var_count) {
    std::smatch matches;

    // Variable Declaration (e.g., `int a = 0;` OR int a;)
    if (std::regex_match(line, matches, var_decl_regex)) {
        std::string var_name = matches[1];
        int value = matches[2].matched ? std::stoi(matches[2].str()) : 0;

        // Allocate space for the variable in the symbol table
        if (!symbol_table.add_variable(var_name)) {
            outFile << "# Error: Variable '" << var_name << "' already declared.\n";
            return;
        }
		int offset = symbol_table.get_offset(var_name);
		
        // If initialized with a value, store the value
        if (value != 0) {
            outFile << "li $t0, " << value << "\n";
            outFile << "sw $t0, " << offset << "($fp)  # Store " << var_name << " with value\n";
        } else {
            // Zero initialize the variable
        	outFile << "sw $zero, " << offset << "($fp)  # " << var_name << " (int)\n";
		}
    }
    
    // Assignment (e.g., `a = 5;`)
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
        outFile << "sw $t0, " << offset << "($fp)\n";
    }
    
    // Return statement (e.g., `return a;`)
    else if (std::regex_match(line, matches, return_regex)) {
        std::string var_name = matches[1];

        if (!var_name.empty()) {
            int offset = symbol_table.get_offset(var_name);
            if (offset == -1) {
                outFile << "# Error: Variable '" << var_name << "' not declared.\n";
                return;
            }
            outFile << "# Return: " << var_name << "\n";
            outFile << "lw $v0, " << offset << "($fp)\n";
        } else {
            outFile << "# Return: void\n";
            outFile << "move $v0, $zero\n";
        }
    }
    
    // Arithmetic Expressions (e.g., `d = a + b * c;`)
    else if (line.find('=') != std::string::npos) {
        size_t eq_pos = line.find('=');
        std::string var_name = line.substr(0, eq_pos);
        var_name.erase(var_name.find_last_not_of(" ")+1); // Trim spaces

        std::string expr = line.substr(eq_pos + 1);
        expr.erase(0, expr.find_first_not_of(" ")); // Trim spaces
        expr.pop_back(); // Remove semicolon

        // Convert infix to postfix using Dijkstra’s Algorithm
        std::string postfix_expr = infix_to_postfix(expr);
		
        // Convert postfix to MIPS assembly
        std::string result_register = convert_postfix_to_mips(postfix_expr, symbol_table, outFile, temp_var_count);
        
        if (result_register.empty()) {
            outFile << "# Error: Processed expression is empty\n";
            return;
        }

        // Get the offset for the target variable
        int offset = symbol_table.get_offset(var_name);
        if (offset == -1) {
            outFile << "# Error: Variable '" << var_name << "' not declared.\n";
            return;
        }
		
        // Store the result of the expression in the variable
        outFile << "# Store result in " << var_name << "\n" << "sw $t0, " << offset << "($fp)\n";
    }
}


int main(int argc, char* argv[]) {
	// Check if the correct number of arguments is provided
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file.c>" << std::endl;
        return 1;
    }

    // Get the input filename from the command-line arguments
    std::string input_filename = argv[1];

    std::ifstream input_file(input_filename);
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
    outFile << ".text\n";
    outFile << ".globl main\n";
    outFile << "main:\n";
    outFile << "move $fp, $sp\n";
    outFile << "addiu $sp, $sp, -0x100\n";

    int temp_var_count = 0;
    SymbolTable symbol_table;

    for (std::string line; std::getline(input_file, line); ) {
        process_line(line, symbol_table, outFile, temp_var_count);
    }
    outFile << "# Printing Integer\n";
    outFile << "move $a0, $v0\n";
	outFile << "li $v0, 1\n";
	outFile << "syscall\n";
	
	outFile << "# exiting gracefully\n";
	outFile << "li $v0, 10\n";
	outFile << "syscall\n";
	
    // Close the file after processing
    input_file.close();
    outFile.close();
    return 0;
}

