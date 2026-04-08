for i in $(seq 0 99); do
    echo "[.] Testing iteration $i"
    fast=$(./query_large db_$i.txt queries_$i.txt)
    slow=$(./naive_queries db_$i.txt queries_$i.txt)

    if [ "$fast" != "$slow" ]; then
        echo "[X] Mismatch on iteration $i: fast=$fast slow=$slow"
        exit 1
    fi
    echo "    [O] Output: $fast"
done

echo "All 100 iterations matched."
