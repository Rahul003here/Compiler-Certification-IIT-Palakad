# Compiler-Certification-IIT-Palakad
# LLVM Local Optimization Passes

A small learning project that implements five classic compiler optimizations as
a single LLVM function pass. The pass is registered under the existing
`helloworld` pass name, so it runs through `opt -passes=helloworld`.

Passes implemented in `HelloWorld.cpp`:

| Function | What it does |
| --- | --- |
| `rahul_constant_propagation` | Folds any instruction whose operands are all constants |
| `rahul_inst_combine` | Algebraic identities (`x+0`, `x*1`, `x-x`, `x/x`, `x*0`, `x&0`) and power-of-two rewrites |
| `rahul_dead_code_elimination` | Removes instructions whose results are never used |
| `rahul_strength_reduction` | `mul`/`sdiv`/`udiv` by a power of two becomes `shl`/`ashr`/`lshr` |
| `rahul_cse` | Reuses an earlier identical computation inside a basic block |

---

## 1. Copy `HelloWorld.cpp` into the LLVM tree

This is the most important step. The file must replace the stock LLVM file at
exactly this path:

```text
llvm-project/llvm/lib/Transforms/Utils/HelloWorld.cpp
```

```bash
cp HelloWorld.cpp llvm-project/llvm/lib/Transforms/Utils/HelloWorld.cpp
```

Verify it landed correctly:

```bash
grep -c "rahul_" llvm-project/llvm/lib/Transforms/Utils/HelloWorld.cpp
```

This should print a number greater than zero. If it prints `0`, the copy went
to the wrong place.

### Files you do NOT need to modify

You do not need to touch any of these. LLVM already has the wiring in place:

| File |
| --- |
| `llvm/include/llvm/Transforms/Utils/HelloWorld.h` |
| `llvm/lib/Passes/PassRegistry.def` |
| `llvm/lib/Transforms/Utils/CMakeLists.txt` |

That registry line that makes the command line find
the C++ class `HelloWorldPass` is `-passes=helloworld`.

---

## 2. Set up the test-cases directory

Create the test directory **next to** `llvm-project`, not inside it. Keeping
your files out of the LLVM tree avoids confusion

```bash
cd <path to llvm-project> && cd ..
mkdir test-cases
cd test-cases
```

Copy all the `.c` files from this project into it. You should end up with:

```text
test-cases/
├── t1_strength.c
├── t2_algebraic.c
├── t3_copyprop.c
├── t4_constfold.c
├── t5_redundant.c
└── t6_cse.c
```

Check:

```bash
ls *.c
```

Your final layout looks like this:

```text
$HOME/llvm-work/
├── llvm-project/
│   ├── llvm/lib/Transforms/Utils/HelloWorld.cpp   <-- the pass you copied
└── test-cases/
    └── *.c                                        <-- your test programs
```

---

## 3. Running a test case

Every test is two commands. Set a shortcut variable first so the commands stay
short:

```bash
cd <path to test-cases dir>

export B=<path to llvm-project>/build/bin
```

You must re-run that `export` line every time you open a new terminal.

### Step 1: C source to unoptimized LLVM IR

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t4_constfold.c -o t4.ll
```

Look at the result:

```bash
cat t4.ll
```

### Step 2: Run the optimization passes

```bash
$B/opt -S -passes='mem2reg,helloworld' t4.ll -o t4.out.ll
```

What this means:

Compare before and after:

```bash
diff t4.ll t4.out.ll
```

Or just show the function body:

```bash
sed -n '/^define/,/^}/p' t4.out.ll
```

---

## 3. All test cases with expected results

Run each block from `$HOME/llvm-work/test-cases` with `$B` already exported.

### Test 1: Strength reduction

Source: `t1_strength.c`

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t1_strength.c -o t1.ll
$B/opt -S -passes='mem2reg,helloworld' t1.ll -o t1.out.ll
```

Look for these lines in the log:

```text
[rahul_inst_combine]   %2 = mul nsw i32 2, ...  ==>  shl i32 ..., 1
[rahul_inst_combine]   %3 = mul nsw i32 ..., 8  ==>  shl i32 ..., 3
```

Multiplying by 2 becomes a left shift by 1. Multiplying by 8 becomes a left
shift by 3, because $8 = 2^3$.

### Test 2: Algebraic identities

Source: `t2_algebraic.c`

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t2_algebraic.c -o t2.ll
$B/opt -S -passes='mem2reg,helloworld' t2.ll -o t2.out.ll
sed -n '/^define/,/^}/p' t2.out.ll
```

Expected final IR:

```llvm
define dso_local i32 @compute(i32 noundef %0, i32 noundef %1) {
  ret i32 23
}
```

The chain is `a/a` → `1`, `1*1` → `1`, `b-b` → `0`, `1+0` → `1`, `1/1` → `1`,
`1-1` → `0`, then `0+23` → `23`. The whole function collapses to a constant.

### Test 3: Copy propagation

Source: `t3_copyprop.c`

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t3_copyprop.c -o t3.ll
$B/opt -S -passes='mem2reg,helloworld' t3.ll -o t3.out.ll
sed -n '/^define/,/^}/p' t3.out.ll
```

`mem2reg` does the actual copy propagation here by turning `c = d` into a
direct SSA reference, and then this pass folds what is left.

### Test 4: Constant folding

Source: `t4_constfold.c`

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t4_constfold.c -o t4.ll
$B/opt -S -passes='mem2reg,helloworld' t4.ll -o t4.out.ll
sed -n '/^define/,/^}/p' t4.out.ll
```

Expected final IR:

```llvm
define dso_local i32 @compute() {
  ret i32 22
}
```

Trace it: `c = 4+2+3 = 9`, `result = 0+2+3 = 5`, `5*9 = 45`, and `45/2 = 22`
using integer division. Note the divide by 2 goes through `ashr` first, then
gets folded, so you will see both a strength reduction line and a constant
propagation line in the log.

### Test 5: Redundant assignment elimination

Source: `t5_redundant.c`

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t5_redundant.c -o t5.ll
$B/opt -S -passes='mem2reg,helloworld' t5.ll -o t5.out.ll
sed -n '/^define/,/^}/p' t5.out.ll
```

Expected final IR:

```llvm
define dso_local i32 @main() {
  ret i32 3
}
```

`c = a+b` is computed but `c` is never returned, so dead code elimination
deletes it.

### Test 6: Common subexpression elimination

Source: `t6_cse.c`

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t6_cse.c -o t6.ll
$B/opt -S -passes='mem2reg,helloworld' t6.ll -o t6.out.ll
sed -n '/^define/,/^}/p' t6.out.ll
```

Expected log lines:

```text
[rahul_cse] ... add nsw i32 %1, %0  ==>  add nsw i32 %0, %1
[rahul_dead_code_elimination] deleting  ... mul nsw i32 %0, 7
```

Expected final IR:

```llvm
define dso_local i32 @compute(i32 noundef %0, i32 noundef %1) {
  %3 = add nsw i32 %0, %1
  %4 = mul nsw i32 %3, %3
  ret i32 %4
}
```

`b + a` is recognized as the same expression as `a + b` because addition is
commutative, so it is computed once. The unused `a * 7` is then deleted.
