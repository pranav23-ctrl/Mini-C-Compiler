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
    std::map<std::string, std::string> keywords = {{"int", "KEYWORD"}, {"return", "KEYWORD"}};

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
    if (!check("IDENTIFIER")) return nullptr;
    std::string varName = advance().value;

    ASTNode* varDecl = new ASTNode("VarDecl", varName);
    varDecl->children.push_back(new ASTNode("Type", "int"));

    if (match("=")) {
        ASTNode* expr = parseExpression();
        if (expr) varDecl->children.push_back(expr);
    }

    consume(";");
    return varDecl;
}

ASTNode* parseStatement() {
    if (match("int")) {
        // Variable declaration
        if (check("Identifier")) {
            std::string varName = advance().value;
            if (globalSymbolTable.count(varName)) {
                semanticErrors.push_back("Variable '" + varName + "' re-declared.");
            } else {
                globalSymbolTable[varName] = "int";
            }
            consume(";");
            ASTNode* varDecl = new ASTNode("VarDecl", varName);
            varDecl->children.push_back(new ASTNode("Type", "int"));
            return varDecl;
        } else {
            semanticErrors.push_back("Expected variable name after 'int'.");
            return nullptr;
        }
    } else if (check("Identifier")) {
        // Assignment
        std::string varName = advance().value;
        if (!globalSymbolTable.count(varName)) {
            semanticErrors.push_back("Undeclared variable: " + varName);
        }
        if (match("=")) {
            ASTNode* expr = parseExpression();
            consume(";");
            ASTNode* assign = new ASTNode("Assignment", varName);
            assign->children.push_back(expr);
            return assign;
        } else {
            semanticErrors.push_back("Expected '=' after identifier.");
            return nullptr;
        }
    } else if (match("return")) {
        // Return statement
        ASTNode* expr = parseExpression();
        consume(";");
        ASTNode* ret = new ASTNode("Return");
        ret->children.push_back(expr);
        return ret;
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
    for (auto* child : root->children) {
        if (child->type == "Function") {
            std::string fname = child->value;
            ir << "define i32 @" << fname << "() {\n";
            ASTNode* block = child->children[1];
            for (auto* stmt : block->children) {
                if (stmt->type == "Return") {
                    ASTNode* val = stmt->children[0];
                    if (val->type == "Call") {
                        ir << "  %1 = call i32 @" << val->value << "(";
                        for (size_t i = 0; i < val->children.size(); ++i) {
                            if (i > 0) ir << ", ";
                            ir << "i32 " << val->children[i]->value;
                        }
                        ir << ")\n  ret i32 %1\n";
                    } else if (val->type == "Literal") {
                        ir << "  ret i32 " << val->value << "\n";
                    }
                }
            }
            ir << "}\n";
        }
    }
    return ir.str();
}

// -------------------- Exports --------------------
extern "C" {
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

    std::string input(ir);
    std::smatch match;

    // 1. Direct return: ret i32 42
    std::regex directRet(R"(ret i32 (\d+))");
    if (std::regex_search(input, match, directRet)) {
        result = "Execution result: " + match[1].str();
        return result.c_str();
    }

    // 2. Function call: call i32 @func(i32 X, i32 Y)
    std::regex callPattern(R"(call i32 @(\w+)\(i32 (\d+), i32 (\d+)\))");
    if (std::regex_search(input, match, callPattern)) {
        std::string func = match[1].str();
        int a = std::stoi(match[2].str());
        int b = std::stoi(match[3].str());

        if (func == "add") result = "Execution result: " + std::to_string(a + b);
        else if (func == "sub") result = "Execution result: " + std::to_string(a - b);
        else if (func == "mul") result = "Execution result: " + std::to_string(a * b);
        else if (func == "div" || func == "divide") result = "Execution result: " + std::to_string(a / b);
        else result = "Execution error: unsupported function '" + func + "'";
        return result.c_str();
    }

    result = "Execution error: no recognizable return.";
    return result.c_str();
}


}
