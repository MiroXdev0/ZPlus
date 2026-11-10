ITERATIONS = 50000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 2492250  # BENCH_EXPECTED


def arithmetic(value):
    return (value * 7 + 3) % 101


checksum = 0
for i in range(ITERATIONS):
    checksum += arithmetic(i % 1000)

print(f"CHECKSUM functions {checksum}")
