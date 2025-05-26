#include <string>
#include <sstream>
#include <vector>
#include <regex>
#include <map>
#include <iostream>
#include <emscripten.h>
#include <cstdio>
#include <unordered_map>

std::unordered_map<std::string, std::string> globalSymbolTable;
std::vector<std::string> semanticErrors;
    std::string userInput;

std::string result;
std::map<std::string, std::string> namedValues;
// -------------------- Lexer --------------------
struct Token {
    std::string type, value;
};

std::string removeComments(const std::string& code) {
    std::string result;
    bool in_single = false, in_multi = false;

    for (size_t i = 0; i < code.size(); ++i) {
        if (in_single && code[i] == '\n') { in_single = false; result += '\n'; continue; }
        if (in_multi && code[i] == '*' && code[i+1] == '/') { in_multi = false; ++i; continue; }

        if (!in_single && !in_multi) {
            if (code[i] == '/' && code[i+1] == '/') { in_single = true; ++i; continue; }
            if (code[i] == '/' && code[i+1] == '*') { in_multi = true; ++i; continue; }
            result += code[i];
        }
    }
    return result;
}

std::vector<Token> tokenizeStructured(const std::string& input) {
    std::string clean = removeComments(input);
    std::vector<Token> tokens;
    std::regex pattern(R"(\s+|==|!=|<=|>=|[+\-*/=<>(){};]|[0-9]+|[a-zA-Z_][a-zA-Z0-9_]*)");
    std::map<std::string, std::string> keywords = {
        {"int", "KEYWORD"}, {"return", "KEYWORD"},
        {"if", "KEYWORD"}, {"else", "KEYWORD"},
        {"printf", "IDENTIFIER"}, {"scanf", "IDENTIFIER"}};

    for (auto it = std::sregex_iterator(clean.begin(), clean.end(), pattern); it != std::sregex_iterator(); ++it) {
        std::string tok = (*it).str();
        if (std::regex_match(tok, std::regex(R"(\s+)"))) continue;

        std::string type = (keywords.count(tok)) ? keywords[tok]
                          : (std::regex_match(tok, std::regex(R"([0-9]+)"))) ? "INTEGER"
                          : (std::regex_match(tok, std::regex(R"([a-zA-Z_][a-zA-Z0-9_]*)"))) ? "IDENTIFIER"
                          : "SYMBOL";
        tokens.push_back({type, tok});
    }
    return tokens;
}

std::string serializeTokens(const std::vector<Token>& tokens) {
    std::stringstream ss;
    for (const auto& t : tokens)
        ss << "TOKEN(" << t.type << ", \"" << t.value << "\")\n";
    return ss.str();
}

// -------------------- AST --------------------
struct ASTNode {
    std::string type;
    std::string value;
    std::vector<ASTNode*> children;

    // Optional metadata for semantic analysis
    std::string inferredType;
    bool isDeclared = false;

    ASTNode(std::string t, std::string v = "") : type(t), value(v) {}
    ~ASTNode() {
        for (auto* child : children) delete child;
    }
};

size_t current = 0;
std::vector<Token> tokens;

ASTNode* parsePrimary() {
    if (tokens[current].type == "INTEGER") return new ASTNode("Literal", tokens[current++].value);
    if (tokens[current].type == "IDENTIFIER") {
        std::string id = tokens[current++].value;
        if (tokens[current].value == "(") {
            current++;
            ASTNode* call = new ASTNode("Call", id);
            while (tokens[current].value != ")") {
                call->children.push_back(parsePrimary());
                if (tokens[current].value == ",") current++;
            }
            current++; // skip ')'
            return call;
        }
        return new ASTNode("Identifier", id);
    }
    return nullptr;
}
ASTNode* parseExpression() {
    ASTNode* left = parsePrimary();
    if (!left) return nullptr;

    while (current < tokens.size() &&
           (tokens[current].value == "+" || tokens[current].value == "-" ||
            tokens[current].value == "*" || tokens[current].value == "/")) {
        std::string op = tokens[current++].value;
        ASTNode* right = parsePrimary();
        ASTNode* binary = new ASTNode("BinaryOp", op);
        binary->children.push_back(left);
        binary->children.push_back(right);
        left = binary;
    }

    return left;
}Token peek() {
    if (current < tokens.size()) return tokens[current];
    return {"EOF", ""};
}

Token advance() {
    if (current < tokens.size()) return tokens[current++];
    return {"EOF", ""};
}

bool check(const std::string& type) {
    return peek().type == type || peek().value == type;
}

bool match(const std::string& expected) {
    if (check(expected)) {
        advance();
        return true;
    }
    return false;
}

