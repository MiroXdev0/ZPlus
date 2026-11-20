window.ZPlusSite = (() => {
  const l = (title, explain, code, output, exercise, solution, summary) => ({
    title, explain, code, output, exercise, solution, summary
  });
  const course = [
    { title: "Introduction", lessons: [
      l("What is Z+?", "Z+ is a general-purpose language with a compiler, interpreter, standard library, and ZBuild. A program is text saved in a source file; the compiler checks that text before it runs.", `start {
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
      l("Understanding the Z+ project structure", "A small project can begin with one source file. Larger projects use `.zh` declaration modules and a `zproject.toml` file for ZBuild. Keep source, assets, and tests in separate folders as the project grows.", `start {
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
      l("Output with say() and printz()", "`say(value)` writes one value and a newline. `printz(value, ...values)` writes arguments consecutively with no separator and no final newline. The interpreter can print more value types than the native backend.", `start {
    printz("Count: ", 3)
    say(" done")
} stop`, "Count: 3 done", "Print a label and number with `printz`, then finish the line with `say`.", `start {
    printz("Score: ", 12)
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
      l("Structs and classes", "Structs and classes define named types with fields. A struct is a value-shaped data model; a class can also declare methods. Constructors currently use positional field order and field defaults must be literals.", `start {
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
      l("Creating reusable modules", "A user module is a `.zh` file containing declarations inside a `start` block. Use a relative path from the importing file; declarations are private unless marked `pub`.", `use "greetings.zh" as greetings
start {
    say(greetings.hello())
} stop`, "If greetings.zh returns Hello, the program prints Hello.", "In a sibling `greetings.zh`, export a zero-argument function named `hello` that returns a string.", `start {
    pub fn hello(): string {
        return "Hello"
    } end fn
} stop`, "Relative modules share code and use explicit `pub` visibility for exported declarations."),
      l("Strings and text processing", "`zstring` provides text functions such as `upper`, `lower`, `trim`, `split`, `join`, and `replace`. String length is counted in Unicode code points.", `use zstring as text
start {
    say(text.upper("z plus"))
    say(text.length("Z+"))
} stop`, "Z PLUS\n2", "Trim surrounding spaces from a string and print it.", `use zstring as text
start {
    say(text.trim("  ready  "))
} stop`, "Call the module functions rather than assuming strings have built-in methods."),
      l("Math utilities", "`zmath` includes functions such as `abs`, `round`, `sqrt`, `pow`, `min`, and `max`. The interpreter uses floating-point numbers; Windows x64 native currently restricts numeric runtime values to integers.", `use zmath as math
start {
    say(math.abs(-7))
    say(math.max(3, 9))
} stop`, "7\n9", "Use `math.min` to print the smaller of 12 and 5.", `use zmath as math
start {
    say(math.min(12, 5))
} stop`, "Check backend limits before relying on fractional native math results."),
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
      l("Environment variables", "`zenv` reads and writes process environment values. `exists` distinguishes a missing variable from an existing variable with an empty value. Changes affect only the current process.", `use zenv as env
start {
    say(env.exists("PATH"))
} stop`, "true on a typical configured system", "Check whether a variable named `APP_MODE` exists.", `use zenv as env
start {
    say(env.exists("APP_MODE"))
} stop`, "Treat environment values as external input and check presence before relying on them."),
      l("Time and dates", "`ztime` provides `now`, date-part helpers, `format`, `parse`, and `sleep`. Its format patterns follow the host C library's time-format conventions.", `use ztime as time
start {
    let timestamp: number = time.now()
    say(time.year(timestamp))
} stop`, "The current local year", "Read the current timestamp and print its month.", `use ztime as time
start {
    let timestamp: number = time.now()
    say(time.month(timestamp))
} stop`, "Use the time module for timestamps and formatting rather than guessing date syntax."),
    ]},
    { title: "Input and errors", lessons: [
      l("Reading terminal input", "`read()` reads a line from standard input as a string. Input can be empty or unavailable, so validate it before using it as meaningful data.", `start {
    say("What is your name?")
    let name: string = read()
    say("Hello, " + name)
} stop`, "The greeting includes the line entered by the user.", "Ask the user for a favorite language and print the response.", `start {
    say("Favorite language?")
    let answer: string = read()
    say(answer)
} stop`, "A terminal program becomes interactive by reading input and responding to it."),
      l("Numeric and boolean input", "`read_number`, `read_integer`, and `read_boolean` parse common input types. Invalid input raises a runtime error; it is not silently converted to a valid value.", `start {
    say("Enter an integer:")
    let count: number = read_integer()
    say(count)
} stop`, "The number entered by the user", "Read a number and print it back.", `start {
    let value: number = read_number()
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
} stop`, "Modules separate implementation while keeping explicit exports."),
      l("Using ZBuild", "ZBuild provides project commands and templates. Create a project using the installed `zbuild` tool, inspect its `zproject.toml`, then use `zbuild build` or `zbuild run` from the project directory.", `start {
    say("ZBuild project commands are run in the terminal.")
} stop`, "ZBuild project commands are run in the terminal.", "Create a Console project with ZBuild and read its generated README and project configuration.", `start {
    say("Keep project configuration next to the source tree.")
} stop`, "ZBuild commands are CLI commands; do not put them inside Z+ source."),
      l("Building a release", "A release build uses `zbuild build --release`; packaging uses `zbuild package --release`. Test the program and inspect the package output before sharing it. Native backend support is limited to the documented subset.", `start {
    say("Test the release build before distributing it.")
} stop`, "Test the release build before distributing it.", "Build and package a small ZBuild project in release mode, then verify the produced files.", `start {
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
      l("Creating and sharing packages", "The Z+ package registry is live. Use ZPM to search for packages, install dependencies, and publish versioned ZPM archives; see the package documentation for supported workflows.", `use "utilities.zh" as utilities
start {
    say(utilities.describe())
} stop`, "A shared module can be imported from a relative source path.", "Prepare a small reusable `.zh` module, document its public functions, and share it with its project configuration.", `start {
    pub fn describe(): string {
        return "A documented reusable module"
    } end fn
} stop`, "The registry accepts ZPM archives; package metadata and source files are checked before publication."),
    ]},
  ];

  const docs = [
    { id: "syntax", title: "Syntax reference", section: "Language", summary: "The executable Z+ syntax accepted by the current compiler.", body: [
      ["Program structure", "Every executable `.zp` source has one `start { ... } stop` block. Function, namespace, and type declarations belong inside it. Standard and relative modules use file-scope `use` directives."],
      ["Types and variables", "Use `let` for mutable variables and `const` for initialized immutable variables. Common types include `number`, `string`, `boolean`, and arrays such as `number[]`."],
      ["Blocks and control flow", "Brace blocks use explicit terminators: `} end fn`, `} end if`, `} end while`, `} end for`, and `} stop`. `else if` and `else` remain in the same conditional chain."],
      ["Functions", "Declare `fn name(parameter: type): returnType { ... } end fn`. Parameter and return types follow the current function reference; return types are not inferred."],
      ["Limitations", "Pointers are parsed but are not implemented. Match expressions, inheritance, function values, map/set types, and string interpolation are not implemented."]
    ]},
    { id: "cli", title: "Compiler CLI", section: "Tools", summary: "Check, interpret, and build Z+ programs.", body: [
      ["Commands", "`zp check file.zp` validates without running. `zp run file.zp` executes in the interpreter. `zp build file.zp -o output.exe` requests native compilation."],
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
    { id: "zbuild", title: "ZBuild reference", section: "Tools", summary: "Project build, run, test, and package commands.", body: [
      ["Commands", "`zbuild init [name]` creates a project; `templates` and `template info NAME` inspect templates; `configure` validates a project; `build`, `run`, `test`, `clean`, `rebuild`, `info`, `targets`, `graph`, and `package` manage targets and outputs. `--debug` / `--release`, `--target`, `--jobs`, `--verbose`, and `--clean-first` apply where supported."],
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
      ["Windows setup", "When an official Windows x64 distribution is available, follow its included installation instructions. The Inno Setup source and build instructions are in this repository's `installer/inno` directory."],
      ["Verify", "Open a new terminal and run `zp version` or `zp --version`, then `zbuild --version`. Validate a source program with `zp check hello.zp` and run it with `zp run hello.zp`."],
      ["Troubleshooting", "If a command is not found, open a fresh terminal and verify the installed `bin` directory is in your user PATH. Keep the distribution's runtime DLLs and standard-library directory in their shipped layout."]
    ]},
  ];

  const wiki = [
    { id: "architecture", title: "How the compiler works", summary: "From source text to a running Z+ program.", body: "The compiler pipeline tokenizes source, parses an AST, performs semantic analysis, lowers to a shared intermediate representation, then either runs the interpreter or generates native code. Diagnostics should be fixed at their source stage; native code generation is a separate capability from interpreter support." },
    { id: "project-layout", title: "Organizing projects", summary: "Keep entry points, modules, assets, and tests easy to find.", body: "Begin with a small source tree and keep a clear entry point. Place reusable declaration modules next to the code that imports them, and use relative paths. Larger ZBuild projects use `src/`, `tests/`, and a root `zproject.toml`. Avoid absolute imports so projects remain portable." },
    { id: "modules", title: "How modules work", summary: "Resolution, visibility, initialization, and cycles.", body: "A relative `.zh` import is resolved from its importer, not the current terminal directory. Modules are loaded depth-first and deduplicated by canonical path. Public declarations require `pub`; circular dependencies fail with a dependency-chain diagnostic." },
    { id: "contributing", title: "Contributing to Z+", summary: "Build, test, and report changes against the compiler repository.", body: "Read the language and backend references before changing behavior. Configure with CMake, build the target configuration, run the focused tests, and then run the wider CTest suite. Include a small Z+ regression program for language-visible changes and describe platform limitations." },
    { id: "troubleshooting", title: "Troubleshooting", summary: "Diagnose common installation and compiler problems.", body: "Open a new terminal after a PATH change. Confirm `zp --version` and `zbuild --version`, then run `zp check` before execution. For native failures, check the documented target subset and required runtime DLLs. Include the exact command, compiler version, OS, and diagnostic when reporting a bug." },
    { id: "faq", title: "Frequently asked questions", summary: "Straight answers about the current Z+ implementation.", body: "Does Z+ have maps and sets? No, not currently. Are all standard modules available natively? No; support varies by platform and function. Is the package registry live? Yes; use ZPM to search, install, and publish packages. Does `zp build` fall back to the interpreter? No. Use the docs to verify a feature before depending on it." },
    { id: "packages", title: "Publishing Z+ packages", summary: "Build, publish, and install packages with ZPM.", body: "The Z+ registry accepts versioned ZPM archives. Use `zpm init` to create a package, `zpm pack` to produce an archive, and `zpm publish` or the website's Publish page to publish it. Registry archives are limited to 4 MiB; the server validates metadata and archive contents before publication. Use `zpm search` and `zpm add` to discover and install published dependencies." },
  ];

  const zts = {
    id: "zts",
    title: "ZTS — TypeScript interoperability",
    section: "Experimental web interoperability",
    summary: "Generate a typed TypeScript-facing client for JSON APIs served by Z+.",
    body: [
      ["Status and scope", "ZTS is an experimental interoperability workflow, not a new language mode or a TypeScript compiler. It uses existing TypeScript and npm tooling together with the Z+ web interoperability generator. The generator produces a JavaScript Fetch client and a matching `.d.ts` declaration file from an OpenAPI JSON contract."],
      ["Version and changelog", "ZTS has no separate stable release or binary. The private generator package is version 0.1.0 and experimental. Initial changelog: generate a Fetch ES module and matching TypeScript declarations from supported OpenAPI 3.0/3.1 JSON. No standalone ZTS compiler or downloadable binary is published."],
      ["Install and generate", "The generator is not published to npm. From a frontend project beside a checkout of this repository, install `../Z-lang-release/tools/zplus-web-interop` as a development dependency, then run `npx zplus-web-interop generate api/openapi.json --out src/generated`. The generator writes `zplus-api.js` and `zplus-api.d.ts`; it refuses to overwrite either unless `--force` is supplied."],
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
      ["Version and changelog", "ZJS has no separate stable release or binary. The private generator package is version 0.1.0 and experimental. Initial changelog: generate an ES module Fetch client from supported OpenAPI 3.0/3.1 JSON. No standalone ZJS runtime or downloadable binary is published."],
      ["Install and generate", "The generator is not published to npm. Install the local package from a repository checkout with `npm install --save-dev ../Z-lang-release/tools/zplus-web-interop`, then run `npx zplus-web-interop generate api/openapi.json --out src/generated`. Import the generated client from `./generated/zplus-api.js` in an existing ES module or npm application."],
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
