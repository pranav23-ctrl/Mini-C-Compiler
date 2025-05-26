🛠️ Mini C Compiler (Web-Based)
A lightweight, interactive Mini C Compiler built using C++ (compiled to WebAssembly via Emscripten) and a modern JavaScript/HTML UI. Designed to simulate the core phases of a C compiler — from tokenization to execution — all in the browser with a focus on learning, experimentation, and user-friendly interaction.

🚀 Features
🧠 Compiler Frontend:
Lexical Analysis (Lexer): Tokenizes input C code into KEYWORD, IDENTIFIER, INTEGER, etc.

Syntax Tree Construction (AST): Parses statements like int x;, scanf(...), printf(...), and return x;

Semantic Analysis: Reports errors like undeclared variables or incorrect function calls.

⚙️ Intermediate Execution (Simulated):
🖨️ printf() Support: Interprets calls like printf("Value:", x) and prints results to output.

🎤 scanf() Support: Allows simulated input via a user-provided field (e.g., enter value 5 for scanf).

🔁 Return Value Tracking: Returns expressions like return x; and logs the value.

📊 Static Analysis: Execution time, memory usage, and complexity estimations are shown per run.

🧾 Input Example
c
Copy code
int main() {
  int x;
  scanf("%d", &x);
  printf("Value is", x);
  return x;
}
✅ Output Simulation
csharp
Copy code
[scanf] x = 5
[printf] Value is = 5
[return] Execution result: 5
--- Static Stats ---
Time Complexity: O(1)
Space Complexity: O(n)
Memory Used: 64 bytes
🖥️ Technologies Used
Layer	Tech
Frontend	HTML, CSS, JavaScript, Monaco Editor
Backend Compiler	C++
Compilation	Emscripten (C++ → WebAssembly)
AI Assistant (Optional)	Together AI API (for NL → C Code)
Voice Input	Web Speech API

📂 Project Structure
cpp
Copy code
📁 index.html         // Main UI + Panels
📁 script.js          // JS logic for editor, wasm, voice, tabs
📁 web_driver.cpp     // C++ compiler logic: lexer, ast, codegen
📁 clang-llvm-lld.js  // WebAssembly output (compiled C++)
🧪 How It Works
You write or paste C code into the Monaco Editor.

Click “Show Result” to:

Tokenize → Parse → Simulate execution

Outputs for Lexer, AST, IR, and Final Execution are shown in separate tabs.

If the code includes scanf(), provide input in the input field.

printf() and return values are simulated and displayed with complexity info.

🗂️ Use Cases
🧑‍🎓 Educational Tool for learning C compilation internals

⚙️ Compiler Design Demos (Lexer, AST, IR, Codegen)

🌐 Web-based IDE for simple C snippets

🧪 Lightweight testbed for language feature simulation

✅ Todo / Future Enhancements
 Support conditionals (if, else)

 Arithmetic expressions (e.g., x + y)

 Loops (for, while)

 Scope tracking and nested blocks

 AST visualization (graphical)

 Export output to .txt or .json

📦 Setup Instructions
🛠️ Local Dev with Emscripten
Install Emscripten

Compile the backend:

bash
Copy code
emcc web_driver.cpp -o clang-llvm-lld.js \
-sEXPORTED_FUNCTIONS="['_run_lexer','_run_ast','_run_ir','_run_codegen','_set_user_input']" \
-sEXPORTED_RUNTIME_METHODS=ccall,cwrap -O2
Open index.html in a browser or run via a local server:

bash
Copy code
npx http-server .
🤖 Bonus: AI Assistant
Use the 🤖 button to describe logic in natural language

The bot returns valid C code using Together.ai or OpenAI

Optionally, speak your logic using 🎤 voice input!

📝 License
This project is open-source and free to use under the MIT License.
