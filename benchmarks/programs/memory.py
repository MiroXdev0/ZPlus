ITERATIONS = 10000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 69995  # BENCH_EXPECTED

checksum = 0
for i in range(ITERATIONS):
    transient = [i, i % 11 + 2, i + 1]
    checksum += transient[1]

if checksum == EXPECTED_CHECKSUM:
    print(f"CHECKSUM memory {checksum}")
else:
    print("CHECKSUM_ERROR memory")
