# Browser frontend

The page talks to the native C backend; it is not a standalone JavaScript optimizer.

From the project root:

```bash
make run-server
```

Open <http://localhost:8081>. Select **C source** to lower supported C code into TAC, or **TAC** to optimize three-address code directly. The backend must be running; opening `index.html` as a local file will not connect to the optimizer.

Optimization uses the local rule-based proposal specialist by default. The backend can use the optional OpenAI LLM proposal agent when `OPENAI_API_KEY` and `OPTIMIZER_AGENT=llm` are set in its environment. All proposals still pass independent verification and strict cost evaluation.
