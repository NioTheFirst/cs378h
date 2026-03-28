---
title: Project Report

---

# Project Report

### Brian Zhang (bz5346)


## Motivation

Modern cloud applications are increasingly being implemented using microservice architecture, where functionality is decomposed into many loosely coupled services that communicate via RPC. 
This deployment model has been adapted in many of the biggest such applications, including Twitter, Amazon, and Netflix.

From a program behaviour standpoint, microservices have largely different control flow, instruction footprint, memory access patterns and more compared to traditional programs (i.e. SPEC).
In particular, microservices consist of distributed graphs of interacting components that require frequent communication, synchronization, and data movement~[DeathStarBench, ASPLOS'19].
One such observation is that in SPEC workloads, the time spent in the kernel only accounts of 1~5% of the total program execution time.
In contrast, microservices spend significantly more time in the kernel(from personal experiments).


Prior microservice work has shown how the different behaviour of microservice workloads leads to prior microarchitecture policies being less efficient on them.
An example includes _Whisper:Profile-Guided Branch Misprediction Elimination for Data Center Applications_(2002) which demonstrates that traditional branch prediction heuristics work poorly in data center workloads.

This raises the question of whether similar effects arise for security mechanisms in the kernel.
As microservices spend substantially more time in the kernel, the cost of these defenses is amplified, but it remains unclear whether this overhead scales linearly or exhibits more severe growth.

Hence, we seek to broadly classify which kernel seucirty mechanisms have the most effect on latency in new microservice workloads.
We also seek to develop a new low-overhead mitigation that preserves security guarantees while minimizing disruption to program behavior.

## Background

__Specter V2 attacks (BTI)__
exploit shared branch prediction structures—primarily the Branch Target Buffer (BTB) to misdirect speculative execution. An attacker trains the predictor so that a victim speculatively executes code at an attacker-chosen target. Although the misspeculated execution is later squashed architecturally, it can leave traces in microarchitectural state (e.g., caches), which can be probed to leak sensitive data via side channels.

__Retpoline__
is a compiler-based mitigation that replaces indirect branches with a “trampoline” sequence. This sequence traps speculative execution in a benign loop (e.g., pause; jmp $-2) and uses a return instruction to reach the true target only after it is resolved. By avoiding BTB-based target prediction for indirect branches, retpoline prevents attacker-controlled redirection, but introduces additional control-flow overhead, loses performance wins from the BTB, and can increase branch mispredictions.

__IBRS(Indirect Branch Restricted Speculation)/eIBRS__ are hardware-based mitigations that restrict how branch predictor state is shared across privilege levels. When enabled, they prevent lower-privilege code from influencing predictions used by higher-privilege code (e.g., user -> kernel). While effective, these mechanisms can introduce latency due to restricted predictor reuse and frequent kernel transitions, especially in workloads with heavy system call activity.

__gem5__ is a cycle-accurate microarchitectural simulator widely used for computer architecture research. It provides detailed models of CPU pipelines, caches, and branch predictors, enabling fine-grained analysis of hardware behavior. In this work, gem5 is used to study branch prediction mechanisms (e.g., BTB and IBP), measure misprediction rates, and evaluate new mitigation designs that are not directly observable on real hardware.

## Security/Performance Concerns

Spectre v2 (Branch Target Injection) was publicly disclosed in January 2018 by researchers from Google Project Zero, including Jann Horn, alongside academic collaborators.

The vulnerability exploits the fact that modern CPUs share branch prediction structures (i.e. BTB), across execution contexts. 
An attacker can poison these predictors so that a victim speculatively executes instructions at an attacker-controlled target,
resulting in leftover microarchitectural traces (which then can be exploited).

Subsequent work has demonstrated that this class of attacks extends beyond simple BTB poisoning. 
Variants such as Branch History Injection (BHI) show that indirect branch prediction structures (IBP) can also be manipulated.

To mitigate Spectre v2, modern processors deploy protections such as IBRS and retpoline.
However, such protections are expensive.
For example, a prior work, _A Systematic Evaluation of Transient Execution Attacks and Defenses_, studies performance impacts on such protections, and finds that for IBRS, performance degradation suffers 20-30% performance penalty.

## Approach/Progress

To study the impact of kernel protections on microservice workloads, we first collect a set of workloads from DeathStarBench, DCPerf, and CloudSuite.
We then configure multiple Linux kernel variants corresponding to individual Spectre-related security mechanisms. The goal is to isolate each mitigation as much as possible, allowing us to attribute performance overhead to specific protections.

The evaluated kernel configurations are:

- Vuln: Baseline kernel with mitigations disabled
- Retp: Retpoline-only enabled
- IBRS: IBRS/eIBRS-only enabled
- FullSet: All mitigations enabled (default secure configuration)
- PTI: Page Table Isolation enabled
- RSB: Return Stack Buffer protections enabled
- Data_sampling + IBRS: Combined data sampling mitigations with IBRS
- IBPB_user: IBPB enabled on user/kernel transitions

The metric measured is the per-benchmark reported latencies, as well as MPKI, performance counters from perf.

We observe that in fact, __IBRS latency is almost as much as the base latency__, where base corresponds to a default kernel with all the protections turned on.

To study the impact in more detail, we load up 2 microservice workloads (only 2 due to difficulty) into gem5, a microarchitectural simulator, to study in depth the reason behind the latency drops.
On analysis of MPKI, we find that the MPKI is extremely large, around 9.8.
For reference, the typical MPKI for traditional workloads is around 3.0.
Upon analyzing the source of mispredictions, we find that it is due to another one of the kernel security mechanisms, namely retpoline.

These observations taken together suggest that due to the disruption of branch prediction behaviour caused by IBRS (prevent lower-higher branch interaction) and retpoline (new control flow, no BTB usage) seem to be a key cause of mispredictions and performance in microservice workloads.

If we can design a single mitigation mechanism to address these weaknesses while providing necessary security, microservice performance can improve.

We propose a context-keyed predictor isolation scheme that targets both the Branch Target Buffer (BTB) and the Indirect Branch Predictor (IBP).

The key idea is to incorporate a per-process secret K_p
into the predictor indexing and tagging functions. 
For indirect branches, the program counter (PC) is transformed using a lightweight operation (e.g., XOR with 
Kp) before accessing the predictor. 
This ensures that predictor entries trained by one process do not alias with those of another, effectively preventing cross-domain interference (to a probabilistic degree).

The design extends naturally to the IBP by incorporating the same per-process key into history-based indexing (e.g., combining PC′
, global history, and path history). This ensures that both BTB and IBP structures are protected.



## Future Work

Future work includes more in-depth analysis of code and prior techniques, collection of missing data (primarily IBRS-related) in real Hardware, implementation of Samsung Exynos protections, and developing clear hardware costs.

## Related Work

__(XOR-based BTB partitioning)[!https://ieeexplore-ieee-org.ezproxy.lib.utexas.edu/stamp/stamp.jsp?tp=&arnumber=9586178]__
 proposes XOR-based transformations of the PC to reduce aliasing in BTB structures. While effective for BTB isolation, these approaches do not extend to indirect predictors (IBP), leaving part of the attack surface exposed.

__(Samsung Exynos)[!https://people.engr.tamu.edu/djimenez/pdfs/exynos_isca2020.pdf]
proposes encrypting BTB entries in key-and-lock fashion.
Again, these approaches do not extend to indirect predictors (IBP).

__HyBP)[!https://ieeexplore.ieee.org/document/9773264]
proposes protection of the BTB through physical separation.



