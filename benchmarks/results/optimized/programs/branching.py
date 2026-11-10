ITERATIONS = 50000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 3124925000  # BENCH_EXPECTED

checksum = 0
for i in range(ITERATIONS):
    if i % 2 == 0:
        checksum += i * 3 + 1
    else:
        checksum += i * 2 - 1

print(f"CHECKSUM branching {checksum}")
