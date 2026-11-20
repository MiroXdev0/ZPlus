ITERATIONS = 10000  # BENCH_ITERATIONS
EXPECTED_CHECKSUM = 29994  # BENCH_EXPECTED

values = [0] * 8
for i in range(ITERATIONS):
    values[i % 8] += i % 7

checksum = sum(values)
if checksum == EXPECTED_CHECKSUM:
    print(f"CHECKSUM arrays {checksum}")
else:
    print("CHECKSUM_ERROR arrays")
