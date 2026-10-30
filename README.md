# Browser frontend

The page talks to the native C backend; it is not a standalone JavaScript optimizer.

From the project root:

```bash
make run-server
```

Open <http://localhost:8081>. Select **C source** to lower supported C code into TAC, or **TAC** to optimize three-address code directly. The backend must be running; opening `index.html` as a local file will not connect to the optimizer.

Optimization uses the local rule-based proposal specialist by default. The backend can use the optional OpenAI LLM proposal agent when `OPENAI_API_KEY` and `OPTIMIZER_AGENT=llm` are set in its environment. All proposals still pass independent verification and strict cost evaluation.
<img width="1024" height="614" alt="image" src="https://github.com/user-attachments/assets/732a8787-014c-461a-95e2-0d3a6d3de77a" />
<img width="1024" height="561" alt="image" src="https://github.com/user-attachments/assets/12b61c75-f61d-4886-9137-20726556ca26" />
<img width="1024" height="612" alt="image" src="https://github.com/user-attachments/assets/b81af9a1-e0e6-430e-877a-cac1310d0812" />
<img width="1024" height="620" alt="image" src="https://github.com/user-attachments/assets/b2237c4c-2098-450a-9eb0-477cc80dca92" />
