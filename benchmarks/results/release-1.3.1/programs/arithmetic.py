ITERATIONS = 50000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 2399670  # BENCH_EXPECTED

checksum = 0
for i in range(ITERATIONS):
    value = (i * 3 + 7) % 97
    checksum += value

print(f"CHECKSUM arithmetic {checksum}")
