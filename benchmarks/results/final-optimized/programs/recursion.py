ITERATIONS = 50000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 1191560  # BENCH_EXPECTED

def recursive_sum(value):
    if value == 0:
        return 0
    return value + recursive_sum(value - 1)

checksum = 0
for i in range(ITERATIONS):
    checksum += recursive_sum(i % 12)

print(f"CHECKSUM recursion {checksum}")
