ITERATIONS = 10000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 39994  # BENCH_EXPECTED

value = 0
for i in range(ITERATIONS):
    value += i % 7 + 1

if value == EXPECTED_CHECKSUM:
    print(f"CHECKSUM structs {value}")
else:
    print("CHECKSUM_ERROR structs")
