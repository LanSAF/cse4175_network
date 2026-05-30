#!/bin/bash

SENDER_SRC="sender_20201636.cc"
RESULTS="results.csv"

# 탐색할 파라미터 범위 (-q 결과 반영)
# WINDOW_SIZE: 100이 압도적 → 더 큰 값까지 탐색
# P_INIT:      500 근방이 좋음 → 300~700 집중
# WARM_UP:     20이 약간 유리 → 15~25 집중
# P_MIN:       영향 거의 없음 → 16, 32만 유지
P_INITS=(450 500 550)
P_MINS=(32 48)
WARM_UPS=(24 25 26)
WINDOW_SIZES=(150 155)

# Validation 시나리오
INPUTS=("sherlock_holmes.txt" "cat_bgm.mp3" "cat_bgm.mp3" "harry_potter.txt")
BERS=("1e-6" "1e-5" "1e-4" "1e-3")
SEEDS=("1001" "2002" "3003" "4004")
MAX_BYTES=("386832300" "316224000" "316224000" "44276800")

# 빠른 모드: -q 옵션 시 Validation 4만 실행 (harry_potter, BER=1e-3)
QUICK=false
if [ "$1" == "-q" ]; then
    QUICK=true
    echo "[Quick mode] Validation 4만 실행합니다."
fi

echo "P_INIT,P_MIN,WARM_UP,WINDOW_SIZE,cost_v1,cost_v2,cost_v3,cost_v4,total_cost" > $RESULTS

total=$((${#P_INITS[@]} * ${#P_MINS[@]} * ${#WARM_UPS[@]} * ${#WINDOW_SIZES[@]}))
run=0

for p_init in "${P_INITS[@]}"; do
for p_min in "${P_MINS[@]}"; do
for warm_up in "${WARM_UPS[@]}"; do
for window_size in "${WINDOW_SIZES[@]}"; do
    run=$((run + 1))

    # WARM_UP은 WINDOW_SIZE보다 작아야 함
    if [ $warm_up -ge $window_size ]; then
        continue
    fi

    printf "[%d/%d] P_INIT=%-5d P_MIN=%-3d WARM_UP=%-3d WINDOW_SIZE=%-3d  " \
        $run $total $p_init $p_min $warm_up $window_size

    # 파라미터를 -D 플래그로 넘겨 컴파일
    g++ -O2 \
        -DP_INIT=$p_init \
        -DP_MIN=$p_min \
        -DWARM_UP=$warm_up \
        -DWINDOW_SIZE=$window_size \
        -o sender_exp \
        $SENDER_SRC netsim_lib.cc -lm 2>/dev/null

    if [ $? -ne 0 ]; then
        echo "COMPILE ERROR"
        continue
    fi

    costs=("N/A" "N/A" "N/A" "N/A")
    total_cost=0
    fail=false

    if $QUICK; then
        run_list=(3)   # Validation 4만
    else
        run_list=(0 1 2 3)
    fi

    for i in "${run_list[@]}"; do
        out=$(./netsim ./sender_exp \
            --input "${INPUTS[$i]}" \
            --output out_exp.rx \
            --ber "${BERS[$i]}" \
            --seed "${SEEDS[$i]}" \
            --max_bytes "${MAX_BYTES[$i]}" 2>&1)

        status=$(echo "$out" | grep "^status:" | awk '{print $2}')
        cost=$(echo "$out"   | grep "^cost:"   | awk '{print $2}')

        if [ "$status" != "SUCCESS" ] || [ -z "$cost" ]; then
            fail=true
            costs[$i]="FAIL"
        else
            costs[$i]=$cost
            total_cost=$((total_cost + cost))
        fi
    done

    if $fail; then
        echo "FAIL"
        result_str="FAIL"
    else
        echo "total=$total_cost"
        result_str=$total_cost
    fi

    echo "$p_init,$p_min,$warm_up,$window_size,${costs[0]},${costs[1]},${costs[2]},${costs[3]},$result_str" >> $RESULTS

done
done
done
done

rm -f sender_exp out_exp.rx

echo ""
echo "=============================="
echo " Top 10 결과 (total_cost 기준)"
echo "=============================="
printf "%-8s %-6s %-8s %-12s %-12s %-12s %-12s %-12s %-12s\n" \
    P_INIT P_MIN WARM_UP WINDOW_SIZE cost_v1 cost_v2 cost_v3 cost_v4 total_cost
echo "---------------------------------------------------------------------------------------------------"
tail -n +2 $RESULTS | grep -v FAIL | sort -t',' -k9 -n | head -10 | \
    awk -F',' '{printf "%-8s %-6s %-8s %-12s %-12s %-12s %-12s %-12s %-12s\n", $1,$2,$3,$4,$5,$6,$7,$8,$9}'

echo ""
echo "전체 결과: $RESULTS"