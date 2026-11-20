ITERATIONS = 10000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 155000  # BENCH_EXPECTED

checksum = 0
for i in range(ITERATIONS):
    value = i % 10 + 0.5
    checksum += int((value * 1.5 + 0.25) * 2)

if checksum == EXPECTED_CHECKSUM:
    print(f"CHECKSUM mixed {checksum}")
else:
    print("CHECKSUM_ERROR mixed")
