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

## 1. Prerequisites

You need these installed before starting:

```bash
sudo apt update
sudo apt install -y git cmake ninja-build build-essential python3
```

Check the versions:

```bash
cmake --version     # 3.20 or newer
ninja --version
g++ --version
```

You also need roughly **60 GB of free disk space** and a machine with several
cores. The build takes a while the first time.

---

## 2. Get the LLVM source

Pick a working directory. This README uses `$HOME/llvm-work`, but any path
works as long as you stay consistent.

```bash
mkdir -p $HOME/llvm-work
cd $HOME/llvm-work

git clone --depth=1 https://github.com/llvm/llvm-project.git
```

After this you have:

```text
$HOME/llvm-work/llvm-project/
```

---

## 3. Copy `HelloWorld.cpp` into the LLVM tree

This is the most important step. The file must replace the stock LLVM file at
exactly this path:

```text
llvm-project/llvm/lib/Transforms/Utils/HelloWorld.cpp
```

Assuming `HelloWorld.cpp` from this project is in your current directory:

```bash
cd $HOME/llvm-work

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

| File | Why it already works |
| --- | --- |
| `llvm/include/llvm/Transforms/Utils/HelloWorld.h` | Already declares `HelloWorldPass` |
| `llvm/lib/Passes/PassRegistry.def` | Already contains `FUNCTION_PASS("helloworld", HelloWorldPass())` |
| `llvm/lib/Transforms/Utils/CMakeLists.txt` | Already lists `HelloWorld.cpp` |

That registry line is what makes `-passes=helloworld` on the command line find
the C++ class `HelloWorldPass`.

---

## 4. Configure and build

Configure the build from inside `llvm-project`:

```bash
cd $HOME/llvm-work/llvm-project

cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF
```

A common mistake: running the `cmake` command while already inside a directory
named `build` creates a nested `build/build`. Run it from the `llvm-project`
directory as shown above.

Now build only the two targets needed:

```bash
cmake --build build --target LLVMTransformUtils opt clang -j"$(nproc)"
```

Or, equivalently, with ninja directly:

```bash
cd build
ninja LLVMTransformUtils opt clang
```

The first build is slow. Every rebuild after editing `HelloWorld.cpp` takes
under a minute, because only that one file recompiles.

When it finishes, confirm the binaries exist:

```bash
ls $HOME/llvm-work/llvm-project/build/bin/opt
ls $HOME/llvm-work/llvm-project/build/bin/clang
```

Nothing is installed system-wide. There is no `ninja install` step, no `sudo`,
and no change to your `PATH`. Everything stays inside the build directory.

---

## 5. Set up the test-cases directory

Create the test directory **next to** `llvm-project`, not inside it. Keeping
your files out of the LLVM tree avoids confusing `git status` later.

```bash
mkdir -p $HOME/llvm-work/test-cases
cd $HOME/llvm-work/test-cases
```

Copy all the `.c` files from this project into it. You should end up with:

```text
$HOME/llvm-work/test-cases/
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
│   └── build/bin/{opt,clang}                      <-- the tools you built
└── test-cases/
    └── *.c                                        <-- your test programs
```

---

## 6. Running a test case

Every test is two commands. Set a shortcut variable first so the commands stay
short:

```bash
cd $HOME/llvm-work/test-cases

export B=$HOME/llvm-work/llvm-project/build/bin
```

You must re-run that `export` line every time you open a new terminal.

### Step 1: C source to unoptimized LLVM IR

```bash
$B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone t4_constfold.c -o t4.ll
```

What the flags mean:

| Flag | Meaning |
| --- | --- |
| `-O0` | No optimization, so your pass has something left to optimize |
| `-S` | Emit text, not a binary object file |
| `-emit-llvm` | Emit LLVM IR instead of assembly |
| `-Xclang -disable-O0-optnone` | Removes the `optnone` attribute clang adds at `-O0`; without this, `opt` refuses to run any pass on the function |

Look at the result:

```bash
cat t4.ll
```

### Step 2: Run the optimization passes

```bash
$B/opt -S -passes='mem2reg,helloworld' t4.ll -o t4.out.ll
```

What this means:

| Part | Meaning |
| --- | --- |
| `-S` | Write readable `.ll` output instead of bitcode |
| `mem2reg` | Promotes stack slots to SSA registers |
| `helloworld` | Your pass, all five optimizations |
| `-o t4.out.ll` | Where the optimized IR goes |

**`mem2reg` is required.** At `-O0` every local variable is an `alloca` with
`load`/`store` around it, so the arithmetic is hidden behind memory. Your
passes work on SSA values, so without `mem2reg` running first they see nothing
to optimize and the output is identical to the input.

Compare before and after:

```bash
diff t4.ll t4.out.ll
```

Or just show the function body:

```bash
sed -n '/^define/,/^}/p' t4.out.ll
```

---

## 7. All test cases with expected results

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

---

## 8. Run everything at once

```bash
cd $HOME/llvm-work/test-cases
export B=$HOME/llvm-work/llvm-project/build/bin

for f in t1_strength t2_algebraic t3_copyprop t4_constfold t5_redundant t6_cse; do
  echo "##### $f #####"
  $B/clang -O0 -S -emit-llvm -Xclang -disable-O0-optnone "$f.c" -o "$f.ll"
  $B/opt -S -passes='mem2reg,helloworld' "$f.ll" -o "$f.out.ll"
  echo "--- final IR ---"
  sed -n '/^define/,/^}/p' "$f.out.ll"
  echo
done
```

---

## 9. After editing `HelloWorld.cpp`

Whenever you change the pass, rebuild just the two affected targets:

```bash
cd $HOME/llvm-work/llvm-project/build
ninja LLVMTransformUtils opt
```

Then re-run your test. You do not need to rebuild `clang`, since the `.ll`
files you already generated are still valid inputs.

---

## 10. Troubleshooting

**`ninja: error: loading 'build.ninja': No such file or directory`**

You are in the wrong directory, or CMake created a nested `build/build`. Check
where `build.ninja` actually is:

```bash
find $HOME/llvm-work/llvm-project -maxdepth 3 -name build.ninja
```

Run `ninja` from the directory that contains it.

**The pass prints nothing and the IR is unchanged**

You almost certainly forgot `mem2reg`. Use
`-passes='mem2reg,helloworld'`, not `-passes=helloworld` alone.

**`opt` runs but skips the function entirely**

You forgot `-Xclang -disable-O0-optnone` when generating the `.ll`. Check for
the `optnone` attribute:

```bash
grep optnone t4.ll
```

If it appears, regenerate the `.ll` with the flag.

**Values turn into `undef` or `0` unexpectedly**

Some test cases read variables that were never initialized, for example `c` and
`f` in `t1_strength.c`. LLVM types those as `undef` and folds operations on
them to `0`. This is correct behavior for undefined values, not a bug. If you
want to watch the shifts survive into the final IR, give those variables
initial values first.

**Want to see the IR without running your pass**

```bash
$B/opt -S -passes='mem2reg' t4.ll -o t4.mem2reg.ll
cat t4.mem2reg.ll
```

This is the exact input your pass receives, which makes debugging much easier.
