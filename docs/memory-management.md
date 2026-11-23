# Managed memory

Z+ automatically manages arrays and dynamically concatenated strings. Programs
do not manually free these values.

```zp
use zp.memory.management

start {
    let name = "Z+" + " language"
    let numbers: number[] = [10, 20, 30]

    say(name)
    say(numbers)
    say(memory.heapUsed())
    say(memory.heapObjects())

    memory.collect()
    say(memory.collectionCount())
} stop
```

`use zp.memory.management` resolves the standard module without a source-file
path. Its default module name is `zp.memory`.

## Public API

| Function | Behavior |
|---|---|
| `memory.heapUsed() -> number` | Estimated bytes held by currently managed objects. |
| `memory.heapObjects() -> number` | Number of live managed arrays and dynamic strings. |
| `memory.collect()` | Runs a tracing collection immediately. |
| `memory.collectionCount() -> number` | Completed collection cycles in this execution. |
| `memory.setThreshold(bytes: number)` | Sets the minimum automatic collection threshold. Requires an integer from 1024 through 1099511627776. |

`heapUsed()` counts managed object headers and payload capacities. It excludes
collector bookkeeping, native stack/global storage, static string literals, and
ordinary interpreter string values that remain owned by C++ value semantics.
It is an estimate, not a process-wide memory measurement.

## Allocation and collection

Arrays, their element buffers, and dynamically produced strings are allocated
in a non-moving managed heap. This includes concatenation results and strings
returned by standard-library string, file, path, environment, time, argument,
and logging APIs. Arrays returned by `split`, `listDirectory`, `args.all`, and
collection-copy operations are managed arrays. Array element buffers grow
geometrically to avoid allocating on every append. `pop`, `remove`, and `clear` reduce logical length
and release removed references, but retain capacity for later reuse. The
initial automatic collection threshold is 64 KiB. When the current
managed size reaches the threshold, the runtime collects before a later managed
allocation. After a collection, the next threshold grows with the live heap so
that a large reachable set does not cause a collection on every allocation.
`setThreshold()` configures the minimum for that adaptive threshold.

The collector marks references reachable from module globals and active
interpreter frames, then sweeps unmarked objects. It follows references inside
nested arrays and safely handles cycles in the object graph. Native Windows
frames and globals register explicit slot ranges; native managed string
references use tagged handles, so arbitrary process memory is not scanned.

Collection may reclaim unreachable objects but does not promise deterministic
destruction of ordinary values. At program exit the runtime releases all
remaining managed allocations.

## Existing ownership model

Before the managed heap, interpreter numbers and booleans were stored by value,
strings were `std::string` values, and arrays were `shared_ptr` graphs. Shared
array references allowed mutation through aliases, but reference cycles could
retain allocations. Runtime slots and environment maps owned their values;
function argument and return values were copied or moved through call frames.
Native scalar values occupy 64-bit stack or global slots. Native string literals
refer to executable data, while concatenation and graphics-returned strings
were retained by the Windows runtime until shutdown.

The collector replaces shared ownership for interpreter arrays and manages
interpreter concatenation results. Other non-managed string values continue to
use value ownership and are released normally by C++.

## Native support and limitations

Windows x64 native programs use the same public APIs and a non-moving
mark-and-sweep heap for arrays and dynamically created strings, including
concatenation results. Arrays are stable tagged handles to dynamically sized
value buffers.
Generated code registers local slot ranges for active calls and the module
global range; the runtime traces nested tagged array and managed-string
references rather than scanning arbitrary process memory. Array construction
temporarily roots the element buffer, and push/insert root their receiver and
new element during capacity growth and any resulting collection. Native
standard-library array builders root their temporary string handles until the
containing array is allocated. Native executables that
use managed values require `zgraphics_runtime.dll` beside the executable; it is
packaged by `zp build`.

Both backends support literal creation, nested arrays, indexed reads/writes,
`length`, `push`, `pop`, `clear`, `insert`, `remove`, and `contains`; aliases,
function arguments/returns, and globals share the same mutable array. `pop`
and `remove` return the removed element. `contains` compares primitive values
and strings by value and nested arrays by identity. Negative and out-of-range
indices, invalid insertion positions, and popping an empty array produce
errors. Linux native array and managed-memory operations are not implemented
and are rejected explicitly. Neither backend exposes raw pointers, manual
deallocation, custom allocators, or deterministic finalizers.