bool consume(const std::string& expected) {
    if (match(expected)) return true;
    semanticErrors.push_back("Expected '" + expected + "' but got '" + peek().value + "'");
    return false;
}
ASTNode* parseVarDecl() {
    if (!match("int")) return nullptr;
    Token varToken = advance();
    if (varToken.type != "IDENTIFIER") return nullptr;

    std::string varName = varToken.value;
    globalSymbolTable[varName] = "0"; // default init

    ASTNode* decl = new ASTNode("VarDecl", varName);
    if (match("=")) {
        ASTNode* expr = parseExpression();
        if (expr) decl->children.push_back(expr);
    }
    match(";");
    return decl;
}


ASTNode* parseStatement() {
    if (peek().value == "int") return parseVarDecl();
    if (match("return")) {
        ASTNode* node = new ASTNode("Return");
        node->children.push_back(parseExpression());
        match(";");
        return node;
    }
    if (peek().type == "IDENTIFIER") {
        std::string id = advance().value;
        if (match("(")) {
            ASTNode* call = new ASTNode("Call", id);
            while (!match(")") && current < tokens.size()) {
                call->children.push_back(parseExpression());
                if (!match(",")) break;
            }
            match(";");
            return call;
        }
    }
    return nullptr;
}

ASTNode* parseReturn() {
    current++; // skip 'return'
    ASTNode* ret = new ASTNode("Return");
    ASTNode* expr = parseExpression();
    if (expr) ret->children.push_back(expr);
    if (tokens[current].value == ";") current++;
    return ret;
}


ASTNode* parseBlock() {
    current++; // skip {
    ASTNode* block = new ASTNode("Block");
    while (tokens[current].value != "}") {
        if (tokens[current].value == "return") block->children.push_back(parseReturn());
        else current++;
    }
    current++; // skip }
    return block;
}

ASTNode* parseFunction() {
    if (!match("int")) return nullptr;

    std::string funcName = advance().value;
    consume("(");

    ASTNode* func = new ASTNode("Function", funcName);
    ASTNode* params = new ASTNode("Params");

    // Save parameters to symbol table
    while (!check(")")) {
        if (match("int")) {
            if (check("IDENTIFIER")) {
                std::string paramName = advance().value;
                globalSymbolTable[paramName] = "int";  // ✅ Register param
                params->children.push_back(new ASTNode("Param", paramName));
            }
            if (!check(")")) match(",");
        } else {
            semanticErrors.push_back("Expected parameter type 'int'");
            break;
        }
    }
    consume(")");

    func->children.push_back(params);
    func->children.push_back(parseBlock());
    return func;
}

void analyzeSemantics(ASTNode* node) {
    if (!node) return;

    if (node->type == "VarDecl") {
        std::string varName = node->value;
        if (globalSymbolTable.count(varName)) {
            semanticErrors.push_back("Variable '" + varName + "' re-declared.");
        } else {
            globalSymbolTable[varName] = "int";  // assume type int for now
        }
    }

    if (node->type == "Identifier") {
        if (!globalSymbolTable.count(node->value)) {
            semanticErrors.push_back("Undeclared variable: " + node->value);
        }
    }

    if (node->type == "Call") {
        std::string funcName = node->value;
        if (funcName != "add" && funcName != "main") {  // basic example
            semanticErrors.push_back("Function not defined: " + funcName);
        }
    }

    // Recursively analyze children
    for (ASTNode* child : node->children) {
        analyzeSemantics(child);
    }
}

std::string printASTTree(ASTNode* node, int indent = 0) {
    std::string tabs(indent * 2, ' ');
    std::string res = tabs + node->type;
    if (!node->value.empty()) res += "(" + node->value + ")";
    res += "\n";
    for (auto* c : node->children) {
        res += printASTTree(c, indent + 1);
    }
    return res;
}

