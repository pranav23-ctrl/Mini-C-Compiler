let Module;
let compileLexer, compileAST, compileIR, compileOptimizedIR, compileCodegen;

require.config({ paths: { 'vs': 'https://unpkg.com/monaco-editor/min/vs' } });

require(['vs/editor/editor.main'], function () {
  window.editor = monaco.editor.create(document.getElementById('editor'), {
    value: `// Sample C code\nint main() { return 42; }`,
    language: 'c',
    theme: 'vs-dark',
    automaticLayout: true
  });

  loadWASM();
});

function loadWASM() {
  createClangModule().then(mod => {
    Module = mod; 
    console.log("✅ WASM module loaded");

    // Make these globally accessible
    window.runLexer = Module.cwrap('run_lexer', 'string', ['string']);
    window.runAST = Module.cwrap('run_ast', 'string', ['string']);
    window.runIR = Module.cwrap('run_ir', 'string', ['string']);
    window.runOptimizedIR = Module.cwrap('run_optimized_ir', 'string', ['string']);
    window.runCodegen = Module.cwrap('run_codegen', 'string', ['string']);
    Module.set_user_input = Module.cwrap('set_user_input', null, ['string']);
    const getCode = () => window.editor.getValue();

    // (Optional) keep these if needed elsewhere
    window.compileLexer = () => runStage(runLexer, getCode(), "lexerOutput", "Lexer");
    window.compileAST = () => runStage(runAST, getCode(), "astOutput", "AST");
    window.compileIR = () => runStage(runIR, getCode(), "irOutput", "IR");
    window.compileOptimizedIR = () => {
      const ir = runIR(getCode());
      runStage(() => runOptimizedIR(ir), "", "irOutput", "Optimized IR");
    };
    window.compileCodegen = () => {
      const ir = runIR(getCode());
      const optimized = runOptimizedIR(ir);
      runStage(() => runCodegen(optimized), "", "wasmOutput", "Codegen");
    };
  });
}


function runStage(fn, code, outputId, label) {
  const start = performance.now();
  let result = typeof code === "string" ? fn(code) : fn();
  const time = performance.now() - start;

  document.getElementById(outputId).textContent = result;
  showTab(outputId);
  showStats(label, time, !result.toLowerCase().includes("error"));
}

function showStats(stage, timeMs, success) {
  const log = `${stage} ${success ? "✅" : "❌"} in ${timeMs.toFixed(2)} ms`;
  console.log(log);
}

function toggleTheme() {
  const light = document.body.classList.toggle("light-theme");
  monaco.editor.setTheme(light ? "vs" : "vs-dark");
}

function showTab(id) {
  document.querySelectorAll('.output-tab').forEach(tab => tab.classList.remove('active'));
  document.querySelectorAll('.output-panel').forEach(panel => panel.classList.remove('active'));

  const tab = document.querySelector(`.output-tab[onclick*="${id}"]`);
  const panel = document.getElementById(id);
  if (tab && panel) {
    tab.classList.add('active');
    panel.classList.add('active');
  }
}
function showFullResult() {
  const code = window.editor.getValue();
  const userInput = document.getElementById("userInput").value;
  Module.set_user_input(userInput);
  const start = performance.now();

  const lexer = runLexer(code);
  const ast = runAST(code);
  const ir = runIR(code);
  const optimizedIR = runOptimizedIR(ir);
  const rawResult = runCodegen(optimizedIR);

  const end = performance.now();
  const execDuration = (end - start).toFixed(2);

  document.getElementById("lexerOutput").textContent = lexer;
  document.getElementById("astOutput").textContent = ast;
  document.getElementById("irOutput").textContent = optimizedIR;

 const finalOutput = `${rawResult}\nTotal Compilation Time: ${execDuration} ms`;

  document.getElementById("wasmOutput").textContent = finalOutput;
  showTab("wasmOutput");
}

function toggleBot() {
  document.getElementById("botPanel").classList.toggle("open");
}

async function generateCodeFromPrompt() {
  const prompt = document.getElementById("nlPrompt").value.trim();
  if (!prompt) return alert("Please describe the code you want.");

  const fullPrompt = `Write a valid C function based on the following instruction:\n"${prompt}"\nOnly provide code.`;

  const response = await fetch("https://api.together.xyz/v1/chat/completions", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "Authorization": "Bearer tgp_v1_8lk3sb6ZQ6IjNwWwXMKj-qGCwMWiECxBh9OSgI9pcZU"  // Replace this
    },
    body: JSON.stringify({
      model: "mistralai/Mixtral-8x7B-Instruct-v0.1",
      messages: [
        { role: "system", content: "You are a helpful assistant who writes C code." },
        { role: "user", content: fullPrompt }
      ],
      temperature: 0.3,
      max_tokens: 512,
      top_p: 0.95,
      stop: null
    })
  });

  const data = await response.json();

  const code = data?.choices?.[0]?.message?.content || "// Failed to generate.";
  window.editor.setValue(code);
  toggleBot();
}
function startVoiceInput() {
  if (!('webkitSpeechRecognition' in window)) {
    alert("Voice input not supported.");
    return;
  }

  const recognition = new webkitSpeechRecognition();
  recognition.lang = 'en-US';
  recognition.interimResults = false;
  recognition.maxAlternatives = 1;

  recognition.onstart = () => alert("🎙️ Speak now...");
  recognition.onresult = (event) => {
    document.getElementById("nlPrompt").value = event.results[0][0].transcript;
  };
  recognition.onerror = (event) => alert("Error: " + event.error);

  recognition.start();
}
