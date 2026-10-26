#!/usr/bin/env bash
# Build one practice scenario and diff every tests/*.txt against expected/*.out
#
#   ./run.sh p1_fifo              build YOUR paging.c, run all tests
#   ./run.sh p1_fifo 2            run only the 2nd test (sorted order)
#   ./run.sh p1_fifo sol          same, but with the reference solution.c
#   ./run.sh p1_fifo sol --regen  (author only) rewrite expected/ from solution.c
#
# p1..p5, p7, p8 link  framework/paging_test_driver.c + framework/sim_framework.c + <paging.c|solution.c>
# p6          is a standalone program (trace_sim.c | solution.c)
set -u
cd "$(dirname "$0")"

scenario="${1:-}"
[ -d "$scenario" ] || { echo "usage: $0 <p1_fifo|p2_clock|p3_swap_cache|p4_fault_stats|p5_readahead|p6_trace_sim|p7_robust_ids|p8_fewest_evictions> [N | sol] [--regen]"; exit 2; }
shift

use_sol=0; regen=0; only=""
for a in "$@"; do
  case "$a" in
    sol) use_sol=1 ;;
    --regen) regen=1 ;;
    [0-9]*) only="$a" ;;
  esac
done

mkdir -p .build
bin=".build/${scenario}$([ $use_sol = 1 ] && echo _sol)"

if [ "$scenario" = "p6_trace_sim" ]; then
  src="$scenario/$([ $use_sol = 1 ] && echo solution.c || echo trace_sim.c)"
  gcc -Wall -Wextra -std=c99 -g "$src" -o "$bin" || exit 1
else
  src="$scenario/$([ $use_sol = 1 ] && echo solution.c || echo paging.c)"
  gcc -Wall -Wextra -std=c99 -g -Iframework \
      framework/paging_test_driver.c "$src" framework/sim_framework.c -o "$bin" || exit 1
fi

pass=0; total=0; n=0
for t in "$scenario"/tests/*.txt; do
  n=$((n+1))
  [ -n "$only" ] && [ "$n" != "$only" ] && continue
  name=$(basename "$t" .txt)
  exp="$scenario/expected/$name.out"
  if [ $regen = 1 ]; then
    "$bin" "$t" > "$exp"; echo "regenerated $exp"; continue
  fi
  total=$((total+1))
  # perl alarm = portable 5s timeout (macOS has no `timeout`)
  actual=$(perl -e 'alarm 5; exec @ARGV' "$bin" "$t" 2>&1; echo "exit=$?")
  expected=$(cat "$exp"; echo "exit=0")
  if [ "$actual" = "$expected" ]; then
    pass=$((pass+1)); echo "PASS  $n  $name"
  else
    echo "FAIL  $n  $name"
    diff <(echo "$expected") <(echo "$actual") | head -40
  fi
done
[ $regen = 1 ] || echo "Summary: $pass/$total passed"
[ $regen = 1 ] || [ "$pass" = "$total" ]
