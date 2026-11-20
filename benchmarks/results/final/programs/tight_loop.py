ITERATIONS = 1000000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 1499999500000  # BENCH_EXPECTED

checksum = 0
for i in range(ITERATIONS):
    checksum += i * 3 + 1

print(f"CHECKSUM tight_loop {checksum}")
