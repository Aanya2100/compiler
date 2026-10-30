const $ = (id) => document.getElementById(id);
let inputMode = "c";

function setMode(mode) {
  inputMode = mode;
  const isC = mode === "c";
  const isTac = mode === "tac";
  $("cTab").classList.toggle("active", isC);
  $("tacTab").classList.toggle("active", isTac);
  $("compareTab").classList.toggle("active", mode === "compare");
  $("cTab").setAttribute("aria-selected", String(isC));
  $("tacTab").setAttribute("aria-selected", String(isTac));
  $("compareTab").setAttribute("aria-selected", String(mode === "compare"));
  $("cInput").classList.toggle("hidden", !isC);
  $("cLabel").classList.toggle("hidden", !isC);
  $("tacInput").classList.toggle("hidden", !isTac);
  $("tacLabel").classList.toggle("hidden", !isTac);
  $("compareInputs").classList.toggle("hidden", mode !== "compare");
  $("run").textContent = mode === "compare" ? "▶ Verify and compare costs" : "▶ Run compiler and optimizer";
  $("frontEndStep").textContent = isC ? "C → TAC" : isTac ? "TAC input" : "Candidate check";
  $("sourceLabel").textContent = isC ? "C front end" : isTac ? "TAC input" : "Candidate comparison";
}

function renderError(message) {
  $("decision").textContent = "Error";
  $("decision").className = "badge error";
  $("generated").textContent = "—";
  $("optimized").textContent = message;
  $("verify").textContent = "—";
  $("verify").className = "";
  $("before").textContent = "—";
  $("after").textContent = "—";
  $("reduce").textContent = "—";
  $("summary").textContent = "No valid result: parsing or processing failed.";
  $("log").textContent = "The request did not complete. Check that the backend is running and that the input uses the supported syntax.";
}

async function checkBackend() {
  try {
    const response = await fetch("/health");
    const health = await response.json();
    if (!health.success) throw new Error("Backend health check failed");
    $("serverStatus").textContent = "Local backend connected";
    $("serverStatus").className = "status online";
  } catch (_) {
    $("serverStatus").textContent = "Backend not running";
    $("serverStatus").className = "status offline";
    $("run").disabled = true;
  }
}

async function runOptimizer() {
  const runButton = $("run");
  runButton.disabled = true;
  runButton.textContent = "Working…";
  $("decision").textContent = "Running";
  $("decision").className = "badge";
  $("log").textContent = "Sending input to the local C optimizer…";
  try {
    if (inputMode === "compare") {
      const source = `${$("originalInput").value}\n--CANDIDATE--\n${$("candidateInput").value}`;
      const response = await fetch("/compare-tac", { method: "POST", headers: { "Content-Type": "text/plain; charset=utf-8" }, body: source });
      const result = await response.json();
      if (!response.ok || !result.success) throw new Error(result.error || `Backend error (${response.status})`);
      $("generated").textContent = $("originalInput").value;
      $("optimized").textContent = $("candidateInput").value;
      $("verify").textContent = result.verification ? "PASS" : "FAIL";
      $("verify").className = result.verification ? "pass" : "fail";
      $("before").textContent = result.original_cost;
      $("after").textContent = result.candidate_cost;
      $("reduce").textContent = `${Number(result.cost_reduction).toFixed(2)}%`;
      $("decision").textContent = result.decision;
      $("decision").className = result.decision === "ACCEPT" ? "badge success" : "badge error";
      $("summary").textContent = `Verification: ${result.verification ? "PASS" : "FAIL"}\nOriginal cost: ${result.original_cost}\nCandidate cost: ${result.candidate_cost}\nCost reduction: ${Number(result.cost_reduction).toFixed(2)}%\nDecision: ${result.decision}`;
      $("log").textContent = `Reason: ${result.reason}\n\nCost = instructions + 2 × arithmetic operations + temporary variables.\nOriginal: ${result.original_instructions} instructions, ${result.original_arithmetic} arithmetic operations, ${result.original_temporaries} temporaries.\nCandidate: ${result.candidate_instructions} instructions, ${result.candidate_arithmetic} arithmetic operations, ${result.candidate_temporaries} temporaries.`;
      return;
    }
    const source = inputMode === "c" ? $("cInput").value : $("tacInput").value;
    const endpointBase = inputMode === "c" ? "/compile-c" : "/optimize";
    const endpoint = endpointBase;
    const response = await fetch(endpoint, {
      method: "POST",
      headers: { "Content-Type": "text/plain; charset=utf-8" },
      body: source
    });
    const result = await response.json();
    if (!response.ok || !result.success) throw new Error(result.error || `Backend error (${response.status})`);

    $("generated").textContent = result.original_tac || "No TAC generated.";
    $("optimized").textContent = result.optimized_tac || "No optimized TAC returned.";
    $("verify").textContent = result.output_match ? "PASS" : "FAIL";
    $("verify").className = result.output_match ? "pass" : "fail";
    $("before").textContent = result.original_cost;
    $("after").textContent = result.optimized_cost;
    $("reduce").textContent = `${Number(result.cost_reduction).toFixed(2)}%`;
    $("summary").textContent = [
      `Iterations: ${result.iterations}`,
      `Proposals: ${result.proposals}`,
      `Verified: ${result.verified}`,
      `Accepted: ${result.accepted}`,
      `Rejected: ${result.rejected}`
    ].join("\n");
    $("log").textContent = `Rule-based optimization ran.\n\nFinal TAC output match: ${result.output_match ? "PASS" : "FAIL"}\nOptimization acceptance rule: verified and lower cost.`;
    $("decision").textContent = result.output_match ? "Verified" : "Check result";
    $("decision").className = result.output_match ? "badge success" : "badge error";
  } catch (error) {
    renderError(error.message || "Unknown request error");
  } finally {
    runButton.disabled = false;
    runButton.textContent = inputMode === "compare" ? "▶ Verify and compare costs" : "▶ Run compiler and optimizer";
  }
}

$("cTab").addEventListener("click", () => setMode("c"));
$("tacTab").addEventListener("click", () => setMode("tac"));
$("compareTab").addEventListener("click", () => setMode("compare"));
$("run").addEventListener("click", runOptimizer);
checkBackend();
