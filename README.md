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
<img width="1024" height="666" alt="image" src="https://github.com/user-attachments/assets/2ebaec9f-60e7-49ea-9967-634e365c3bad" />


Project Description
This project automatically optimizes Three Address Code (TAC) generated from supported C programs. It analyzes the code for optimization opportunities, then proposes changes using either built-in rules or an optional LLM agent.

Each proposed change is checked by running the original and optimized TAC through an interpreter on multiple inputs. A change is accepted only if both versions produce matching outputs and the optimized code has a lower cost.

The project includes a dataset of 600 TAC programs covering arithmetic, nested expressions, repeated expressions, dead code, loops, conditionals, and mixed cases. It also records optimization decisions, verification results, and cost changes.

The C-to-TAC frontend supports integer expressions, assignments, scalar parameters, if/else, while, and for loops within its supported subset. The LLM agent is optional and requires an API key, network access, and curl.

  
