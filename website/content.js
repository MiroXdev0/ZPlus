window.ZPlusSite = (() => {
  const l = (title, explain, code, output, exercise, solution, summary, files = []) => ({
    title, explain, code, output, exercise, solution, summary, files
  });
  const course = [
    { title: "Introduction", lessons: [
      l("What is Z+?", "Z+ is a general-purpose language with a compiler, interpreter, standard library, and ZPBuild project tool. A program is text saved in a source file; the compiler checks that text before it runs.", `start {
    say("A program is a set of instructions.")
} stop`, "A program is a set of instructions.", "Change the message to describe one thing you want a computer to do.", `start {
    say("A computer can follow clear instructions.")
} stop`, "A Z+ program is source code that can be checked and run with the Z+ tools."),
      l("Installing Z+", "Install Z+ from the latest stable release shown on the Download page. The official release page is the source of truth; if it has no published release, wait for a release rather than downloading a guessed file.", `start {
    say("After installation, verify Z+ from a new terminal.")
} stop`, "After installation, verify Z+ from a new terminal.", "After installing, run `zp --version` in PowerShell or a terminal, then use `zp check main.zp` on a source file.", `start {
    say("The download page links only to published release assets.")
} stop`, "Use an official release and verify the command-line compiler responds before starting."),
      l("Running your first program", "Save code in a file ending in `.zp`. `zp run` checks the source and executes it with the interpreter. The braces and `stop` make the program entry block explicit.", `start {
    say("Hello, Z+!")
} stop`, "Hello, Z+!", "Save the example as `hello.zp`, then run `zp run hello.zp`.", `start {
    say("Hello from my first program.")
} stop`, "A `.zp` file with one `start { ... } stop` block is a complete first program."),
      l("Understanding the Z+ project structure", "A small project can begin with one source file. Larger projects use `.zh` declaration modules and a `zproject.toml` file for ZPBuild. Keep source, assets, and tests in separate folders as the project grows.", `start {
    say("src/main.zp is a common place for an app entry point.")
} stop`, "src/main.zp is a common place for an app entry point.", "Create a folder for a project and decide where its entry-point source file will live.", `start {
    say("Keep project files together and name their purpose clearly.")
} stop`, "A clear folder layout makes it easier to find code and assets."),
      l("Using zp from the terminal", "The compiler CLI includes `check`, `run`, and `build`. `check` validates without running; `run` interprets a program; `build` creates a native executable for the supported host subset.", `start {
    say("Use zp check before sharing a change.")
} stop`, "Use zp check before sharing a change.", "Try `zp --help`, `zp check hello.zp`, and `zp run hello.zp`; notice how checking differs from running.", `start {
    say("Check validates; run executes.")
} stop`, "Choose the CLI command that matches the job: validate, interpret, or compile."),
    ]},
    { title: "Language basics", lessons: [
      l("Your first Z+ program", "Every executable source file has one entry block. `say` writes a value followed by a newline. Z+ uses braces for blocks and an explicit block terminator.", `start {
    say("One instruction at a time.")
} stop`, "One instruction at a time.", "Add another `say` call so the program prints a second line.", `start {
    say("First line")
    say("Second line")
} stop`, "The entry block contains the statements that run when the program starts."),
      l("Variables", "Use `let` for a mutable variable and `const` for a value that should not be reassigned. The compiler can infer a variable's type from its initial value, or you can write the type explicitly.", `start {
    let score: number = 10
    score = score + 5
    const player: string = "Ari"
    say(player)
    say(score)
} stop`, "Ari\n15", "Create a mutable `number` called `lives`, set it to 3, subtract one, and print it.", `start {
    let lives: number = 3
    lives = lives - 1
    say(lives)
} stop`, "`let` creates mutable state; `const` requires an initial value and cannot be reassigned."),
      l("Data types", "The common primitive types are `number`, `string`, and `boolean`. Array types use `T[]`. A type annotation helps the compiler catch incompatible values early.", `start {
    let count: number = 3
    let label: string = "items"
    let ready: boolean = true
    say(label)
    say(ready)
} stop`, "items\ntrue", "Add a boolean named `finished` and print it.", `start {
    let finished: boolean = false
    say(finished)
} stop`, "Choose a type that describes the values a variable is meant to hold."),
      l("Numbers and arithmetic", "Z+ supports arithmetic operators such as `+`, `-`, `*`, `/`, and `%`. Multiplication and division bind more tightly than addition and subtraction; parentheses make intent explicit.", `start {
    let total: number = (4 + 2) * 3
    say(total)
} stop`, "18", "Calculate the remainder when 17 is divided by 5.", `start {
    let remainder: number = 17 % 5
    say(remainder)
} stop`, "Arithmetic expressions produce values that can be stored, compared, and printed."),
      l("Strings", "A string is text enclosed in double quotes. Join strings with `+`; there is no string interpolation syntax. `zstring` offers additional text functions.", `start {
    let greeting: string = "Hello, " + "Z+"
    say(greeting)
} stop`, "Hello, Z+", "Join your first name and a greeting, then print the result.", `start {
    let name: string = "Mira"
    say("Hello, " + name)
} stop`, "String concatenation combines two strings into a new string."),
      l("Booleans", "A boolean has one of two values: `true` or `false`. Comparisons produce booleans, which are commonly used to control branches and loops.", `start {
    let isReady: boolean = 5 > 2
    say(isReady)
} stop`, "true", "Compare two numbers and print whether the first is less than the second.", `start {
    say(3 < 8)
} stop`, "Boolean values represent a condition's truth value."),
      l("Comments", "Use `//` for a line comment. Comments explain intent to readers; they do not change the program's behavior.", `start {
    // Print a short status message.
    say("Ready")
} stop`, "Ready", "Add a comment explaining why your program prints its message.", `start {
    // Tell the user the program has started.
    say("Started")
} stop`, "Keep comments focused on intent that is not already obvious from the code."),
      l("Output with say() and printz()", "`say(value)` writes one value and a newline. `printz(value, ...values)` writes arguments consecutively with no separator and no final newline. The interpreter accepts more printable value types than native output, which currently accepts strings only.", `start {
    printz("Count: ", "3")
    say(" done")
} stop`, "Count: 3 done", "Convert a small number to text in your source and print it between two string arguments.", `start {
    printz("Score: ", "12")
    say("")
} stop`, "Use `say` for line-oriented output and `printz` when you need exact concatenation."),
    ]},
    { title: "Control flow", lessons: [
      l("Conditions", "Conditions are boolean expressions. Comparisons such as `==`, `!=`, `<`, and `>=` can be combined with `and`, `or`, or `not`.", `start {
    let age: number = 20
    say(age >= 18 and true)
} stop`, "true", "Check whether a number is both at least 1 and less than 10.", `start {
    let value: number = 7
    say(value >= 1 and value < 10)
} stop`, "A condition evaluates to a boolean the program can act on."),
      l("if, else if, and else", "`if` runs a block when its condition is true. `else if` tests another condition; `else` handles the remaining case. Close the whole chain with `end if`.", `start {
    let score: number = 82
    if score >= 90 {
        say("Excellent")
    } else if score >= 70 {
        say("Good")
    } else {
        say("Keep practicing")
    } end if
} stop`, "Good", "Add a branch that prints `Pass` when a score is at least 60.", `start {
    let score: number = 65
    if score >= 60 {
        say("Pass")
    } else {
        say("Try again")
    } end if
} stop`, "Branches let a program choose behavior while keeping each path readable."),
      l("while loops", "`while` repeats its block as long as its boolean condition remains true. Change state inside the loop so it can eventually finish.", `start {
    let count: number = 1
    while count <= 3 {
        say(count)
        count++
    } end while
} stop`, "1\n2\n3", "Use a `while` loop to print 0, 1, and 2.", `start {
    let count: number = 0
    while count < 3 {
        say(count)
        count++
    } end while
} stop`, "A loop condition is checked before each iteration."),
      l("for loops", "A range loop visits integers from its start inclusive to its end exclusive. An array loop visits each element in order. Both use `end for`.", `start {
    for index in 0..3 {
        say(index)
    } end for
} stop`, "0\n1\n2", "Change the range so the loop prints 2, 3, and 4.", `start {
    for index in 2..5 {
        say(index)
    } end for
} stop`, "The end of a range is exclusive, so `2..5` visits 2, 3, and 4."),
      l("Nested control flow", "A control-flow block can contain another block. Use indentation to make nesting visible, and keep each closing terminator matched to its opening construct.", `start {
    for value in 1..4 {
        if value % 2 == 0 {
            say(value)
        } end if
    } end for
} stop`, "2", "Print only values greater than 2 from a range loop.", `start {
    for value in 1..5 {
        if value > 2 {
            say(value)
        } end if
    } end for
} stop`, "Nested blocks express a sequence of checks and actions without changing the outer loop."),
      l("Understanding block terminators", "Brace-style Z+ blocks pair `{` and `}` with a kind-specific terminator such as `end if`, `end while`, or `end fn`. The top-level block ends with `} stop`. Do not mix legacy colon blocks inside brace blocks.", `start {
    if true {
        say("The inner block closes first")
    } end if
    say("Then the program block closes")
} stop`, "The inner block closes first\nThen the program block closes", "Match each opening block with its correct explicit terminator.", `start {
    while false {
        say("This does not run")
    } end while
} stop`, "Explicit terminators make the structure of nested Z+ blocks unambiguous."),
    ]},
    { title: "Functions", lessons: [
      l("Declaring functions", "Declare a function with `fn name(parameters) { ... } end fn`. Functions belong inside the `start` declaration block. A function without a return value has type `void`.", `start {
    fn greet() {
        say("Hello")
    } end fn
    greet()
} stop`, "Hello", "Declare and call a function named `showStatus`.", `start {
    fn showStatus() {
        say("Ready")
    } end fn
    showStatus()
} stop`, "A function gives a reusable name to a sequence of statements."),
      l("Parameters", "Parameters receive values from the caller. Add a type annotation to document and check the values the function expects.", `start {
    fn greet(name: string) {
        say("Hello, " + name)
    } end fn
    greet("Ari")
} stop`, "Hello, Ari", "Write a function that accepts a `string` and prints it.", `start {
    fn show(message: string) {
        say(message)
    } end fn
    show("Practice")
} stop`, "Parameters let a function work with different input values."),
      l("Return values", "A function declares a return type after `:` and sends a value back with `return`. The value must match the declared type.", `start {
    fn add(left: number, right: number): number {
        return left + right
    } end fn
    say(add(2, 3))
} stop`, "5", "Write a function that doubles one numeric argument and returns a number.", `start {
    fn double(value: number): number {
        return value * 2
    } end fn
    say(double(4))
} stop`, "Return values make a function's computed result available to its caller."),
      l("Default arguments", "A trailing parameter can specify a default expression. Parameters after the first default must also have defaults. Calls may omit those trailing arguments.", `start {
    fn greet(name: string = "friend"): string {
        return "Hello, " + name
    } end fn
    say(greet())
} stop`, "Hello, friend", "Give a numeric function parameter a default value of 1.", `start {
    fn addOne(value: number = 1): number {
        return value + 1
    } end fn
    say(addOne())
} stop`, "Defaults provide an explicit value when a caller omits trailing arguments."),
      l("Variable scope", "A variable is visible in the scope where it is declared and its nested scopes. A function's parameters and local variables are separate from variables in its caller.", `start {
    let message: string = "outside"
    fn show() {
        let local: string = "inside"
        say(local)
    } end fn
    show()
    say(message)
} stop`, "inside\noutside", "Declare a variable inside a function and use it there; keep a different variable in the entry block.", `start {
    fn report() {
        let status: string = "local"
        say(status)
    } end fn
    report()
} stop`, "Lexical scope controls where a declared name can be referenced."),
    ]},
    { title: "Data structures", lessons: [
      l("Arrays", "Array literals use brackets and array types use `T[]`. Arrays are mutable and indexed from zero. `length` returns the number of elements.", `start {
    let colors: string[] = ["blue", "gold"]
    say(colors[0])
    say(length(colors))
} stop`, "blue\n2", "Create a numeric array and print its last element.", `start {
    let values: number[] = [4, 8, 12]
    say(values[2])
} stop`, "Arrays hold an ordered sequence of values of a compatible type."),
      l("Working with collections", "Core array functions include `push`, `insert`, `pop`, `remove`, `contains`, `clear`, and `length`. `zcollections` supplies numeric helpers such as `sum` and `average`.", `start {
    let scores: number[] = [4, 7]
    push(scores, 9)
    say(contains(scores, 7))
    say(length(scores))
} stop`, "true\n3", "Add an item to an array, check whether it is present, and print the length.", `start {
    let tasks: string[] = ["read"]
    push(tasks, "write")
    say(contains(tasks, "write"))
    say(length(tasks))
} stop`, "Use core array operations for mutation; `zcollections` focuses on numeric-array utilities."),
      l("Structs and classes", "Structs and classes define nominal types with fields. A struct groups typed data; a class can also declare instance methods. Both use implicit positional construction and literal field defaults. Struct instances use reference semantics: assigning an instance to another variable preserves its identity.", `start {
    struct Point {
        x: number
        y: number
    } end struct
    let origin = Point(0, 0)
    say(origin.x)
} stop`, "0", "Create a `struct` with a string field and construct one instance.", `start {
    struct Book {
        title: string
    } end struct
    let book = Book("Z+ Notes")
    say(book.title)
} stop`, "Use a custom data type when several related values should travel together."),
      l("Choosing the right data structure", "Use an array when order and indexed access matter. Use a struct or class when values have named meaning. Dedicated map/dictionary and set types are not currently implemented.", `start {
    let names: string[] = ["Ari", "Mira"]
    for name in names {
        say(name)
    } end for
} stop`, "Ari\nMira", "Choose a suitable structure for an ordered list of three filenames and explain why.", `start {
    let files: string[] = ["main.zp", "tasks.zh", "README.md"]
    say(files[0])
} stop`, "Prefer implemented structures that match the problem; do not assume map/set APIs exist."),
    ]},
    { title: "Modules and the standard library", lessons: [
      l("Importing modules with use", "`use` loads a standard library module or a relative `.zh` file. An alias names its namespace. Standard module imports are resolved by the compiler's configured standard-library directory.", `use zmath as math
start {
    say(math.sqrt(81))
} stop`, "9", "Import `zstring` as `text` and call `text.upper`.", `use zstring as text
start {
    say(text.upper("hello"))
} stop`, "A module import keeps related functions under a clear namespace."),
      l("Creating reusable modules", "A user module is a `.zh` file containing declarations inside its declaration-only `start` block. Import it with a relative path from the importing file; declarations are private unless marked `pub`. The two files below form one complete import example.", `use "greetings.zh" as greetings
start {
    say(greetings.hello())
} stop`, "Hello", "In a sibling `greetings.zh`, export a zero-argument function named `hello` that returns a string.", `start {
    pub fn hello(): string {
        return "Hello"
    } end fn
} stop`, "Relative modules share code and use explicit `pub` visibility for exported declarations.", [
        { name: "greetings.zh", code: `start {
    pub fn hello(): string {
        return "Hello"
    } end fn
} stop` }
      ]),
      l("Strings and text processing", "`zstring` provides text functions such as `upper`, `lower`, `trim`, `split`, `join`, and `replace`. String length is counted in Unicode code points.", `use zstring as text
start {
    say(text.upper("z plus"))
    say(text.length("Z+"))
} stop`, "Z PLUS\n2", "Trim surrounding spaces from a string and print it.", `use zstring as text
start {
    say(text.trim("  ready  "))
} stop`, "Call the module functions rather than assuming strings have built-in methods."),
      l("Math utilities", "`zmath` includes functions such as `abs`, `round`, `sqrt`, `pow`, `min`, and `max`. The core `number` type is double precision. Standard-library functions and backend support vary, so check the module documentation before relying on a target-specific operation.", `use zmath as math
start {
    say(math.abs(-7))
    say(math.max(3, 9))
} stop`, "7\n9", "Use `math.min` to print the smaller of 12 and 5.", `use zmath as math
start {
    say(math.min(12, 5))
} stop`, "The interpreter and Windows x64 native backend support the shown integer-valued results. Check target-specific documentation before relying on other math operations."),
      l("File operations", "`zfile` exposes public functions including `exists`, `read`, `write`, `append`, `readLines`, and `writeLines`. I/O failures raise runtime errors; use `try`/`catch` when recovery is appropriate.", `use zfile as file
start {
    file.write("note.txt", "first")
    file.append("note.txt", " line")
    say(file.read("note.txt"))
} stop`, "first line", "Write two lines to a file and read them back with `readLines`.", `use zfile as file
start {
    file.writeLines("tasks.txt", ["learn", "build"])
    say(length(file.readLines("tasks.txt")))
} stop`, "Use the public file API and handle expected I/O failures instead of hiding them."),
      l("Working with paths", "`zpath` provides path operations such as `join`, `normalize`, `filename`, `extension`, and `isAbsolute`. Paths follow the host platform's filesystem rules.", `use zpath as path
start {
    let full: string = path.join("projects", "main.zp")
    say(path.filename(full))
} stop`, "main.zp", "Join a folder and a file name, then print its extension.", `use zpath as path
start {
    let full: string = path.join("src", "app.zp")
    say(path.extension(full))
} stop`, "Use path functions instead of manually concatenating separators."),
      l("Environment variables", "`zenv` reads and writes process environment values. `exists` distinguishes a missing variable from an existing variable with an empty value. Changes affect only the current process, so results depend on the environment that launched the program.", `use zenv as env
start {
    say(env.exists("PATH"))
} stop`, "", "Check whether a variable named `APP_MODE` exists.", `use zenv as env
start {
    say(env.exists("APP_MODE"))
} stop`, "Treat environment values as external input and check presence before relying on them."),
      l("Time and dates", "`ztime` provides `now`, date-part helpers, `format`, `parse`, and `sleep`. Its format patterns follow the host C library's time-format conventions. Date-part results depend on the current host clock and local timezone.", `use ztime as time
start {
    let timestamp: number = time.now()
    say(time.year(timestamp))
} stop`, "", "Read the current timestamp and print its month.", `use ztime as time
start {
    let timestamp: number = time.now()
    say(time.month(timestamp))
} stop`, "Use the time module for timestamps and formatting rather than guessing date syntax."),
    ]},
    { title: "Input and errors", lessons: [
      l("Reading terminal input", "`read()` reads one complete line from standard input as a string, preserving spaces. You can pass a string prompt; the runtime writes and flushes it before waiting. `read()` returns an empty string at EOF, so validate it before treating the value as meaningful data.", `start {
    let name: string = read("What is your name? ")
    say("Hello, " + name)
} stop`, "The greeting includes the line entered by the user.", "Ask the user for a favorite language and print the response.", `start {
    let answer: string = read("Favorite language? ")
    say(answer)
} stop`, "A terminal program becomes interactive by reading input and responding to it."),
      l("Numeric and boolean input", "`read_number`, `read_integer`, and `read_boolean` accept an optional string prompt and parse one complete line. Surrounding ASCII whitespace is accepted. Invalid values, overflow, and EOF raise runtime errors instead of silently becoming a default value.", `start {
    let count: number = read_integer("Enter an integer: ")
    say(count)
} stop`, "The integer entered by the user", "Read a number and print it back.", `start {
    let value: number = read_number("Enter a number: ")
    say(value)
} stop`, "Select the reader that matches the value you expect and plan for invalid input."),
      l("Handling invalid input", "`try`/`catch` handles a thrown value or interpreter runtime error. The caught `Error` currently exposes its `message` field; custom error subclasses and stable error codes are not implemented.", `start {
    try {
        let value: number = read_integer()
        say(value)
    } catch error {
        say(error.message)
    } end try
} stop`, "On invalid input, the caught runtime error message is printed.", "Catch an error from a file read and show its message.", `use zfile as file
start {
    try {
        say(file.read("missing.txt"))
    } catch error {
        say(error.message)
    } end try
} stop`, "Catch expected runtime failures close to where the program can respond meaningfully."),
      l("Understanding compiler diagnostics", "`zp check` reports problems before execution. Diagnostics include a source file and location when known; syntax, semantic, and module errors have distinct diagnostic codes.", `start {
    let count: number = "three"
} stop`, "A type diagnostic points to the incompatible initializer.", "Run `zp check` on a file with a type mismatch and use the location to find the faulty expression.", `start {
    let count: number = 3
    say(count)
} stop`, "Use diagnostics to find the source of the error; fix the expression rather than suppressing the message."),
      l("Writing reliable programs", "Reliable programs validate inputs, make state changes explicit, and report errors instead of pretending an operation succeeded. Keep functions small enough to test and understand.", `start {
    let value: number = 4
    if value > 0 {
        say("positive")
    } else {
        say("not positive")
    } end if
} stop`, "positive", "Add a validation branch that prints a message when a count is negative.", `start {
    let count: number = -1
    if count < 0 {
        say("Count cannot be negative")
    } else {
        say(count)
    } end if
} stop`, "Validate assumptions at boundaries and make failure behavior clear."),
    ]},
    { title: "Building real projects", lessons: [
      l("Building a command-line application", "A command-line app combines input, decisions, reusable functions, and output. Start with one job and one predictable input/output path before adding options.", `start {
    fn greet(name: string): string {
        return "Hello, " + name
    } end fn
    say(greet("developer"))
} stop`, "Hello, developer", "Write a small command-line program that converts a known number of minutes to seconds.", `start {
    fn seconds(minutes: number): number {
        return minutes * 60
    } end fn
    say(seconds(5))
} stop`, "Build one useful behavior first, then extend it with validation and tests."),
      l("Creating a TODO application", "A TODO program needs a representation for tasks and commands to add, list, and complete them. This lesson uses a typed array in memory; file persistence follows in the next lesson.", `start {
    let tasks: string[] = ["Read the Z+ syntax guide"]
    push(tasks, "Build a small program")
    for task in tasks {
        say(task)
    } end for
} stop`, "Read the Z+ syntax guide\nBuild a small program", "Add a third task and print all tasks in order.", `start {
    let tasks: string[] = ["Plan"]
    push(tasks, "Build")
    push(tasks, "Review")
    for task in tasks {
        say(task)
    } end for
} stop`, "Arrays and loops are enough to model and display a simple ordered TODO list."),
      l("Reading and writing files", "Persist tasks by writing lines to disk and reading them again when the program starts. `writeLines` separates entries with newlines; filesystem errors can be caught.", `use zfile as file
start {
    file.writeLines("tasks.txt", ["Learn Z+", "Build an app"])
    let tasks: string[] = file.readLines("tasks.txt")
    for task in tasks {
        say(task)
    } end for
} stop`, "Learn Z+\nBuild an app", "Save two task strings to `tasks.txt`, read them back, and display them.", `use zfile as file
start {
    let tasks: string[] = ["Practice", "Share"]
    file.writeLines("tasks.txt", tasks)
    for task in file.readLines("tasks.txt") {
        say(task)
    } end for
} stop`, "Persisting plain lines is a simple starting point; choose a structured format when tasks need more fields."),
      l("Organizing a multi-file project", "Move reusable declarations into sibling `.zh` files and import them with a relative path. The module loader resolves from the importing file, loads a canonical file once, and detects cycles.", `use "greetings.zh" as greetings
start {
    say(greetings.hello())
} stop`, "Hello from a module", "Create `greetings.zh` with a public `hello` function returning a string.", `start {
    pub fn hello(): string {
        return "Hello from a module"
    } end fn
} stop`, "Modules separate implementation while keeping explicit exports.", [
        { name: "greetings.zh", code: `start {
    pub fn hello(): string {
        return "Hello from a module"
    } end fn
} stop` }
      ]),
      l("Using ZPBuild", "ZPBuild provides project commands and templates. Create a project using the installed `zpbuild` tool, inspect its `zproject.toml`, then use `zpbuild build` or `zpbuild run` from the project directory. The `zbuild` command remains a compatibility alias.", `start {
    say("ZPBuild project commands are run in the terminal.")
} stop`, "ZPBuild project commands are run in the terminal.", "Create a Console project with ZPBuild and read its generated README and project configuration.", `start {
    say("Keep project configuration next to the source tree.")
} stop`, "ZPBuild commands are CLI commands; do not put them inside Z+ source."),
      l("Building a release", "A release build uses `zpbuild build --release`; packaging uses `zpbuild package --release`. Test the program and inspect the package output before sharing it. Native backend support is limited to the documented subset.", `start {
    say("Test the release build before distributing it.")
} stop`, "Test the release build before distributing it.", "Build and package a small ZPBuild project in release mode, then verify the produced files.", `start {
    say("A release should include the files the program actually needs.")
} stop`, "A release is a tested artifact, not just a successful compiler invocation."),
    ]},
    { title: "Advanced Z+", lessons: [
      l("Native compilation", "`zp build source.zp -o program.exe` generates a native executable for the host. Native builds reject unsupported constructs instead of silently falling back to the interpreter.", `start {
    say("This program uses a supported basic output operation.")
} stop`, "This program uses a supported basic output operation.", "Build a small program natively and run the generated executable.", `start {
    say("Native builds produce an executable for the host.")
} stop`, "Check the documented native subset when a feature compiles in the interpreter but not natively."),
      l("Interpreter versus native backend", "`zp run` uses the interpreter; `zp build` targets native code. Their supported features and numeric behavior differ, so test in the backend you intend to ship.", `start {
    let value: number = 7
    say(value * 2)
} stop`, "14", "Run a simple program with the interpreter and compare it with a native build on Windows x64.", `start {
    let value: number = 9
    say(value + 1)
} stop`, "Backend parity is feature-specific; consult the runtime notes instead of assuming every module behaves identically."),
      l("Performance considerations", "Measure a real workload before optimizing. Prefer clear algorithms and avoid repeated file reads or unnecessary copies. The language's current numeric and native limits can affect performance choices.", `start {
    let values: number[] = [2, 4, 6]
    say(length(values))
} stop`, "3", "Identify one repeated operation in a program and decide whether its result can be computed once.", `start {
    let total: number = 2 + 4 + 6
    say(total)
} stop`, "Optimize measured bottlenecks while keeping the program easy to verify."),
      l("Advanced standard-library functionality", "The standard library includes modules for JSON, HTTP, SQLite, graphics, audio, physics, and other tasks. Availability and backend support varies by operation and platform; read the module documentation first.", `use zmath as math
start {
    say(math.pow(2, 3))
} stop`, "8", "Choose a standard library module for one project requirement and confirm its backend support in the reference.", `use zstring as text
start {
    say(text.trim(" ready "))
} stop`, "Standard library calls should be grounded in documented, implemented functions."),
      l("Creating and sharing packages", "Use ZPM to search for packages, install dependencies, and publish versioned ZPM archives; see the package documentation for supported workflows.", `use "utilities.zh" as utilities
start {
    say(utilities.describe())
} stop`, "A documented reusable module", "Prepare a small reusable `.zh` module, document its public functions, and share it with its project configuration.", `start {
    pub fn describe(): string {
        return "A documented reusable module"
    } end fn
} stop`, "The registry accepts ZPM archives; package metadata and source files are checked before publication.", [
        { name: "utilities.zh", code: `start {
    pub fn describe(): string {
        return "A documented reusable module"
    } end fn
} stop` }
      ]),
    ]},
    { title: "Types and modules", lessons: [
      l("Class visibility", "Class fields and methods accept `pub`, `private`, or `protected`. Members without a modifier remain public for compatibility. `private` and `protected` currently permit access only from methods of the declaring class; inheritance, and therefore subclass access to protected members, is not implemented.", `start {
    class Counter {
        private value: number = 0
        pub fn increment() {
            this.value = this.value + 1
        } end fn
        pub fn current(): number {
            return this.value
        } end fn
    } end class

    let counter = Counter()
    counter.increment()
    say(counter.current())
} stop`, "1", "Add a private field and expose a public method that updates it. Read the field only from methods of that class.", `start {
    class Meter {
        private value: number = 2
        pub fn next(): number {
            this.value = this.value + 1
            return this.value
        } end fn
    } end class

    let meter = Meter()
    say(meter.next())
} stop`, "Visibility is checked during semantic analysis before interpretation or native code generation."),
      l("Enums and typed states", "An enum is a nominal type whose members carry integer values. Omitted values start at zero and then increment the previous member. Explicit values must be signed integer literals, and comparisons require values of the same enum type.", `start {
    enum State {
        Ready
        Running = 4
        Done
    } end enum

    let current: State = State.Running
    say(current == State.Running)
    say(State.Done)
} stop`, "true\n5", "Declare an enum with two members and store one member in a variable with the enum type.", `start {
    enum Direction {
        North
        South
    } end enum
    let facing: Direction = Direction.South
    say(facing == Direction.South)
} stop`, "Enum members are numeric at runtime but retain their enum type during checking."),
    ]},
  ];

  const docs = [
    { id: "syntax", title: "Syntax reference", section: "Language", summary: "The executable Z+ syntax accepted by the current compiler.", body: [
      ["Program structure", "Every executable `.zp` source has one `start { ... } stop` block. Function, namespace, and type declarations belong inside it. Standard and relative modules use file-scope `use` directives.", 'start {\n    say("Hello, Z+!")\n} stop'],
      ["Types and variables", "Use `let` for mutable variables and `const` for initialized immutable variables. Common types include `number`, `string`, `boolean`, and arrays such as `number[]`."],
      ["Blocks and control flow", "Brace blocks use explicit terminators: `} end fn`, `} end if`, `} end while`, `} end for`, and `} stop`. `else if` and `else` remain in the same conditional chain."],
      ["Functions", "Declare `fn name(parameter: type): returnType { ... } end fn`. Parameter and return types follow the current function reference; return types are not inferred."],
      ["Limitations", "Pointers are parsed but are not implemented. Match expressions, inheritance, function values, map/set types, and string interpolation are not implemented."]
    ]},
    { id: "cli", title: "Compiler CLI", section: "Tools", summary: "Check, interpret, and build Z+ programs.", body: [
      ["Commands", "`zp check file.zp` validates without running. `zp run file.zp` executes in the interpreter. `zp build file.zp -o output.exe` requests native compilation.", "zp check hello.zp\nzp run hello.zp\nzp build hello.zp -o hello.exe"],
      ["Options", "`-o <file>` sets the native output path; `--windows-gui` selects the Windows GUI subsystem; `--source-stdin` reads check source from standard input; `--diagnostics=json` emits JSON diagnostics; `-h` and `--help` show command help."],
      ["Version and arguments", "`zp version` reports the compiler version. Program arguments after `--` are passed to `zp run`."],
      ["Backend behavior", "Native compilation does not fall back to interpretation. Unsupported native operations are reported. Supported syntax and runtime features vary by platform."]
    ]},
    { id: "standard-library", title: "Standard library", section: "Reference", summary: "Module names, public functions, and backend limitations.", body: [
      ["Imports", "Use `use zstring as text` or a dotted module such as `use zmemory.management as memory`. Call public functions through the imported namespace. The exact module set and compatibility depend on the packaged standard library."],
      ["Math and text", "`zmath` exports `pi()`, `e()`, unary `abs`, `round`, `floor`, `ceil`, `sqrt`, trigonometric, logarithm and exponential functions, and binary `pow`, `min`, and `max` (all numeric). `zstring` exports `upper`, `lower`, `trim`, `reverse` `(string)->string`; `split(string,string)->string[]`; `join(string[],string)->string`; `replace(string,string,string)->string`; `contains`, `startsWith`, `endsWith` `(string,string)->boolean`; `substring(string,number,number)->string`; `length(string)->number`; and `repeat(string,number)->string`."],
      ["Collections and randomness", "`zcollections` numeric-array APIs are `sum`, `average`, `min`, `max`, `reverse`, `sort`, `unique`, `contains`, `count`, `first`, `last`, and `isEmpty`. Core typed arrays also support `push`, `insert`, `pop`, `remove`, `contains`, `clear`, `length`, indexing, and `for` iteration. `zrandom` exports `integer(number,number)->number`, `number(number,number)->number`, `boolean()->boolean`, `choice(number[])->number`, `shuffle(number[])->number[]`, and `seed(number)->void`. Map and set types are not implemented."],
      ["Files and paths", "`zfile` exports `read(string)->string`, `readLines(string)->string[]`, `write(string,string)->void`, `writeLines(string,string[])->void`, `append(string,string)->void`, `exists(string)->boolean`, `delete(string)->boolean`, `createDirectory(string)->boolean`, `listDirectory(string)->string[]`, `size(string)->number`, and `isDirectory(string)->boolean`. `zpath` exports `join(string,string)`, `normalize(string)`, `extension(string)`, `filename(string)`, `directory(string)`, `absolute(string)` (return strings), and `isAbsolute(string)->boolean`."],
      ["Environment, time, arguments, logs", "`zenv` exports `get`, `exists`, `set`, `homeDirectory`, `currentDirectory`, and `arguments`. `ztime` exports `now`, `format`, `parse`, date-part helpers, and `sleep`. `zargs` exports `count`, `get`, `has`, and `all`. `zlog` exports `debug`, `info`, `warning`, `error`, `setLevel`, and `setOutput`. See the checked-in standard-library reference for exact parameter names and error behavior."],
      ["Other public modules", "The source-backed modules also include `zjson` (JSON text and JSON Pointer operations), `zfilesystem` (copy/move and directory operations), `zprocess` (child process execution), `zcrypto` (file SHA-256), `zhttp` (HTTP client/server and request helpers), `zsqlite` (SQLite handles/queries), `zregistry` (Windows registry), `zsystem` (system and shell operations), `zmemory.management` (heap metrics and collection), `zaudio`, `zgraphics`, and `zphysics`. Function signatures and platform support are specific to each wrapper."],
      ["Errors and backend support", "Filesystem and validation failures raise runtime errors rather than success-shaped empty values. The interpreter and Windows x64 native runtime are the main tested execution targets; some native numbers are integer-only and Linux native support varies. Verify each operation's support before depending on it."]
    ]},
    { id: "modules", title: "Modules and imports", section: "Language", summary: "Use standard modules and reusable relative `.zh` files.", body: [
      ["Relative modules", "A relative `use \"path.zh\" as alias` resolves from the file containing that directive. Imports can nest; canonical files load once and cycles are diagnosed."],
      ["Visibility", "Declarations in `.zh` modules are private by default. Mark declarations `pub` to expose them to importers."],
      ["Module initialization", "A module contains one declaration-only `start` block. Dependencies initialize in deterministic depth-first order."]
    ]},
    { id: "errors", title: "Errors and diagnostics", section: "Language", summary: "Understand compile-time diagnostics and runtime try/catch.", body: [
      ["Compile-time codes", "`E1000` is lexical, `E1001` is parsing, `E2001` is semantic analysis, `E3001` is module loading/imports, `E4001` is runtime execution, and `E0001` is command-line or other compiler errors. Locations are shown only when the compiler has a source position."],
      ["JSON diagnostics", "Use `--diagnostics=json` for one JSON diagnostic per line. Records include severity, code, message, file, line, and column when available; the current compiler also emits source-line and suggestion fields for supported diagnostics."],
      ["Runtime errors", "`try` requires a `catch`. The caught `Error` value currently exposes a `message` field. Custom error subclasses and stable error categories are not implemented."],
      ["Cleanup", "`defer` accepts one direct named function call and runs registered calls in last-in, first-out order when the scope exits."]
    ]},
    { id: "zbuild", title: "ZPBuild reference (legacy URL)", section: "Tools", summary: "Project build, run, test, and package commands; the current URL is `/docs/zpbuild`.", body: [
      ["Commands", "`zpbuild init [name]` creates a project; `templates` and `template info NAME` inspect templates; `configure` validates a project; `build`, `run`, `test`, `clean`, `rebuild`, `info`, `targets`, `graph`, and `package` manage targets and outputs. `zbuild` remains a compatibility alias. `--debug` / `--release`, `--target`, `--jobs`, `--verbose`, and `--clean-first` apply where supported.", "zpbuild init MyApp\ncd MyApp\nzpbuild build --release\nzpbuild test"],
      ["Configuration", "The root `zproject.toml` supports `[project]`, `[build]`, `[compiler]`, `[platform]`, `[assets]`, `[tests]`, and `[[target]]`. Use executable or test targets with `.zp` entry points; use `.zh` modules for source reuse. The parser accepts a constrained TOML subset, not arbitrary TOML."],
      ["Build behavior", "Incremental signatures include compiler version/hash, target settings, source and reachable module contents, dependency outputs, and assets. Outputs are under `build/<debug|release>/`; package files go in that selected profile."],
      ["Boundaries", "Targets are standalone executables or tests; the compiler has no object, archive, or library-link interface. Local dependencies are locked and validated but are not automatically imported or linked. Use ZPM for package discovery and installation; platform and native-link support remain target-specific."]
    ]},
    { id: "native", title: "Native backend", section: "Runtime", summary: "Supported subset and platform-specific behavior.", body: [
      ["Host targets", "The compiler has x86-64 native backends. Runtime integrations and supported constructs differ between Windows and Linux."],
      ["No fallback", "`zp build` reports unsupported native operations instead of emitting an interpreter wrapper."],
      ["Interpreter", "`zp run` executes through the interpreter. It supports features that may not be available in a native backend, including interpreter-specific runtime semantics."],
      ["Windows x64", "Windows native execution has the broader tested runtime set. Programs using native runtime DLLs must distribute those DLLs beside the executable."],
      ["Linux x64", "Linux native support is narrower and is not certified for all standard-library modules or aggregate, graphics, exception, and memory-management operations."],
      ["Testing", "Run the generated executable and test the target OS; successful semantic checking alone does not establish backend support."]
    ]},
    { id: "builtins", title: "Built-in functions", section: "Language", summary: "Terminal input, output, and dynamic-array operations.", body: [
      ["Output", "`say(value)->void` prints one value followed by a newline. `printz(value, ...values)->void` prints arguments consecutively with no separator and no final newline. `printm` is a deprecated compatibility alias for `printz`."],
      ["Terminal input", "`read()->string`, `read_number()->number`, `read_integer()->number`, and `read_boolean()->boolean` read and parse a line from standard input. Invalid typed input and EOF report runtime errors."],
      ["Arrays", "`length(array)->number`, `push(array,value)->void`, `insert(array,index,value)->void`, `pop(array)->element`, `remove(array,index)->element`, `contains(array,value)->boolean`, and `clear(array)->void` operate on typed mutable arrays. Indexes are zero-based; invalid indexes and popping an empty array fail."],
      ["Backend limits", "Interpreter output supports numbers, booleans, arrays, and strings. Native output currently accepts strings only; native array and number support varies by target. Refer to the backend article for those limits."]
    ]},
    { id: "installation", title: "Installation and verification", section: "Getting started", summary: "Install an official build and confirm the compiler works.", body: [
      ["Official downloads", "The Download page queries the official Z+ GitHub Releases API and links only to published assets. If the feed has no stable release or the API is unavailable, use the Releases page fallback instead of a guessed version or file."],
      ["Windows setup", "The published Windows x64 installer is built from the Release distribution with Inno Setup. It installs the command-line tools, required runtime files, standard library, project templates, documentation, and examples. Other platforms are listed only when a release asset is actually available."],
      ["Verify", "Open a new terminal and run `zp version`, `zpm --version`, and `zpbuild --version`. Validate a source program with `zp check hello.zp` and run it with `zp run hello.zp`."],
      ["Troubleshooting", "If a command is not found, open a fresh terminal and verify the installed `bin` directory is in your user PATH. Keep the distribution's runtime DLLs and standard-library directory in their shipped layout."]
    ]},
    { id: "introduction", title: "Introduction to Z+", section: "Getting started", summary: "What the language and its developer tools include today.", body: [
      ["A language and toolchain", "Z+ is a statically typed programming language with a C++ compiler, an interpreter, and a native code-generation backend. The repository includes the standard library, ZPM package manager, and ZPBuild build system. First-party project materials are licensed under Apache-2.0; third-party components retain their own terms."],
      ["Designed for readable programs", "Programs use explicit `start` and `stop` boundaries, typed declarations, named functions, and module imports. Z+ supports console programs and project-based workflows; backend support varies by feature and target."],
      ["Start here", "Install an actual published release, follow the first program guide, and use `zp check` before running a program. The reference labels important interpreter and native backend limits instead of treating them as interchangeable."],
      ["Source of current version", "Published release versions come from the official release feed. The checked-in compiler may be on a newer development version than the latest stable download; the site distinguishes the two."]
    ]},
    { id: "first-program", title: "Your first Z+ program", section: "Getting started", summary: "Create a source file, check it, and run it with the interpreter.", body: [
      ["Create hello.zp", "The minimal executable has a single `start` block, a call to `say`, and the matching `} stop` terminator.", 'start {\n    say("Hello, Z+!")\n} stop'],
      ["Check and run", "From the directory containing the file, validate it and then run it with the interpreter. Checking does not execute the program.", "zp check hello.zp\nzp run hello.zp"],
      ["Build a native executable", "On a target supported by the current compiler and source operations, `zp build` writes a native executable. Native builds do not fall back to the interpreter."],
      ["Next", "Continue with variables and types, or use the project guide to scaffold a larger multi-file project."]
    ]},
    { id: "variables", title: "Variables and types", section: "Language", summary: "Declare mutable values, constants, arrays, and custom types safely.", body: [
      ["Mutable and immutable values", "`let` declares a mutable local; `const` declares an initialized immutable value. Add a type annotation or let the compiler infer the type from an initializer."],
      ["Primitive types", "The core source types include `number`, `string`, `boolean`, and `char`. The `number` type is a double in the interpreter and native arithmetic, although some native standard-library functions remain integer-only."],
      ["Typed arrays", "Use `number[]`, `string[]`, or another supported element type. Arrays are mutable and dynamically sized. The interpreter and Windows x64 native backend support array operations; Linux native builds reject array operations."],
      ["Type checking", "Assignments, function arguments, returns, array elements, and enum values are checked. Structs and classes are nominal custom types; enum comparisons require matching enum types."]
    ]},
    { id: "functions", title: "Functions", section: "Language", summary: "Declare typed functions, pass arguments, and return values.", body: [
      ["Declaration form", "Functions declare parameter types and use a return annotation after `:`. The body closes with `} end fn`.", 'start {\n    fn add(left: number, right: number): number {\n        return left + right\n    } end fn\n\n    say(add(20, 22))\n} stop'],
      ["Calling functions", "Call by name with arguments in parameter order. Trailing default arguments are supported in interpreter and native calls. A `void` function may omit a return value."],
      ["Modules", "A function declared in an imported `.zh` file is private by default. Use `pub fn` to expose it through a module alias."],
      ["Execution support", "The interpreter supports recursive function calls. Native code generation supports its documented function subset; unsupported operations fail the native build."]
    ]},
    { id: "classes", title: "Classes", section: "Language", summary: "Create instances with fields and instance methods.", body: [
      ["Members and construction", "Classes support typed fields, literal field initializers, implicit positional construction, instance methods, and `this` for field access. There are no explicit constructor declarations."],
      ["Visibility", "Class fields and methods accept `pub`, `private`, or `protected`. Existing unmodified members remain public for compatibility. Private access is limited to methods of the declaring class; protected is reserved for subclasses, but inheritance is not implemented."],
      ["Validation", "Duplicate members, repeated or conflicting visibility modifiers, invalid fields, and illegal accesses are rejected during semantic analysis before execution."],
      ["Backend support", "Class construction, fields, `this`, and instance calls are supported by the interpreter and Windows x64 native backend. Linux native aggregate operations are unsupported."]
    ]},
    { id: "visibility", title: "Class visibility modifiers", section: "Language", summary: "Control access to class fields and methods.", body: [
      ["Modifiers", "`pub` allows callers to access a member, `private` restricts it to methods of the declaring class, and `protected` is reserved for subclass access. No inheritance syntax is implemented yet, so protected subclass access is not currently available."],
      ["Default", "A class member with no modifier is public. This preserves compatibility with existing classes."],
      ["Example", "The public method can read and mutate its private field. An external attempt to access that field is a compile-time semantic error."],
      ["Modules and execution", "The class declaration itself must be exported for use from another module. Visibility checks apply before both interpreter and native execution."]
    ]},
    { id: "enums", title: "Enums", section: "Language", summary: "Use nominal enum values with explicit or sequential integer members.", body: [
      ["Declaration and values", "Enum members have integer values. The first omitted value is zero; subsequent omitted values increment the previous member. Explicit values must be signed decimal, binary, or hexadecimal integer literals within the exact numeric range.", 'start {\n    enum State {\n        Ready\n        Running = 4\n        Done\n    } end enum\n\n    let current: State = State.Running\n    say(current == State.Running)\n} stop'],
      ["Qualified member access", "Access members through their enum name, such as `State.Ready`. Public enums imported from a module can use an alias, such as `states.State.Ready`."],
      ["Type safety", "Enum values are not strings or ordinary numbers. Assignments, function arguments and returns, and equality comparisons preserve nominal enum type; values from different enums cannot be compared."],
      ["Visibility and backend", "Enums declared in modules are private by default. Export with `pub enum`. Enum values and comparisons work in the interpreter and Windows x64 native backend; Linux native aggregate operations are unsupported."]
    ]},
    { id: "structs", title: "Structs", section: "Language", summary: "Group typed fields in nominal records with managed reference semantics.", body: [
      ["Field declarations", "A struct field uses `name: Type` with an optional literal default; a field with a literal default can infer its type. Required fields must precede fields with defaults.", 'start {\n    struct Point {\n        x: number\n        y: number = 0\n    } end struct\n\n    let origin = Point(0)\n    origin.x = 12\n    say(origin.x)\n} stop'],
      ["Construction", "Call the struct name with positional values in declaration order. Omitted fields use their literal defaults. Unknown field types, duplicate fields, invalid constructor arity, and incompatible values are diagnosed."],
      ["Nested values and identity", "Structs can contain other structs and supported arrays. Struct values use reference semantics: assignments and function calls preserve object identity, so mutations through aliases are visible."],
      ["Visibility and backend", "Struct fields are currently public. A module can export a struct with `pub`; access it by an unambiguous public type name or qualify its constructor through the module alias. Runtime values are supported in the interpreter and Windows x64 native backend; Linux native aggregate operations are unsupported."]
    ]},
    { id: "interpreter", title: "Interpreter execution", section: "Runtime", summary: "Run source programs directly and understand interpreter-specific behavior.", body: [
      ["Run a program", "`zp run path/to/main.zp` loads modules, checks the program, and executes it through the interpreter."],
      ["Input and errors", "The built-ins `read()`, `read_number()`, `read_integer()`, and `read_boolean()` read complete standard-input lines and accept an optional string prompt. Invalid typed input and end of input report runtime errors. `try`/`catch` handles runtime exceptions where supported."],
      ["Runtime values", "The interpreter uses double-precision numbers, mutable arrays, class and struct instances, and managed tracing for selected heap values. It supports a broader set of operations than some native targets."],
      ["Standard input", "The process reads from ordinary stdin, so interactive terminals and redirected input can both be used. The prompt is flushed before waiting for input."]
    ]},
    { id: "zpm", title: "ZPM package manager", section: "Tools", summary: "Manage local and registry dependencies with integrity-aware lockfiles.", body: [
      ["Common commands", "`zpm init`, `zpm add`, `zpm install`, `zpm list`, `zpm build`, `zpm test`, `zpm remove`, `zpm pack`, and `zpm publish` support package workflows. See the dedicated ZPM page and the full package guide for details.", "zpm init my-app\nzpm add ..\\my-library\nzpm install --locked\nzpm list"],
      ["Manifest and lock", "Package metadata lives in `zpm.json`; a project lockfile records the manifest hash, resolved versions, and content hashes. `zpm install --locked` restores the exact graph and refuses lock changes."],
      ["Sources and storage", "ZPM accepts local directories and `.zpm` archives as well as registry packages. Installed packages live under `z+_packages/`; verified package sources may be restored from the local cache offline."],
      ["Security", "Package metadata, sources, and archives are validated, and SHA-256 integrity is checked. Installation does not run arbitrary package scripts. The official website registry provides search, version resolution, publishing, and archive download endpoints."]
    ]},
    { id: "zpbuild", title: "ZPBuild build system", section: "Tools", summary: "Configure, build, test, and inspect multi-target Z+ projects.", body: [
      ["Primary command", "Use `zpbuild`; `zbuild` remains a compatibility alias. Commands include `init`, `new`, `configure`, `build`, `plan`, `run`, `check`, `test`, `clean`, `rebuild`, `info`, `targets`, `graph`, and `package`."],
      ["Configuration", "Projects use `zproject.toml` with project, build, profile, compiler, and target settings. The parser intentionally supports a documented TOML subset, not every TOML feature."],
      ["Build graph", "ZPBuild plans target dependencies and can execute independent ready targets in parallel. Build signatures include compiler identity, configuration, reachable source/module content, and relevant assets. Cache controls are available on the CLI."],
      ["Test runner", "Configure test targets and run them with `zpbuild test`. Program and test exit codes propagate to the caller. The compiler remains responsible for Z+ parsing, semantics, and code generation."]
    ]},
    { id: "vscode", title: "Visual Studio Code extension", section: "Tools", summary: "Use Z+ syntax support and compiler-backed commands in VS Code.", body: [
      ["Files and editing", "The extension recognizes `.zp` and `.zh`, provides syntax highlighting and snippets, and includes formatter support based on lexical scanning. The README documents the current feature set."],
      ["Compiler workflows", "Commands can check, run, and build a file and invoke ZPBuild and ZPM project workflows. Compiler, ZPBuild, and ZPM executable paths are configurable; otherwise, tools are discovered on PATH."],
      ["Analysis boundaries", "Diagnostics are backed by the compiler. The extension has workspace symbol and source-reference features, but it is not a Language Server Protocol implementation and does not provide a debugger or claim compiler type inference for completion ranking."],
      ["Distribution status", "The current extension package is version 1.3.4 in this checkout, but no verified Marketplace listing or extension asset appears in the official release feed. The website marks the download unavailable until a valid public asset is published."]
    ]},
    { id: "zgraphics", title: "ZGraphics module", section: "Standard library", summary: "Create a native window and draw basic 2D shapes from Z+.", body: [
      ["API overview", "The `zgraphics` module exposes window creation and lifecycle, titles, positions and sizing, background colors, text and basic shapes, frame updates, keyboard and mouse state, and an event queue."],
      ["Platforms", "The native Windows backend uses Win32 and GDI. Linux uses X11 for interpreter execution and needs X11 development files at build time and a display at runtime. macOS is not implemented. Linux native linking is not implemented."],
      ["Runtime requirements", "Windows native applications need `zgraphics_runtime.dll` beside the executable. Keep the executable and runtime DLL together when distributing an application."],
      ["Limitations", "ZGraphics is a platform layer, not a full GUI toolkit. Invalid handles, dimensions, colors, and frame values raise runtime errors. Consult the module guide for the complete API and limits."]
    ]},
  ];

  const wiki = [
    { id: "syntax", title: "Z+ Syntax", section: "Language reference", summary: "A navigable reference to the syntax and behavior documented in docs/syntax.md and verified against the compiler.", body: [
      ["Program structure", "An executable `.zp` file contains one `start { ... } stop` block. Declarations belong inside it; `use` directives are at file scope. A `.zh` module has a declaration-only `start` block. Brace blocks require their matching kind-specific terminator, and legacy colon blocks cannot be mixed with brace blocks.", `start {
    fn greet(name: string): string {
        return "Hello, " + name
    } end fn

    say(greet("Z+"))
} stop`],
      ["Lexical structure and identifiers", "Keywords are lowercase and case-sensitive. Identifiers start with a letter or underscore and continue with letters, digits, or underscores. The lexer skips `//` line comments and `/* ... */` block comments; block comments are not nested. Newlines separate statements; indentation is not significant."],
      ["Keywords", "The case-sensitive reserved spellings are `start`, `stop`, `end`, `let`, `const`, `number`, `string`, `char`, `character`, `boolean`, `bool`, `void`, `fn`, `return`, `if`, `else`, `while`, `for`, `repeat`, `break`, `continue`, `try`, `catch`, `throw`, `defer`, `switch`, `case`, `default`, `struct`, `class`, `enum`, `pub`, `public`, `private`, `protected`, `this`, `namespace`, `import`, `use`, `export`, `as`, `say`, `printz`, `true`, `false`, `null`, `nullptr`, `and`, `or`, `not`, and `is`. Reserved does not always mean supported: `public` is not a visibility modifier, legacy `import`/`export` forms do not load files, and `printm` is an identifier-based deprecated alias."],
      ["Variables and types", "`let name [: type] [= expression]` declares a mutable variable. `const name [: type] = expression` requires an initializer and cannot be reassigned. Primitive type spellings are `number`, `string`, `char`/`character`, `boolean`/`bool`, and `void`; `void` is an annotation, not a value type. Arrays use `T[]`; structs, classes, and enums are nominal custom types.", `start {
    let count: number = 3
    const label: string = "items"
    let names: string[] = ["Ada", "Lin"]
    say(label + ": " + to_string(count))
} stop`],
      ["Literals", "Numbers include decimal, binary (`0b`/`0B`), hexadecimal (`0x`/`0X`), and scientific notation and are represented as IEEE-754 doubles. Signs are unary operators. Strings use double quotes and characters use single quotes. String escapes are `\\n`, `\\t`, `\\r`, `\\b`, `\\f`, `\\v`, `\\a`, `\\0`, `\\\\`, `\\\"`, and `\\'`; physical newlines are not allowed inside literals. `true`, `false`, `null`, and `nullptr` are recognized literals.", `start {
    say(12.5e2)
    say(0x10)
    say("first\\nsecond")
    say('Z')
    say(false)
} stop`],
      ["Operators and expressions", "Expressions include literals, identifiers, parenthesized expressions, calls, arrays, indexing, member access, unary/binary operations, assignments, and the conditional `?:` expression. Operators are assignment (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`); logical (`or`, `||`, `and`, `&&`); bitwise (`&`, `|`, `^`, `~`, `<<`, `>>`); equality (`==`, `!=`); comparison (`<`, `<=`, `>`, `>=`, `is`); arithmetic (`+`, `-`, `*`, `/`, `%`); unary (`+`, `-`, `!`, `not`, `~`, prefix `++`/`--`); postfix `++`/`--`; and call, index, `.`, and `->` access forms. Precedence from low to high is assignment, conditional, logical OR, logical AND, bitwise OR, XOR, AND, equality, comparison/type test, shifts, addition/subtraction, multiplication/division/remainder, prefix unary, chained access/calls/postfix, and primary expressions. Assignment is right-associative; other binary tiers group left-to-right. Logical forms short-circuit. `is` checks struct instances in the interpreter and Windows x64 native backend; it is not general reflection. `as` is not a cast operator.", `start {
    let total: number = (4 + 2) * 3
    let valid: boolean = total >= 18 and true
    let label: string = valid ? "ready" : "check"
    say(label)
} stop`],
      ["Functions", "Functions use `fn name(parameters) [: returnType] { ... } end fn`; the legacy `->` return separator is also accepted. Types are declared explicitly, trailing parameters may have defaults, and a `void` function can omit a return value. Generic functions, structs, and classes support `T: number` and `T: equatable` constraints, with interpreter and Windows x64 native specialization.", `start {
    fn add(left: number, right: number): number {
        return left + right
    } end fn

    say(add(2, 3))
} stop`],
      ["Statements and control flow", "Supported statements include `if`/`else if`/`else`, `while`, `repeat(count)`, C-style `for`, range and array `for`, `switch`/`case`/`default`, `break`, and `continue`. Range iteration is inclusive at the start and exclusive at the end. Switch cases do not fall through; `match` is not implemented.", `start {
    for value in 0..4 {
        if value == 2 {
            say("middle")
        } else {
            say(value)
        } end if
    } end for
} stop`],
      ["Error handling and cleanup", "`try` requires one `catch`; the caught `Error` exposes a `message` field. `throw` raises a value. `defer` accepts exactly one direct named function call and runs calls in last-in, first-out order when the lexical scope exits.", `start {
    try {
        throw "problem"
    } catch error {
        say(error.message)
    } end try
} stop`],
      ["Arrays and collections", "Arrays are mutable, indexed from zero, and typed with `T[]`. Core operations include indexing, `length`, `push`, `insert`, `pop`, `remove`, `contains`, and `clear`. Maps and sets are not implemented. Arrays execute in the interpreter and Windows x64 native backend; Linux native rejects array operations.", `start {
    let values: number[] = [4, 7]
    push(values, 9)
    say(values[2])
    say(length(values))
} stop`],
      ["Structs", "Structs are nominal types with typed fields (`field: Type`) and optional literal defaults. Required fields precede defaulted fields; construction is positional. Nested structs, arrays in fields, and function arguments/returns are supported. Struct instances use reference semantics and fields are public. Interpreter and Windows x64 native support are implemented; Linux native aggregate operations are unsupported.", `start {
    struct Point {
        x: number
        y: number = 0
    } end struct

    let origin = Point(0)
    origin.x = 12
    say(origin.x)
} stop`],
      ["Classes and visibility", "Classes support typed fields, literal initializers, implicit positional construction, instance methods, and `this`. The `let` before a class field is optional. Members default to public for compatibility. `pub`, `private`, and `protected` are recognized; private and protected access is currently restricted to methods of the declaring class. Inheritance, subclass access, and explicit constructors are not implemented.", `start {
    class Counter {
        private value: number = 0
        pub fn increment() {
            this.value = this.value + 1
        } end fn
        pub fn current(): number {
            return this.value
        } end fn
    } end class

    let counter = Counter()
    counter.increment()
    say(counter.current())
} stop`],
      ["Enums", "Enums are nominal types with integer-valued members. The first omitted value is zero; each later omitted value increments the previous value. Explicit values must be signed decimal, binary, or hexadecimal integer literals in the exactly representable range. Enum equality requires matching enum types. Module enums are private by default and can be exported with `pub`.", `start {
    enum State {
        Ready
        Running = 4
        Done
    } end enum

    let current: State = State.Running
    say(current == State.Running)
    say(State.Done)
} stop`],
      ["Modules and imports", "A `use` directive loads a standard-library module or a relative `.zh` file. Paths resolve from the importing file; aliases provide qualified access. Module declarations are private by default and require `pub` to export. Cycles and missing dependencies are diagnosed. A public imported struct can be constructed through an alias, but dotted type annotations are unsupported.", { name: "main.zp", code: `use "greetings.zh" as greetings
start {
    say(greetings.hello())
} stop`, files: [{ name: "greetings.zh", code: `start {
    pub fn hello(): string {
        return "Hello"
    } end fn
} stop` }] }],
      ["Built-in functions", "`say(value)` writes one value followed by a newline. `printz(value, ...values)` concatenates arguments without a separator or final newline; deprecated `printm` is an alias. `read()` reads a complete line and accepts an optional prompt; `read_number`, `read_integer`, and `read_boolean` validate and convert input. Array built-ins are listed in the array section. Native output currently accepts strings only."],
      ["Standard library", "The standard library is a collection of imported modules rather than methods added to primitive values. For example, `zstring` supplies functions such as `trim`, `upper`, `split`, and `replace`; `zfile` handles file I/O; `zpath` handles host paths; and `zjson`, `zhttp`, and `zsqlite` expose structured data, HTTP, and SQLite APIs. Exact signatures and interpreter/native platform coverage are module-specific; see the checked-in `stdlib/README.md`.", `use zstring as text
start {
    say(text.upper(text.trim(" z+ ")))
} stop`],
      ["Compiler and execution behavior", "`zp check file.zp` validates through parsing, semantic analysis, and IR generation without running. `zp run file.zp` executes with the interpreter. `zp build file.zp` emits native code for the selected platform's supported subset and does not fall back to interpretation. Interpreter support is broader; Linux native support is narrower than Windows x64 for aggregates and some runtime integrations."],
      ["Unsupported or limited constructs", "Pointer types and some pointer operators are parsed but do not have runtime support. `public` is reserved but unsupported as a modifier; there is no class inheritance, function-value type, match expression, map/set type, string interpolation, or cast operator. Consult the matching sections in `docs/syntax.md` and the native backend guide for details and current backend tables."]
    ]},
    { id: "idea", title: "The Idea Behind Z+", section: "About Z+", summary: "The project’s approach to language design, compiler stages, and execution modes.", body: [
      ["Why Z+ exists", "The checked-in specification describes what the project implements, but it does not record a detailed origin story or a single founding problem statement. This page therefore focuses on the verifiable idea visible in the language and toolchain: a project-owned language with explicit syntax, compiler diagnostics, an interpreter, and a native code-generation path."],
      ["A language you can inspect", "Z+ uses explicit `start`/`stop` program boundaries, typed declarations, named functions, and blocks with matching terminators. Types and semantic checks aim to catch invalid operations before execution. Some reserved spellings are unsupported, and the syntax reference calls those out instead of treating every token as a feature."],
      ["A staged compiler", "The compiler tokenizes source, parses it into an AST, performs semantic analysis, builds intermediate representation, and then either executes through the interpreter or generates native code for a supported subset. The compiler remains the authority for accepted syntax and diagnostics."],
      ["Two execution paths", "`zp run` executes through the interpreter; `zp build` emits a native executable when the program uses supported operations. The interpreter currently supports a broader set of runtime operations. Native support differs by platform and subsystem, so successful checking is not proof that every target can build the program."],
      ["The standard library and tools", "The standard library supplies modules for text, files, paths, time, math, JSON, networking, databases, processes, and other areas. ZPM manages packages; ZPBuild coordinates project targets and tests; the VS Code extension provides editor integration. Each tool has its own limitations documented in its reference."],
      ["Direction", "The project is expanding the language foundations and developer tools while retaining compatibility with existing source where possible. Planned items are not automatically supported: use the syntax and runtime pages for implemented behavior, and treat roadmap material as future work."]
    ]},
    { id: "uses", title: "What Is Z+ Used For?", section: "Practical guide", summary: "Where the current compiler and toolchain can be applied, and where their limits matter.", body: [
      ["Console programs and scripts", "The interpreter can run a `.zp` entry program with terminal input/output, functions, conditions, loops, arrays, error handling, and standard-library calls. This is suitable for learning, automation, and console utilities whose dependencies are supported by the interpreter.", `start {
    let name: string = read("Name: ")
    say("Hello, " + name)
} stop`],
      ["Native executables", "`zp build` generates a native executable for supported source operations. Basic typed functions, expressions, control flow, strings, and other documented subsets can be compiled; arrays and aggregate types have target-specific limits. Verify the actual target by building and running the produced executable."],
      ["Modular applications", "Relative `.zh` modules, explicit `pub` exports, and ZPBuild project targets support splitting a program into files. ZPM can resolve and install package dependencies, but package installation does not mean every package operation is natively supported by every target."],
      ["Programs using the standard library", "Use modules such as `zfile`, `zpath`, `zstring`, `zjson`, and `zhttp` when their documented APIs fit the task. Check module-specific platform notes: Windows x64 is the broader native target, while Linux native support remains narrower for aggregates and some runtime integrations."],
      ["Current limitations", "Z+ does not currently provide class inheritance, maps/sets, function values, string interpolation, or a native fallback through the interpreter. ZGraphics is a basic platform drawing layer rather than a complete GUI framework. Treat graphics, audio, physics, and other platform integrations according to their documented status and test them on the target system."],
      ["Choosing a project", "Z+ is usable today for learning the language, console utilities, modular applications, interpreter-driven scripts, and native executables within the supported subset. For production use, validate the full application on its intended OS, runtime dependencies, and backend; no benchmark or cross-language performance guarantee is implied."]
    ]}
  ];

  const zts = {
    id: "zts",
    title: "ZTS — TypeScript interoperability",
    section: "Experimental web interoperability",
    summary: "Generate a typed TypeScript-facing client for JSON APIs served by Z+.",
    body: [
      ["Status and scope", "ZTS is an experimental interoperability workflow, not a new language mode or a TypeScript compiler. It uses existing TypeScript and npm tooling together with the Z+ web interoperability generator. The generator produces a JavaScript Fetch client and a matching `.d.ts` declaration file from an OpenAPI JSON contract."],
      ["Version and changelog", "ZTS has no separate stable release or binary. The generator is experimental and unpublished. Its documented first version generates a Fetch ES module and matching TypeScript declarations from supported OpenAPI 3.0/3.1 JSON. No standalone ZTS compiler or downloadable binary is published."],
      ["Install and generate", "The generator is not currently published to npm or offered as a download on this website. No installation link is provided until an official distributable is available. Its documented interface generates `zplus-api.js` and `zplus-api.d.ts` and refuses to overwrite either unless `--force` is supplied."],
      ["Typed operations and shared data", "OpenAPI `components.schemas` become TypeScript types. Supported schemas include local references, objects, arrays, primitives, literal enums, nullable types, and oneOf/anyOf/allOf compositions. Path, query, and header parameters and JSON request bodies become typed operation inputs. This provides shared contract types, but does not inspect Z+ source or prove that server routes match the contract."],
      ["Call a Z+ API", "Import `createClient` from `./generated/zplus-api.js`, create a client with `baseUrl`, then call `await api.getGreeting({ path: { name: \"Ada\" }, query: { locale: \"en\" } })`. Responses are checked by TypeScript through the generated declaration. Non-2xx responses reject with `ApiError`; network, abort, and invalid JSON failures are not converted into successful empty values."],
      ["Connect the Z+ server", "ZWeb provides JSON request/response helpers, route matching, and explicit CORS configuration. Serve the OpenAPI contract and implement each described route with the existing `serverNext`, `routeMatch`, and `serverRespondJson` APIs. For browser cookie sessions, configure Fetch `credentials: \"include\"` and matching credential-aware CORS at the trusted deployment boundary."],
      ["Compatibility limits", "The tool accepts a documented subset of OpenAPI 3.0/3.1 JSON; remote references, non-JSON bodies, custom parameter serialization, and unsupported schema keywords are rejected. Z+ does not parse TypeScript source, generate declarations directly from Z+ declarations, compile TypeScript, or provide WebSockets, async handlers, HTTPS transport, or source maps. Use TypeScript's own compiler for TypeScript language features."]
    ]
  };

  const zjs = {
    id: "zjs",
    title: "ZJS — JavaScript interoperability",
    section: "Experimental web interoperability",
    summary: "Use a generated ES module client with native JavaScript Fetch and browser tools.",
    body: [
      ["Status and scope", "ZJS is an experimental JavaScript interoperability workflow, not a JavaScript runtime or a backend language. It generates an ES module that calls JSON endpoints implemented by a Z+ server. The generated client uses the standard Fetch API and has no runtime dependency."],
      ["Version and changelog", "ZJS has no separate stable release or binary. The generator is experimental and unpublished. Its documented first version generates an ES module Fetch client from supported OpenAPI 3.0/3.1 JSON. No standalone ZJS runtime or downloadable binary is published."],
      ["Install and generate", "The generator is not currently published to npm or offered as a download on this website. No installation link is provided until an official distributable is available. When the generator is available in a developer environment, its command is `zplus-web-interop generate api/openapi.json --out src/generated`; import the generated client from `./generated/zplus-api.js`."],
      ["Use JavaScript modules", "Create a client with `createClient({ baseUrl: \"https://api.example.test\" })` and call generated operations with `await api.getGreeting({ path: { name: \"Ada\" } })`. Use the caller's existing JavaScript module system, bundler, and npm packages; this tool does not rewrite or bundle the rest of the application."],
      ["Promises, browser APIs, and errors", "Generated operations return Promises and accept an optional `AbortSignal`. Use browser-native DOM, events, WebSocket, and other Web APIs directly. The generated client does not polyfill browser APIs or add a Z+ WebSocket feature. HTTP error responses reject with `ApiError`, including status, body, and the original response."],
      ["Authentication and transport", "Supply bearer headers through the client's `headers` option, or select `credentials: \"include\"` for same-origin or explicitly CORS-enabled cookie sessions. Never put private credentials in browser bundles. The browser may use HTTPS through a trusted host or reverse proxy; the current Z+ `zhttp` client itself only accepts `http://` URLs."],
      ["Compatibility limits", "The generated client supports GET, POST, PUT, PATCH, DELETE, HEAD, and OPTIONS; path/query/header parameters; JSON request bodies; and JSON or bodyless success responses from the supported OpenAPI 3.0/3.1 subset. It does not execute JavaScript inside Z+, provide a JS server runtime, add WebSockets to ZWeb, or guarantee that an API contract matches its application implementation."]
    ]
  };

  const examples = [
    { id: "hello", title: "Hello World", summary: "The smallest executable Z+ program.", code: `start {
    say("Hello, Z+!")
} stop` },
    { id: "calculator", title: "Calculator", summary: "A small typed arithmetic function.", code: `start {
    fn add(left: number, right: number): number {
        return left + right
    } end fn
    say(add(12, 30))
} stop` },
    { id: "guessing", title: "Number guessing", summary: "A console guessing game using input, conditions, and a loop.", code: `start {
    let secret: number = 7
    let guess: number = 0
    while guess != secret {
        say("Guess a number from 1 to 10:")
        guess = read_integer()
        if guess == secret {
            say("Correct")
        } else if guess < secret {
            say("Too low")
        } else {
            say("Too high")
        } end if
    } end while
} stop` },
    { id: "text", title: "Text processing", summary: "Normalize and inspect text with zstring.", code: `use zstring as text
start {
    let cleaned: string = text.trim("  Z+  ")
    say(text.upper(cleaned))
} stop` },
    { id: "files", title: "File handling", summary: "Write and read lines with the public zfile API.", code: `use zfile as file
start {
    file.writeLines("notes.txt", ["First", "Second"])
    for line in file.readLines("notes.txt") {
        say(line)
    } end for
} stop` },
    { id: "collections", title: "Collections", summary: "Mutate and iterate over a typed dynamic array.", code: `start {
    let values: number[] = [4, 8]
    push(values, 12)
    for value in values {
        say(value)
    } end for
} stop` },
    { id: "functions", title: "Functions", summary: "Reuse typed logic and return a value.", code: `start {
    fn square(value: number): number {
        return value * value
    } end fn
    say(square(6))
} stop` },
    { id: "modules", title: "Multi-file project", summary: "Import an explicitly exported declaration from a sibling module.", code: `use "greetings.zh" as greetings
start {
    say(greetings.hello())
} stop`, relatedFiles: [{ name: "greetings.zh", code: `start {
    pub fn hello(): string {
        return "Hello from a module"
    } end fn
} stop` }] },
    { id: "todo", title: "TODO application", summary: "Add, list, and save tasks in a persistent text file.", code: `use zfile as file
start {
    let filePath: string = "tasks.txt"
    if not file.exists(filePath) {
        file.writeLines(filePath, [])
    } end if

    let running: boolean = true
    while running {
        say("TODO: add, list, or quit")
        let command: string = read()
        if command == "add" {
            say("Task:")
            let task: string = read()
            let tasks: string[] = file.readLines(filePath)
            push(tasks, task)
            file.writeLines(filePath, tasks)
            say("Saved")
        } else if command == "list" {
            for task in file.readLines(filePath) {
                say(task)
            } end for
        } else if command == "quit" {
            running = false
        } else {
            say("Unknown command")
        } end if
    } end while
} stop` },
  ];

  return { course, docs, wiki, examples, zts, zjs };
})();