std::string generateAST(const std::string& input) {
    tokens = tokenizeStructured(input);
    current = 0;
    semanticErrors.clear();
    globalSymbolTable.clear();

    ASTNode* root = new ASTNode("ROOT");

    while (current < tokens.size()) {
        ASTNode* node = parseFunction();
        if (!node) node = parseStatement();
        if (!node) node = parseVarDecl();
        if (node) root->children.push_back(node);
        else current++;
    }

    // Perform semantic analysis
    analyzeSemantics(root);

    std::stringstream ss;
    ss << printASTTree(root);
    if (!semanticErrors.empty()) {
        ss << "\n--- Semantic Errors ---\n";
        for (const std::string& err : semanticErrors) {
            ss << "❌ " << err << "\n";
        }
    } else {
        ss << "\n✅ Semantic analysis passed.\n";
    }

    delete root;
    return ss.str();
}
std::string generateIR(ASTNode* root) {
    std::stringstream ir;
    ir << "define i32 @main() {\n";
    for (auto* child : root->children) {
        if (child->type == "Function") {
            ASTNode* block = child->children[0];
            for (auto* stmt : block->children) {
                if (stmt->type == "Call") {
                    if (stmt->value == "scanf") {
                        std::string var = stmt->children[1]->value;
                        ir << "  call i32 @scanf(\"%d\", i32 *" << var << ")\n";
                    } else if (stmt->value == "printf") {
                        std::string format = stmt->children[0]->value;
                        std::string var = stmt->children[1]->value;
                        ir << "  call i32 @printf(\"" << format << "\", i32 " << var << ")\n";
                    }
                }
                else if (stmt->type == "VarDecl") {
    std::string var = stmt->value;
    globalSymbolTable[var] = "0";  // Initialize to 0
}

                else if (stmt->type == "Return") {
    ASTNode* val = stmt->children[0];
    std::string returnVal = globalSymbolTable.count(val->value) ? globalSymbolTable[val->value] : val->value;
    ir << "  ret i32 " << returnVal << "\n";
}
            }
        }
    }
    ir << "}\n";
    return ir.str();
}
// -------------------- Exports --------------------
extern "C" {
    
    EMSCRIPTEN_KEEPALIVE
    void set_user_input(const char* input) {
    userInput = std::string(input);
    }

    EMSCRIPTEN_KEEPALIVE
    const char* run_lexer(const char* input) {
        static std::string result;
        result = serializeTokens(tokenizeStructured(std::string(input)));
        return result.c_str();
    }

    EMSCRIPTEN_KEEPALIVE
   const char* run_ast(const char* input) {
    static std::string result;
    globalSymbolTable.clear();
    semanticErrors.clear();

    result = generateAST(input);
    if (!semanticErrors.empty()) {
        result += "\nSemantic Errors:\n";
        for (auto& err : semanticErrors)
            result += "  - " + err + "\n";
    }
    return result.c_str();
}


   EMSCRIPTEN_KEEPALIVE
const char* run_ir(const char* input) {
    static std::string result;
    tokens = tokenizeStructured(input);
    current = 0;
    ASTNode* root = new ASTNode("ROOT");
    while (current < tokens.size()) {
        auto* fn = parseFunction();
        if (fn) root->children.push_back(fn);
        else current++;
    }
    result = generateIR(root);
    delete root;
    return result.c_str();
}


   EMSCRIPTEN_KEEPALIVE
const char* run_optimized_ir(const char* inputIR) {
    static std::string optimized;

    std::string ir(inputIR);

    // Very simple optimization: Replace "ret i32 40 + 2" with "ret i32 42"
    std::regex addConstRegex(R"(ret i32 (\d+)\s*\+\s*(\d+))");
    std::smatch match;
    if (std::regex_search(ir, match, addConstRegex)) {
        int a = std::stoi(match[1].str());
        int b = std::stoi(match[2].str());
        int sum = a + b;
        ir = std::regex_replace(ir, addConstRegex, "ret i32 " + std::to_string(sum));
    }

    optimized = "; Optimized IR\n" + ir;
    return optimized.c_str();
}


EMSCRIPTEN_KEEPALIVE
const char* run_codegen(const char* ir) {
    static std::string result;
    double start = emscripten_get_now();

    std::string input(ir);
    std::stringstream output;
    std::smatch match;

    // Simulate scanf
    std::regex scanfRegex(R"(call i32 @scanf\("%d", i32 \*(\w+)\))");
    if (std::regex_search(input, match, scanfRegex)) {
        std::string var = match[1];
        globalSymbolTable[var] = userInput;
        output << "[scanf] " << var << " = " << userInput << "\n";
    }

    // Simulate printf
    std::regex printfRegex("call i32 @printf\\(\"([^\"]+)\", i32 (\\w+)\\)");
    if (std::regex_search(input, match, printfRegex)) {
        std::string format = match[1];
        std::string var = match[2];
        std::string val = globalSymbolTable.count(var) ? globalSymbolTable[var] : "undefined";
        output << "[printf] " << format << " = " << val << "\n";
    }
    std::regex returnRegex(R"(ret i32 (\d+))");
    if (std::regex_search(input, match, returnRegex)) {
        output << "[return] Execution result: " << match[1] << "\n";
    }

    double end = emscripten_get_now();
    double duration = end - start;

    output << "--- Static Stats ---\n";
    output << "Execution Time: " << duration << " ms\n";
    output << "Time Complexity: O(1)\n";
    output << "Space Complexity: O(n)\n";
    output << "Memory Used: 64 bytes\n";

    result = output.str();
    return result.c_str();
}

}
