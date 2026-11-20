ITERATIONS = 50000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 3034241  # BENCH_EXPECTED


def transform(value):
    return (value * 7 + 11) % 103


checksum = 0
for i in range(ITERATIONS):
    value = transform(i % 1000)
    if i % 3 == 0:
        checksum += value * 2
    else:
        checksum += (value + i % 17) % 89

print(f"CHECKSUM combined {checksum}")
